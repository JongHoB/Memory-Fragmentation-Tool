// This code is from saravan2/phymem_fragmenter
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

#if defined CONFIG_TRANSPARENT_HUGEPAGE
#define COMPACTION_HPAGE_ORDER HPAGE_PMD_ORDER
#elif defined CONFIG_HUGETLBFS
#define COMPACTION_HPAGE_ORDER HUGETLB_PAGE_ORDER
#else
#define COMPACTION_HPAGE_ORDER (PMD_SHIFT - PAGE_SHIFT)
#endif

#ifndef CONFIG_FORCE_MAX_ZONEORDER
#define MAX_ORDER 11
#else
#define MAX_ORDER CONFIG_FORCE_MAX_ZONEORDER
#endif

static struct task_struct *task = NULL;

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

  for (order = 0; order < MAX_ORDER; order++)
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

int score_printer(void *arg)
{
  compaction_score_t score;
  int i = 0;
  // unsigned long start_time = jiffies;

  // Print the compaction score of the system every 5000ms
  allow_signal(SIGUSR1);
  while (1)
  {
    msleep_interruptible(500);
    if (kthread_should_stop())
    {
      break;
    }
    score = get_compaction_score();
    i = 0;
    while (i < score.total_node)
    {
      printk(KERN_INFO "STATUS - Compaction Score: %d Node : %d in Kernel", score.score[i], score.node[i]);
      i++;
    }
  }
  return 0;
}

int fragmenter_init(void)
{

  task = kthread_run(score_printer, NULL, "score_printer kthread run");

  return 0;
}

void fragmenter_exit(void)
{
  printk(KERN_INFO "Releasing score_printer()\n");
  if (task)
  {
    send_sig(SIGUSR1, task, 0);
    kthread_stop(task);
  }
  printk(KERN_INFO "Released score_printer()\n");
}
2 module_init(fragmenter_init);
module_exit(fragmenter_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(
    "Physical Memory Fragmenter Score Printer, exhausts contiguous physical memory (from a particular order)");
MODULE_AUTHOR("Jongho Baik");
