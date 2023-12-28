// This code is from saravan2/phymem_fragmenter
// Modified by Jongho Baik

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmstat.h>
#include <linux/mmzone.h>

#define COMPACTION_HPAGE_ORDER 9
// #define MAX_ORDER 10

static int order;
static int compaction_score;
module_param(order, int, 0);
MODULE_PARM_DESC(order, "Order of the page to allocate and fragment");

module_param(compaction_score, int, 0);
MODULE_PARM_DESC(compaction_score, "Compaction score to stop the Fragmenter");

LIST_HEAD(fragment_list);

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
} compaction_score_t;

// Caculate the Compaction Score of the system
// Compaction Score is used by Proactive Compaction
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

compaction_score_t get_compaction_score(void)
{
  // Get the number of nodes in the system
  int i = 0;
  int total_node = nr_node_ids;
  compaction_score_t score;
  score.total_node = total_node;
  // Get the compaction score of each node
  while (i < total_node)
  {
    pg_data_t *pgdat = NODE_DATA(i);
    score.score[i] = fragmentation_score_node(pgdat);
    score.node[i] = i;
    i++;
  }
  return score;
}

int create_fragments(void)
{
  struct page *page;
  int next;
  int i;
  int chk = 0;
  compaction_score_t score;
  while ((page = alloc_pages(GFP_KERNEL, order)))
  {
    split_page(page, order);
    list_add(&page->lru, &fragment_list);
    for (next = 1; next < (1 << order); next++)
      __free_page(page + next);
    if (compaction_score > 0 && compaction_score < 100)
    {
      score = get_compaction_score();
      i = 0;
      while (i < score.total_node)
      {
        if (score.score[i] >= compaction_score)
        {
          printk(KERN_INFO "Compaction Score: %d Node : %d in Kernel", score.score[i], score.node[i]);
          printk(KERN_INFO "Compaction Score is larger than %d, so stop the Fragmenter\n", compaction_score);
          chk = 1;
          break;
        }
      }
    }
    if (chk == 1)
      break;
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
  if (compaction_score < 0 || compaction_score > 100)
  {
    printk(KERN_INFO "Invalid compaction score value\n");
    return -1;
  }
  printk(KERN_INFO "Starting fragmenter\n");
  create_fragments();
  return 0;
}

int release_fragments(void)
{
  struct page *page, *next;
  list_for_each_entry_safe(page, next, &fragment_list, lru)
  {
    __free_page(page);
  }
  return 0;
}

void fragmenter_exit(void)
{
  printk(KERN_INFO "Releasing all fragments\n");
  release_fragments();
  printk(KERN_INFO "Released all fragments\n");
}

module_init(fragmenter_init);
module_exit(fragmenter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
    "Physical Memory Fragmenter, exhausts contiguous physical memory (from a particular order)");
MODULE_AUTHOR("Jongho Baik");
