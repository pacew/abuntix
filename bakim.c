#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>

#define JOURNAL_DATA_FL 1 << 14
#define NOATIME_FL 1 << 7
#define NODUMP_FL 1 << 6
#define APPEND_FL 1 << 5
#define IMMUTABLE_FL 1 << 4
#define SYNC_FL 1 << 3
#define COMPR_FL 1 << 2
#define UNRM_FL 1 << 1
#define SECRM_FL 1 << 0
#define NOTAIL_FL 1 << 15

void
print_flags (unsigned long num)
{
	char buf[100];
	unsigned long n;
	int idx;

	n = num;

	for (idx = 0; idx <= 15; idx++) {
		buf[idx] = (n & 1) ? '1' : '0';

		n = n >> 1;
	}

	buf[idx] = 0;

	printf ("%s\n", buf);
}

int
main (int argc, char **argv)
{
	struct stat stat_buf;
	int fd, f;
	unsigned long flags;

	if (lstat ("a", &stat_buf) == -1)
		exit (1);

	fd = open ("a", O_RDONLY);
	if (fd == -1)
		return (-1);

	ioctl (fd, _IOR ('f', 1, long), &f);

	flags = f;
	close (fd);

	print_flags (flags);

	printf ("%s\n", (flags & JOURNAL_DATA_FL) ? "journal" : "not journal");
	printf ("%s\n", (flags & NOATIME_FL) ? "noatime" : "not noatime");
	printf ("%s\n", (flags & NODUMP_FL) ? "nodump" : "not nodump");
	printf ("%s\n", (flags & APPEND_FL) ? "append" : "not append");
	printf ("%s\n", (flags & IMMUTABLE_FL) ? "immutable" : "not immutable");
	printf ("%s\n", (flags & SYNC_FL) ? "sync" : "not sync");
	printf ("%s\n", (flags & COMPR_FL) ? "compr" : "not compr");
	printf ("%s\n", (flags & UNRM_FL) ? "unrm" : "not unrm");
	printf ("%s\n", (flags & SECRM_FL) ? "secrm" : "not secrm");
	printf ("%s\n", (flags & NOTAIL_FL) ? "notail" : "not notail");

	return (0);
}
