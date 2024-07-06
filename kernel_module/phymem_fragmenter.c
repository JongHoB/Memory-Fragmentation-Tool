// Reference Code is from saravan2/phymem_fragmenter
// Modified by Jongho Baik

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmstat.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/swap.h>
#include <linux/page_ref.h>
#include <linux/memcontrol.h>
#include <linux/mm_inline.h>
#include <linux/mmzone.h>


#if defined CONFIG_TRANSPARENT_HUGEPAGE
#define COMPACTION_HPAGE_ORDER HPAGE_PMD_ORDER
#elif defined CONFIG_HUGETLBFS
#define COMPACTION_HPAGE_ORDER HUGETLB_PAGE_ORDER
#else
#define COMPACTION_HPAGE_ORDER (PMD_SHIFT - PAGE_SHIFT)
#endif

#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 10
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

static int order;
static int fragmentation_score;
static long nr_allocated_pages;

module_param(order, int, 0);
MODULE_PARM_DESC(order, "Order of the page to allocate and fragment");

module_param(fragmentation_score, int, 0);
MODULE_PARM_DESC(fragmentation_score, "Fragmentation score to stop the Fragmenter");

LIST_HEAD(fragment_list);
LIST_HEAD(alloc);

struct contig_page_info
{
  unsigned long free_pages;
  unsigned long free_blocks_total;
  unsigned long free_blocks_suitable;
};

typedef struct
{
  int score[2];
  int node[2];
  int total_node;
} fragmentation_score_t;

typedef struct
{
	struct page *page;
	struct list_head lru;
} page_list;

unsigned int extfrag_for_order(struct zone *zone, unsigned int order);
void score_printer(void);
int create_fragments(void *arg);
int fragmenter_init(void);
int release_fragments(void);
int alloc_mem(void);
void fragmenter_exit(void);
int release_alloc(void);
fragmentation_score_t get_fragmentation_score(void);

// Caculate the Fragmentation Score of the system
// Fragmentation Score is used by Proactive Compaction
// to determine whether to compact or not
// The Linux kernel divides a node’s memory into
// “zones”, typically ZONE_DMA32, ZONE_DMA, and ZONE_NORMAL,
// where the last one holds the most memory while others are special purpose zones
// and are typically manage small amounts of total RAM.
// Each Node Score is sum of it's zones scores
// Zone Score is calculated by (total number of pages in the zone)/(total number of pages in the zone's parent node)*extfrag(zone,HUGETLB_PAGE_ORDER)
// extfrag(zone,HUGETLB_PAGE_ORDER) is the external fragmentation of the huge page order in this zone.
// It is calculated by {(total number of free pages in the zone)-(the number of free pages available in blocks of size >= 2^order)}/(total number of free pages in the zone)*100
// If total number of free pages in the zone is 0, then the zone score is 0

static void fill_contig_page_info(struct zone *zone,
                                  unsigned int suitable_order,
                                  struct contig_page_info *info)
{
  unsigned int order;

  info->free_pages = 0;
  info->free_blocks_total = 0;
  info->free_blocks_suitable = 0;

  for (order = 0; order <= MAX_ORDER; order++)
  {
    unsigned long blocks;

    /*
     * Count number of free blocks.
     *
     * Access to nr_free is lockless as nr_free is used only for
     * diagnostic purposes. Use data_race to avoid KCSAN warning.
     */
    blocks = data_race(zone->free_area[order].nr_free);
    info->free_blocks_total += blocks;

    /* Count free base pages */
    info->free_pages += blocks << order;

    /* Count the suitable free blocks */
    if (order >= suitable_order)
      info->free_blocks_suitable += blocks << (order - suitable_order);
  }
}

unsigned int extfrag_for_order(struct zone *zone, unsigned int order)
{
  struct contig_page_info info;

  fill_contig_page_info(zone, order, &info);
  if (info.free_pages == 0)
    return 0;

  return div_u64((info.free_pages -
                  (info.free_blocks_suitable << order)) *
                     100,
                 info.free_pages);
}

static unsigned int fragmentation_score_zone(struct zone *zone)
{
  return extfrag_for_order(zone, COMPACTION_HPAGE_ORDER);
}

static unsigned int fragmentation_score_zone_weighted(struct zone *zone)
{
  unsigned long score;

  score = zone->present_pages * fragmentation_score_zone(zone);
  return div64_ul(score, zone->zone_pgdat->node_present_pages + 1);
}

static unsigned int fragmentation_score_node(pg_data_t *pgdat)
{
  unsigned int score = 0;
  int zoneid;

  for (zoneid = 0; zoneid < MAX_NR_ZONES; zoneid++)
  {
    struct zone *zone;

    zone = &pgdat->node_zones[zoneid];
    if (!populated_zone(zone))
      continue;
    score += fragmentation_score_zone_weighted(zone);
  }

  return score;
}

fragmentation_score_t get_fragmentation_score(void)
{
  // Get the number of nodes in the system
  int i = 0;
  int total_node = nr_node_ids;
  fragmentation_score_t score;
  score.total_node = total_node;
  // Get the Fragmentation Score of each node
  while (i < total_node)
  {
    pg_data_t *pgdat = NODE_DATA(i);
    score.score[i] = fragmentation_score_node(pgdat);
    score.node[i] = i;
    i++;
  }
  return score;
}

void score_printer(void)
{
  fragmentation_score_t score;
  int i = 0;
  // unsigned long start_time = jiffies;

  // Print the Fragmentation Score of the system every 5000ms
  while (1)
  {
    msleep_interruptible(500);
    score = get_fragmentation_score();
    i = 0;
    while (i < score.total_node)
    {
      printk(KERN_INFO "STATUS - Fragmentation Score: %d Node : %d in Kernel", score.score[i], score.node[i]);
      i++;
    }
  }
}

int alloc_mem(void)
{
	struct sysinfo si;
	long pages=0;
	while(1)
	{
		struct page *page = alloc_pages_node(0, GFP_USER | __GFP_MOVABLE | __GFP_NOWARN, order);
		pages += 1<<order;
		si_meminfo(&si);
		list_add_tail(&page->lru, &alloc);
		
		if(si.freeram*100/si.totalram <76)
		{
			pr_info("alloc %ld pages",pages);
			break;
		}
	}
	return 0;
}
int create_fragments(void *arg)
{
  fragmentation_score_t score;
  struct sysinfo si;
  struct page *page;
  int next;
  int i;
  int count;
  long nr_while=0;

  // set the start time
  unsigned long start_time = jiffies;

  i = 0;
  count = 0;
  score = get_fragmentation_score();
  nr_allocated_pages=0;
  while (i < score.total_node)
  {
    printk(KERN_INFO "Initial Fragmentation Score: %d Node : %d in Kernel", score.score[i], score.node[i]);
    i++;
  }

  // Allocate pages and split them
  // Maximum allocation would be 80% of the total memory
  while (1)
  {
    // Allocate pages
    // It would act as an userspace application
    // For Memory Compaction.

    page = alloc_pages_node(0, GFP_USER |__GFP_MOVABLE | __GFP_NOWARN, order);
    if (!page)
    {
      printk(KERN_INFO "Failed to allocate pages\n");
      count++;
      if (count > 10)
      {
        printk(KERN_INFO "Failed to allocate pages 10 times, so stop the Fragmenter\n");
        return 0;
      }
      continue;
    } 
//    SetPageLRU(page); 
    count = 0;
    nr_allocated_pages+= 1<<order;


    for (int i=0; i < (1<<order)*PAGE_SIZE;i+=PAGE_SIZE)
    {
	sprintf(page_to_virt(page)+i,"alloc_pages %ld", i / PAGE_SIZE);
	memset(page_to_virt(page)+i+strlen(page_to_virt(page)+i) +1,'0', PAGE_SIZE - strlen(page_to_virt(page)+i)-1);
    }


    // check the memory usage
    // if the memory usage is over 80%, then stop the fragmenter
    si_meminfo(&si);
    if (si.freeram * 100 / si.totalram < 20)
    {
      printk(KERN_INFO "Memory usage is over 80%%, so stop the Fragmenter\n");
      return 0;
    }



    split_page(page, order);
 
    for (next = 0 ; next < (1 << order); next++)
    {	


//	pr_info("ref count %d",page_count(page+next));
//	list_add(&(page+next)->lru, &fragment_list);
	if(page_count(page+next)==2)
		page_ref_sub(page+next,1);
//	else
//		pr_info("ref count %d",page_count(page+next));
	if(!((next/4)%2)){
		nr_while++;
//		SetPageLRU(page+next);
//		add_to_page_cache_lru(page+next, page_mapping(page+next),next,GFP_HIGHUSER_MOVABLE);

		folio_add_lru(page_folio((page+next)));
		folio_mark_accessed(page_folio((page+next)));
//		void *addr = kmalloc(PAGE_SIZE, GFP_USER | __GFP_MOVABLE);
		page_list *pg = kmalloc(sizeof(page_list),GFP_USER | __GFP_MOVABLE);
		pg->page = (page+next);
		list_add_tail(&pg->lru,&fragment_list);

		continue;
	}

//     	list_del(&(page+next)->lru);

     	__free_page(page + next);
      	nr_allocated_pages-=1;
    }


    // Check the Fragmentation Score of the system
    // get_fragmentation_score() is run every 500ms from the start time
    // If the Fragmentation Score is higher than the Fragmentation Score parameter,
    // then stop the fragmenter
    if (fragmentation_score > 0 && fragmentation_score < 100)
    {
      if ((jiffies - start_time) * 1000 / HZ >= 500)
      {
        score = get_fragmentation_score();

        i = 0;
        while (i < score.total_node)
        {
          if (score.score[i] >= fragmentation_score)
          {
            printk(KERN_INFO "Fragmentation Score:%d - larger than %d, so stop the Fragmenter\n",score.score[i], fragmentation_score);
	    printk(KERN_INFO "Allocating %ld pages, nr_while : %ld size of page %zu size of page_list %zu\n", nr_allocated_pages,nr_while, sizeof(struct page), sizeof(page_list));
            return 0;
          }
          i++;
        }
        start_time = jiffies;
      }
    }
  }
  return 0;
}

int fragmenter_init(void)
{
  if (order < 0 || order > 11)
  {
    printk(KERN_INFO "Invalid order value\n");
    return -1;
  }
  if (fragmentation_score < 0 || fragmentation_score > 100)
  {
    printk(KERN_INFO "Invalid Fragmentation Score value\n");
    return -1;
  }
  if (order >= 0)
  {

    printk(KERN_INFO "Starting fragmenter\n");
	alloc_mem();
    create_fragments(NULL);
  }
  else
  {
    score_printer();
  }
  return 0;
}

int release_fragments(void)
{
  page_list *page, *next;
//  struct page *page, *next;
  long nr=0;
  list_for_each_entry_safe(page, next, &fragment_list, lru)
  {
	set_page_count(page->page,2);
	if(page_count(page->page) != 2) 
		pr_info("ref count %d",page_count(page->page));
	
	list_del(&page_folio(page->page)->lru);
//	folio_put(page_folio(page->page));
//	clear_page_dirty_for_io(page);
//	end_page_writeback(page);
   list_del(&page->lru);
    __free_page(page->page);
    nr++;
   kfree(page);
  }
  pr_info("FREE %ld pages\n",nr);
  return 0;
}

int release_alloc(void)
{
	struct page *page, *next;
	list_for_each_entry_safe(page, next, &alloc, lru)
	{
		list_del(&page->lru);
		__free_page(page);
	}
	return 0;
}

void fragmenter_exit(void)
{
  printk(KERN_INFO "Releasing all fragments\n");
	release_alloc();
  release_fragments();
  printk(KERN_INFO "Released all fragments\n");
}

module_init(fragmenter_init);
module_exit(fragmenter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
    "Physical Memory Fragmenter, exhausts contiguous physical memory (from a particular order)");
MODULE_AUTHOR("Jongho Baik");
