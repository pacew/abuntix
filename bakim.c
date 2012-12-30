#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <errno.h>

#define EXT2_SECRM_FL			0x00000001 /* Secure deletion */
#define EXT2_UNRM_FL			0x00000002 /* Undelete */
#define EXT2_COMPR_FL			0x00000004 /* Compress file */
#define EXT2_SYNC_FL			0x00000008 /* Synchronous updates */
#define EXT2_IMMUTABLE_FL		0x00000010 /* Immutable file */
#define EXT2_APPEND_FL			0x00000020 /* writes to file may only append */
#define EXT2_NODUMP_FL			0x00000040 /* do not dump file */
#define EXT2_NOATIME_FL			0x00000080 /* do not update atime */
/* Reserved for compression usage... */
#define EXT2_DIRTY_FL			0x00000100
#define EXT2_COMPRBLK_FL		0x00000200 /* One or more compressed clusters */
#define EXT2_NOCOMPR_FL			0x00000400 /* Access raw compressed data */
#define EXT2_ECOMPR_FL			0x00000800 /* Compression error */
/* End compression flags --- maybe not all used */
#define EXT2_BTREE_FL			0x00001000 /* btree format dir */
#define EXT2_INDEX_FL			0x00001000 /* hash-indexed directory */
#define EXT2_IMAGIC_FL			0x00002000
#define EXT3_JOURNAL_DATA_FL		0x00004000 /* file data should be journaled */
#define EXT2_NOTAIL_FL			0x00008000 /* file tail should not be merged */
#define EXT2_DIRSYNC_FL 		0x00010000 /* Synchronous directory modifications */
#define EXT2_TOPDIR_FL			0x00020000 /* Top of directory hierarchies*/
#define EXT4_HUGE_FILE_FL               0x00040000 /* Set to each huge file */
#define EXT4_EXTENTS_FL 		0x00080000 /* Inode uses extents */
#define EXT4_EA_INODE_FL	        0x00200000 /* Inode used for large EA */
#define EXT4_EOFBLOCKS_FL		0x00400000 /* Blocks allocated beyond EOF */
#define EXT4_SNAPFILE_FL		0x01000000  /* Inode is a snapshot */
#define EXT4_SNAPFILE_DELETED_FL	0x04000000  /* Snapshot is being deleted */
#define EXT4_SNAPFILE_SHRUNK_FL		0x08000000  /* Snapshot shrink has completed */
#define EXT2_RESERVED_FL		0x80000000 /* reserved for ext2 lib */

#define EXT2_FL_USER_VISIBLE		0x004BDFFF /* User visible flags */
#define EXT2_FL_USER_MODIFIABLE		0x004B80FF /* User modifiable flags */

void
usage (void)
{
	printf ("usage: bakim [FILE]...\n");
}

struct flags_name {
	unsigned long	flag;
	const char	*short_name;
	const char	*long_name;
};


static struct flags_name flags_array[] = {
	{ EXT2_SECRM_FL, "s", "Secure_Deletion" },
	{ EXT2_UNRM_FL, "u" , "Undelete" },
	{ EXT2_SYNC_FL, "S", "Synchronous_Updates" },
	{ EXT2_DIRSYNC_FL, "D", "Synchronous_Directory_Updates" },
	{ EXT2_IMMUTABLE_FL, "i", "Immutable" },
	{ EXT2_APPEND_FL, "a", "Append_Only" },
	{ EXT2_NODUMP_FL, "d", "No_Dump" },
	{ EXT2_NOATIME_FL, "A", "No_Atime" },
	{ EXT2_COMPR_FL, "c", "Compression_Requested" },
#ifdef ENABLE_COMPRESSION
	{ EXT2_COMPRBLK_FL, "B", "Compressed_File" },
	{ EXT2_DIRTY_FL, "Z", "Compressed_Dirty_File" },
	{ EXT2_NOCOMPR_FL, "X", "Compression_Raw_Access" },
	{ EXT2_ECOMPR_FL, "E", "Compression_Error" },
#endif
	{ EXT3_JOURNAL_DATA_FL, "j", "Journaled_Data" },
	{ EXT2_INDEX_FL, "I", "Indexed_directory" },
	{ EXT2_NOTAIL_FL, "t", "No_Tailmerging" },
	{ EXT2_TOPDIR_FL, "T", "Top_of_Directory_Hierarchies" },
	{ EXT4_EXTENTS_FL, "e", "Extents" },
	{ EXT4_HUGE_FILE_FL, "h", "Huge_file" },
	{ 0, NULL, NULL }
};

void print_flags (FILE * f, unsigned long flags, unsigned options)
{
	struct flags_name *fp;

	for (fp = flags_array; fp->flag != 0; fp++) {
		if (flags & fp->flag) {
			fputs(fp->short_name, f);
		} else {
			fputs("-", f);
		}
	}
}

int fsetflags (const char * name, unsigned long flags)
{
	int fd, r, f, save_errno = 0;
	struct stat buf;

	if (!lstat(name, &buf) &&
	    !S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode)) {
		goto notsupp;
	}

	fd = open (name, O_RDONLY);
	if (fd == -1)
		return -1;
	f = (int) flags;
	r = ioctl (fd, _IOW('f', 2, long), &f);
	if (r == -1)
		save_errno = errno;
	close (fd);
	if (save_errno)
		errno = save_errno;
	return r;

notsupp:
	return -1;
}

int fgetflags (const char * name, unsigned long * flags)
{
	struct stat buf;

	int fd, r, f;

	if (!lstat(name, &buf) &&
	    !S_ISREG(buf.st_mode) && !S_ISDIR(buf.st_mode)) {
		goto notsupp;
	}
	fd = open (name, O_RDONLY);
	if (fd == -1)
		return -1;
	r = ioctl (fd, _IOR ('f', 1, long), &f);
	*flags = f;
	close (fd);
	return r;

notsupp:
	return -1;
}

static int set_immutable (const char * name)
{
	unsigned long flags;

	if (fgetflags(name, &flags) == -1) {
		printf ("%d\n", __LINE__);
		return -1;
	}

	flags |= EXT2_IMMUTABLE_FL;

	if (fsetflags(name, flags) == -1) {
		printf ("failed to set flags\n");
		return -1;
	}
	return 0;
}

int
main (int argc, char **argv)
{
	int c, idx;

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind < argc) {
		for (idx = optind; idx < argc; idx++) {
			set_immutable (argv[idx]);
		}
	} else {
		usage ();
	}


	return (0);
}
