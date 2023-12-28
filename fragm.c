#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_ORDER 11
#define PAGE_SIZE 4096
#define SZ_GB (1UL << 30)

#define min(a, b)               \
	({                          \
		__typeof__(a) _a = (a); \
		__typeof__(b) _b = (b); \
		_a < _b ? _a : _b;      \
	})

static int print_buddyinfo(void)
{
	char buf[4 * PAGE_SIZE] = {0};
	int ret, off, fd, i;
	ssize_t len;
	unsigned long nr[MAX_ORDER] = {0};
	unsigned long total = 0, cumulative = 0;

	fd = open("/proc/buddyinfo", O_RDONLY);
	if (fd < 0)
	{
		perror("open");
		return -1;
	}

	len = read(fd, buf, sizeof(buf));
	if (len <= 0)
	{
		perror("read");
		close(fd);
		return -1;
	}

	off = 0;
	while (off < len)
	{
		int node;
		char __node[64], __zone[64], zone[64];
		unsigned long n[MAX_ORDER];
		int parsed;

		ret = sscanf(buf + off, "%s %d, %s %s %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n%n",
					 __node, &node, __zone, zone, &n[0], &n[1], &n[2], &n[3], &n[4], &n[5], &n[6],
					 &n[7], &n[8], &n[9], &n[10], &parsed);
		// printf("%d %s %lu %lu %lu\n", node, zone, n[0], n[1], n[10]);
		if (ret < 15)
			break;

		off += parsed;

		for (i = 0; i < MAX_ORDER; i++)
			nr[i] += n[i];
	}

	for (i = 0; i < MAX_ORDER; i++)
		total += (PAGE_SIZE << i) * nr[i];

	printf("%-4s%10s%10s%10s%10s\n", "Order", "Pages", "Total", "%Free", "%Higher");
	for (i = 0; i < MAX_ORDER; i++)
	{
		unsigned long bytes = (PAGE_SIZE << i) * nr[i];

		cumulative += bytes;

		printf("%-4d %10lu %7.2lfGB %8.1lf%% %8.1lf%%\n", i, nr[i],
			   (double)bytes / SZ_GB,
			   (double)bytes / total * 100,
			   (double)(total - cumulative) / total * 100);
	}

	close(fd);
	return 0;
}

// Caculate Compaction Score

static int fragment_memory(int order, int compaction_score, int report)
{
	struct sysinfo info;
	size_t off = 0, size, mmap_size, i;
	char *area;
	int ret = 0;

	ret = sysinfo(&info);
	if (ret)
	{
		perror("sysinfo");
		return -1;
	}

	print_buddyinfo();
FRAG:
	printf("Before : Total %.1lf GB, Free %.1lf GB\n", (double)info.totalram / SZ_GB,
		   (double)info.freeram / SZ_GB);

	// 10GB allocation per 24GB TOTAL RAM Size
	mmap_size = (size_t)info.totalram * 10 / 24;

	// mmap size must be multiple of 2^n
	// it must be page aligned
	mmap_size = (mmap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

	area = mmap(NULL, mmap_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	printf("mmap_size = %.lf\n", mmap_size / SZ_GB);
	printf("After allocation : Total %.1lf GB, Free %.1lf GB\n", (double)info.totalram / SZ_GB,
		   (double)info.freeram / SZ_GB);
	printf("Fragmenting memory...\n");

	if (area == MAP_FAILED)
	{
		perror("mmap");
		return -1;
	}

	off = 0;
	while (1)
	{
		ret = sysinfo(&info);
		if (ret)
		{
			perror("sysinfo");
			break;
		}

		if (info.freeram <= mmap_size)
		{
			printf("Not enough free memory\n");
			munmap(area, mmap_size);
			goto FRAG;
		}

		size = (size_t)1 << order;

		// 1. Populate the memory
		for (i = off; i < min(off + size, mmap_size); i += PAGE_SIZE)
		{
			*(volatile char *)(area + i) = 0;
		}

		// 2. Fragment the memory with 2^order size
		// Free 1~2^order-1 pages from 0~2^order-1 pages
		for (i = off + PAGE_SIZE; i < min(off + size, mmap_size); i += PAGE_SIZE << order)
		{
			ret = madvise(area + i, (PAGE_SIZE << order) - PAGE_SIZE, MADV_DONTNEED);
			if (ret)
			{
				perror("madvise");
				return -1;
			}
			printf("Free %u bytes", (PAGE_SIZE << order) - PAGE_SIZE);
			printf("Fragmenting....Free Ram = %lu\n", info.freeram);
		}

		off += size;
		if (off >= mmap_size)
		{
			munmap(area, mmap_size);
			goto FRAG;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	int compaction_score = 0, report = 0, order;

	if (argc < 2)
		goto bad_args;

	if (strcmp(argv[1], "-s") == 0)
	{
		print_buddyinfo();
		return EXIT_SUCCESS;
	}
	else if (strcmp(argv[1], "-f") == 0)
	{
		switch (argc)
		{
		case 7:
			report = atoi(argv[6]);
			if (report < 0 || report > 1)
			{
				fprintf(stderr, "Report must be 0 or 1\n");
				return EXIT_FAILURE;
			}
		case 5:
			compaction_score = atoi(argv[4]);
			if (compaction_score < 0 || compaction_score > 100)
			{
				fprintf(stderr, "Compaction score must be in [0; 100] range\n");
				return EXIT_FAILURE;
			}
		case 3:
			order = atoi(argv[2]);
			if (order < 1 || order > 20)
			{
				fprintf(stderr, "Order must be in [1; 20] range\n");
				return EXIT_FAILURE;
			}
			break;
		default:
			goto bad_args;
		}

		fragment_memory(order, compaction_score, report);
	}
	else
	{
		goto bad_args;
	}

	return EXIT_SUCCESS;

bad_args:
	fprintf(stderr,
			"Usage:\n"
			"    Fragment memory: -f <order> -s <compaction_score threshold> -r <report:0 or 1>\n"
			"    Show stats:      -s\n");
	return EXIT_FAILURE;
}
