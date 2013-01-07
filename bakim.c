#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <ftw.h>
#include <utime.h>
#include <time.h>

#define EXT2_IMMUTABLE_FL 0x00000010
#define BACKUP_ROOT "/big"

#define MAX_DIRS_OPEN 100

#define FILE_FOUND 0x00000001

char *backup_root = BACKUP_ROOT;
char *backup_directory, *newest, backup_branch[100];

struct dir_data {
	struct dir_data *next;
	char *path;
	time_t atime, mtime;
	int mode;
};

struct dir_data *first_dir, *first_collision_dir, *last_collision_dir;

int base_off;

void usage (void);
void *xcalloc (unsigned int a, unsigned int b);
char *xstrdup (const char *old);
int fsetflags (const char * name, unsigned long flags);
int fgetflags (const char * name, unsigned long * flags);
static int set_immutable (const char *fn);
void touched_dir (char *path, const struct stat *sb);
static int delete_subtree (const char *path, const struct stat *sb, int tflag,
			   struct FTW *ftwbuf);
void delete_file_or_dir (char *path);
void copy_file (const char *src_fn, char *dst_fn);
void base26 (int c, char *s);
struct dir_data *find_dir (const char *path);
int pave_path (const char *path, struct dir_data *dp);
struct dir_data *find_slot (const char *fpath, const struct stat *sb,
			    int *flags);
int check_same (const struct stat *a, const struct stat *b, const char *a_path,
		const char *b_path);
int backup_file (const char *fpath, const struct stat *sb, struct FTW *ftwbuf,
		 char *backup_path);
int backup_dir (const char *fpath, const struct stat *sb, struct FTW *ftwbuf,
		char *backup_path);
int backup_link (const char *fpath, const struct stat *sb, struct FTW *ftwbuf,
		 char *backup_path);
static int mk_backup (const char *fpath, const struct stat *sb, int tflag,
		      struct FTW *ftwbuf);
void fix_dirs (void);

void
usage (void)
{
	printf ("usage: bakim [FILE]...\n");
	exit (1);
}

void *
xcalloc (unsigned int a, unsigned int b)
{
	void *p;

	if ((p = calloc (a, b)) == NULL) {
		fprintf (stderr, "memory error\n");
		exit (1);
	}

	return (p);
}

char *
xstrdup (const char *old)
{
	char *new;

	if ((new = strdup (old)) == NULL) {
		fprintf (stderr, "out of memory\n");
		exit (1);
	}

	return (new);
}

int
fsetflags (const char * name, unsigned long flags)
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

int
fgetflags (const char * name, unsigned long * flags)
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

static int
set_immutable (const char *fn)
{
	unsigned long flags;

	if (fgetflags(fn, &flags) == -1) {
		printf ("failed to get flags for %s\n", fn);
		return -1;
	}

	flags |= EXT2_IMMUTABLE_FL;

	if (fsetflags(fn, flags) == -1) {
		printf ("failed to set flags for %s\n", fn);
		return -1;
	}

	return (0);
}

void
touched_dir (char *path, const struct stat *sb)
{
	struct dir_data *dp;

	dp = xcalloc (1, sizeof *dp);

	dp->path = xstrdup (path);
	dp->atime = sb->st_atime;
	dp->mtime = sb->st_mtime;
	dp->mode = sb->st_mode;

	if (!first_dir) {
		first_dir = dp;
	} else {
		dp->next = first_dir;
		first_dir = dp;
	}
}

static int
delete_subtree (const char *path, const struct stat *sb,
		int tflag, struct FTW *ftwbuf)
{
	if (remove (path) == -1) {
		fprintf (stderr, "failed to remove old entry %s: %m\n",
			 path);
		exit (1);
	}

	return (0);
}

void
delete_file_or_dir (char *path)
{
	int flags;
	struct stat sb;

	if (lstat (path, &sb) == -1) {
		if (errno == ENOENT) {
			return;
		} else {
			fprintf (stderr, "error with lstat on %s: %m\n", path);
			exit (1);
		}
	}


	flags = FTW_PHYS | FTW_DEPTH;

	if (nftw (path, delete_subtree, MAX_DIRS_OPEN, flags) == -1) {
		fprintf (stderr, "deletion nftw failed\n");
		return;
	}
}

void
copy_file (const char *src_fn, char *dst_fn)
{
	int n_read;
	FILE *src, *dst;
	char buf[1024*1024];

	if ((src = fopen (src_fn, "r")) == NULL) {
		printf ("cannot open src file %s\n", src_fn);
		exit (1);

	}

	if ((dst = fopen (dst_fn, "w")) == NULL) {
		fprintf (stderr, "cannot open dst file %s\n", dst_fn);
		exit (1);
	}

	while ((n_read = fread (buf, 1, sizeof buf, src)) > 0) {
		if (fwrite (buf, 1, n_read, dst) != n_read) {
			fprintf (stderr, "error copying file:"
				 " potentially out of space\n");
			exit (1);
		}
	}

	if (fclose (src) != 0) {
		fprintf (stderr, "error closing file %s: %m", src_fn);
		exit (1);
	}
	if (fclose (dst) != 0) {
		fprintf (stderr, "error closing file %s: %m", dst_fn);
		exit (1);
	}
}

void
base26 (int c, char *s)
{
	s[0] = (int) ((c % (26 * 26)) / 26) + 'a';
	s[1] = (c % 26) + 'a';
	s[2] = 0;
}

struct dir_data *
find_dir (const char *path)
{
	struct dir_data *dp;

	for (dp = first_dir; dp; dp = dp->next) {
		if (strcmp (path, dp->path) == 0) {
			return (dp);
		}
	}

	fprintf (stderr, "touched directories path corrupted, unable to find"
		 " %s. exiting\n", path);
	exit (1);
}

int
pave_path (const char *path, struct dir_data *dp)
{
	char *s, *p, new[PATH_MAX], dir_name[PATH_MAX], orig_dir[PATH_MAX];
	struct stat sb;
	struct dir_data *dir;

	s = xstrdup (path);
	p = s;

	while (*p == '/')
		p++;

	while (1) {
		while (*p && *p != '/')
			p++;

		if (*p == 0)
			break;

		*p = 0;

		if (strlen (dp->path) + strlen (s) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dir_name, "%s/%s", dp->path, s);

		if (lstat (dir_name, &sb) == -1) {
			if (errno != ENOENT) {
				fprintf (stderr, "error with lstat on %s: %m\n",
					 new);
				exit (1);
			}

			if (strlen (backup_directory) + strlen (s)
			    + 100 >= PATH_MAX) {
				fprintf (stderr, "path exceeds PATH_MAX\n");
				exit (1);
			}

			sprintf (orig_dir, "%s/%s", backup_directory, s);
			dir = find_dir (orig_dir);

			if (mkdir (dir_name, dir->mode) == -1) {
				fprintf (stderr, "failed to create"
					 " directory %s: %m\n", dir_name);
				exit (1);
			}
		} else {
			if (!S_ISDIR (sb.st_mode)) {
				free (s);
				return (-1);
			}
		}

		*p = '/';

		while (*p == '/')
			p++;
	}

	free (s);

	return (0);
}

struct dir_data *
find_slot (const char *fpath, const struct stat *sb, int *flags)
{
	int count;
	struct dir_data *dp;
	char dst_name[PATH_MAX], suffix[3];
	const char *path;
	struct stat dst_sb;

	path = fpath + base_off;
	count = 0;
	*flags = 0;

	for (dp = first_collision_dir; dp; dp = dp->next) {
		if (strlen (dp->path) + strlen (path) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}
		sprintf (dst_name, "%s/%s", dp->path, path);

		if (lstat (dst_name, &dst_sb) == -1) {
			if (errno == ENOENT) {
				if (pave_path (path, dp) != -1)
					return (dp);
			} else if (errno != ENOTDIR) {
				fprintf (stderr, "error with lstat on %s: %m\n",
 					 dst_name);
				exit (1);
			}
		} else {
			if (check_same (sb, &dst_sb, fpath, dst_name)) {
				*flags |= FILE_FOUND;
				return (dp);
			}
		}

		count++;
	}

	for (; count <= 675; count++) {
		dp = xcalloc (1, sizeof *dp);
		dp->path = xcalloc (1, strlen (backup_directory) + 10);

		base26 (count, suffix);
		sprintf (dp->path, "%s-%s", backup_directory, suffix);

		if (!first_collision_dir)
			first_collision_dir = dp;

		if (last_collision_dir)
			last_collision_dir->next = dp;

		last_collision_dir = dp;

		if (lstat (dp->path, &dst_sb) == -1) {
			if (errno != ENOENT) {
				fprintf (stderr, "error with lstat on %s: %m\n",
					 dp->path);
				exit (1);
			}

			if (mkdir (dp->path, 0755) == -1) {
				fprintf (stderr, "failed to create directory %s: %m\n",
					 dp->path);
				exit (1);
			}

			if (pave_path (path, dp) != -1)
				return (dp);
		}


		if (strlen (dp->path) + strlen (path) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dst_name, "%s/%s", dp->path, path);
		if (lstat (dst_name, &dst_sb) == -1) {
			if (errno == ENOENT) {
				if (pave_path (path, dp) != -1)
					return (dp);
			} else if (errno == ENOTDIR) {
				continue;
			} else {
				fprintf (stderr, "error with lstat on %s: %m\n",
 					 dst_name);
				exit (1);
			}
		} else {
			if (check_same (sb, &dst_sb, fpath, dst_name)) {
				*flags |= FILE_FOUND;
				return (dp);
			}
		}
	}

	return (NULL);
}

int
check_same (const struct stat *a, const struct stat *b,
	    const char *a_path, const char *b_path)
{
	int mask, r;
	char a_tar[PATH_MAX], b_tar[PATH_MAX];

	mask = 0777;

	if (S_ISREG (a->st_mode) && S_ISREG (b->st_mode)) {
		if (a->st_mtime == b->st_mtime && a->st_size == b->st_size
		    && (a->st_mode & mask) == (b->st_mode & mask)
		    && a->st_uid == b->st_uid && a->st_gid == b->st_gid) {
			return (1);
		}
	} else if (S_ISDIR (a->st_mode) && S_ISDIR (b->st_mode)) {
		if ((a->st_mode & mask) == (b->st_mode & mask)
		    && a->st_uid == b->st_uid && a->st_gid == b->st_gid) {
			return (1);
		}
	} else if (S_ISLNK (a->st_mode) && S_ISLNK (b->st_mode)) {
		if (!a_path || !b_path) {
			fprintf (stderr, "bad call to check_same,"
				 " paths required for links\n");
			exit (1);
		}

		if (a->st_uid != b->st_uid || a->st_gid != b->st_gid)
			return (0);

		r = readlink (a_path, a_tar, a->st_size + 1);

		if (r < 0) {
			fprintf (stderr, "failed to read link %s: %m\n",
				 a_path);
			exit (1);
		}

		if (r > a->st_size) {
			fprintf (stderr, "symlink increased in size"
				 " between lstat and readlink\n");
			exit (1);
		}

		b_tar[b->st_size] = 0;

		r = readlink (b_path, b_tar, b->st_size + 1);

		if (r < 0) {
			fprintf (stderr, "failed to read link %s: %m\n",
				 b_path);
			exit (1);
		}

		if (r > b->st_size) {
			fprintf (stderr, "symlink increased in size"
				 " between lstat and readlink\n");
			exit (1);
		}

		b_tar[b->st_size] = 0;

		if (strcmp (a_tar, b_tar) == 0)
			return (1);
	}

	return (0);
}

int
backup_file (const char *fpath, const struct stat *sb, struct FTW *ftwbuf,
	     char *backup_path)
{
	const char *path;
	char dst_name[PATH_MAX], newbr_name[PATH_MAX], newbr_tar[PATH_MAX], *p;
	struct stat dst_sb;
	struct utimbuf times;
	int idx, flags;
	struct dir_data *dp;

	path = fpath + base_off;

	if (strlen (path) + strlen (backup_path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	sprintf (dst_name, "%s/%s", backup_path, path);

	if (lstat (dst_name, &dst_sb) == -1) {
		if (errno == ENOTDIR) {
			if ((dp = find_slot (fpath, sb, &flags)) == NULL) {
				fprintf (stderr, "failed to find slot for %s\n",
					 fpath);
				return (-1);
			}

			if (flags & FILE_FOUND)
				return (0);

			if (backup_file (fpath, sb, ftwbuf, dp->path) == -1) {
				fprintf (stderr, "backup failed for %s\n",
					 fpath);
				return (-1);
			} else {
				return (0);
			}
		} else if (errno != ENOENT) {
			fprintf (stderr, "error with lstat on %s: %m\n",
				 dst_name);
			exit (1);
		}
	} else {
		if (check_same (sb, &dst_sb, NULL, NULL)) {
			return (0);
		} else {
			if ((dp = find_slot (fpath, sb, &flags)) == NULL) {
				fprintf (stderr, "failed to find slot for %s\n",
					 fpath);
				return (-1);
			}

			if (flags & FILE_FOUND)
				return (0);

			if (backup_file (fpath, sb, ftwbuf, dp->path)) {
				fprintf (stderr, "backup failed for %s\n",
					fpath);
				return (-1);
			} else {
				return (0);
			}
		}
	}

	if (strlen (newest) + strlen (path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}
	sprintf (newbr_name, "%s/%s", newest, path);

	if (strlen (backup_path) + strlen (path)
	    + strlen ("../") * ftwbuf->level + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	p = newbr_tar;
	for (idx = 0; idx < ftwbuf->level + 1; idx++) {
		strcpy (p, "../");
		p += 3;
	}
	sprintf (p, "%s/%s", backup_path, path);

	copy_file (fpath, dst_name);

	if (chmod (dst_name, sb->st_mode) == -1)
		fprintf (stderr, "failed to set mode on %s: %m\n", dst_name);

	times.actime = sb->st_atime;
	times.modtime = sb->st_mtime;

	if (utime (dst_name, &times) == -1) {
		fprintf (stderr, "failed to set timestamps on %s: %m\n",
			 dst_name);
	}

	if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1)
		fprintf (stderr, "failed to chown %s: %m\n", dst_name);

	set_immutable (dst_name);

	delete_file_or_dir (newbr_name);

	if (symlink (newbr_tar, newbr_name) == -1) {
		fprintf (stderr, "failed to create symlink %s: %m\n",
			 newbr_name);
		return (-1);
	}

	return (0);
}

int
backup_dir (const char *fpath, const struct stat *sb, struct FTW *ftwbuf,
	    char *backup_path)
{
	const char *path;
	char dst_name[PATH_MAX], newbr_name[PATH_MAX], newbr_tar[PATH_MAX], *p;
	struct stat dst_sb;
	int idx, flags;
	struct dir_data *dp;

	path = fpath + base_off;

	if (strlen (path) + strlen (backup_path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	sprintf (dst_name, "%s/%s", backup_path, path);

	if (lstat (dst_name, &dst_sb) == -1) {
		if (errno == ENOTDIR) {
			if ((dp = find_slot (fpath, sb, &flags)) == NULL) {
				fprintf (stderr, "failed to find slot for %s\n",
					 fpath);
				return (-1);
			}

			if (flags & FILE_FOUND)
				return (0);

			if (backup_dir (fpath, sb, ftwbuf, dp->path) == -1) {
				fprintf (stderr, "backup failed for %s\n",
					fpath);
				return (-1);
			} else {
				return (0);
			}
		} else if (errno != ENOENT) {
			fprintf (stderr, "error with lstat on %s: %m\n",
				 dst_name);
			exit (1);
		}
	} else {
		if (check_same (sb, &dst_sb, NULL, NULL)) {
			touched_dir (dst_name, sb);
			return (0);
		} else {
			if ((dp = find_slot (fpath, sb, &flags)) == NULL) {
				fprintf (stderr, "failed to find slot for %s\n",
					 fpath);
				return (-1);
			}

			if (flags & FILE_FOUND)
				return (0);

			if (backup_dir (fpath, sb, ftwbuf, dp->path) == -1) {
				fprintf (stderr, "backup failed for %s\n",
					 fpath);
				return (-1);
			} else {
				return (0);
			}
		}
	}

	if (strlen (newest) + strlen (path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}
	sprintf (newbr_name, "%s/%s", newest, path);

	if (strlen (backup_path) + strlen (path)
	    + strlen ("../") * ftwbuf->level + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	p = newbr_tar;
	for (idx = 0; idx < ftwbuf->level + 1; idx++) {
		strcpy (p, "../");
		p += 3;
	}
	sprintf (p, "%s/%s", backup_path, path);

	if (mkdir (dst_name, sb->st_mode) == -1) {
		fprintf (stderr, "failed to create directory %s: %m\n",
			 dst_name);
		return (-1);
	}

	if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
		fprintf (stderr, "failed to chown %s: %m\n", dst_name);
	}

	if (lstat (dst_name, &dst_sb) == -1) {
		fprintf (stderr, "error with lstat on %s: %m\n", dst_name);
		return (-1);
	}

	touched_dir (dst_name, sb);

	delete_file_or_dir (newbr_name);

	if (mkdir (newbr_name, sb->st_mode) == -1) {
		fprintf (stderr, "failed to create directory %s: %m\n",
			 newbr_name);
		return (-1);
	}

	if (lchown (newbr_name, sb->st_uid, sb->st_gid) == -1) {
		fprintf (stderr, "failed to chown %s: %m\n", newbr_name);
	}

	if (lstat (newbr_name, &dst_sb) == -1) {
		fprintf (stderr, "error with lstat on %s: %m\n", newbr_name);
		return (-1);
	}

	touched_dir (newbr_name, sb);

	return (0);
}

int
backup_link (const char *fpath, const struct stat *sb, struct FTW *ftwbuf,
	     char *backup_path)
{
	const char *path;
	char dst_name[PATH_MAX], newbr_name[PATH_MAX], newbr_tar[PATH_MAX], 
		lnk_tar[PATH_MAX], *p;
	struct stat dst_sb;
	int idx, r, flags;
	struct dir_data *dp;

	path = fpath + base_off;

	if (strlen (path) + strlen (backup_path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	sprintf (dst_name, "%s/%s", backup_path, path);

	if (lstat (dst_name, &dst_sb) == -1) {
		if (errno == ENOTDIR) {
			if ((dp = find_slot (fpath, sb, &flags)) == NULL) {
				fprintf (stderr, "failed to find slot for %s\n",
					 fpath);
				return (-1);
			}

			if (flags & FILE_FOUND)
				return (0);

			if (backup_link (fpath, sb, ftwbuf, dp->path) == -1) {
				fprintf (stderr, "backup failed for %s\n",
					fpath);
				return (-1);
			} else {
				return (0);
			}
		} else if (errno != ENOENT) {
			fprintf (stderr, "error with lstat on %s: %m\n",
				 dst_name);
			exit (1);
		}
	} else {
 		if (check_same (sb, &dst_sb, fpath, dst_name)) {
			return (0);
		} else {
			if ((dp = find_slot (fpath, sb, &flags)) == NULL) {
				fprintf (stderr, "failed to find slot for %s\n",
					 fpath);
				return (-1);
			}

			if (flags & FILE_FOUND)
				return (0);

			if (backup_link (fpath, sb, ftwbuf, dp->path) == -1) {
				fprintf (stderr, "backup failed for %s\n",
					fpath);
				return (-1);
			} else {
				return (0);
			}
		}
	}

	if (strlen (newest) + strlen (path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}
	sprintf (newbr_name, "%s/%s", newest, path);

	if (strlen (backup_path) + strlen (path)
	    + strlen ("../") * ftwbuf->level + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	p = newbr_tar;
	for (idx = 0; idx < ftwbuf->level + 1; idx++) {
		strcpy (p, "../");
		p += 3;
	}
	sprintf (p, "%s/%s", backup_path, path);

	r = readlink (fpath, lnk_tar, sb->st_size + 1);

	if (r < 0) {
		fprintf (stderr, "readlink failed on %s: %m\n", fpath);
		return (-1);
	}

	if (r > sb->st_size) {
		fprintf (stderr,
			 "symlink increased in size, failed to back up\n");
		return (-1);
	}

	lnk_tar[sb->st_size] = 0;

	if (symlink (lnk_tar, dst_name) == -1) {
		fprintf (stderr, "failed to create symlink %s: %m\n", dst_name);
		return (-1);
	}

	if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
		fprintf (stderr, "failed to chown %s: %m\n", dst_name);
	}

	delete_file_or_dir (newbr_name);

	if (symlink (newbr_tar, newbr_name) == -1) {
		fprintf (stderr, "failed to create symlink %s: %m\n",
			 newbr_name);
		return (-1);
	}

	return (0);
}

static int
mk_backup (const char *fpath, const struct stat *sb,
		int tflag, struct FTW *ftwbuf)
{
	if (strncmp (fpath, backup_root, strlen (backup_root)) == 0) {

		return (0);
	}

	switch (tflag) {
	case FTW_F:
		if (backup_file (fpath, sb, ftwbuf, backup_directory) == -1)
			fprintf (stderr, "failed to back up %s\n", fpath);

		break;
	case FTW_D:
		if (backup_dir (fpath, sb, ftwbuf, backup_directory) == -1)
			fprintf (stderr, "failed to back up %s\n", fpath);
		break;
	case FTW_SL:
		if (backup_link (fpath, sb, ftwbuf, backup_directory) == -1)
			fprintf (stderr, "failed to back up %s\n", fpath);

		break;
	default:
		break;
	}

	return (0);
}

void
fix_dirs (void)
{
	struct dir_data *dp, *ndp;
	struct utimbuf times;

	for (dp = first_dir; dp; dp = ndp) {
		ndp = dp->next;

		times.actime = dp->atime;
		times.modtime = dp->mtime;

		if (utime (dp->path, &times) == -1) {
			fprintf (stderr, "failed to set timestamp on %s: %m\n",
				dp->path);
		}

		free (dp->path);
		free (dp);
	}

	first_dir = 0;
}

int
main (int argc, char **argv)
{
	int c, idx, flags, l;
	char *p, *s;
	time_t rawtime;
	struct tm *timeinfo;
	struct dir_data *dp, *ndp;

	while ((c = getopt (argc, argv, "")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	flags = FTW_PHYS;

	if (optind >= argc) {
		usage ();
	}

	l = strlen (backup_root) + strlen ("newest") + 10;
	if ((newest = calloc (1, l)) == NULL) {
		fprintf (stderr, "failed to allocate newest\n");
		return (1);
	}

	sprintf (newest, "%s/%s", backup_root, "newest");

	if (mkdir (newest, 0755) == -1) {
		if (errno != EEXIST) {
			fprintf (stderr,
				 "failed to create directory %s: %m\n",
				 newest);
			return (1);
		}
	}

	umask (0);

	time (&rawtime);
	timeinfo = localtime (&rawtime);
	sprintf (backup_branch, "%04d-%02d-%02d",
		 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
		 timeinfo->tm_mday);

	l = strlen (backup_root) + strlen (backup_branch) + 10;
	if ((backup_directory = calloc (1, l)) == NULL) {
		fprintf (stderr,
			 "failed to allocate backup_directory\n");
		return (1);
	}

	sprintf (backup_directory, "%s/%s", backup_root, backup_branch);

	if (mkdir (backup_directory, 0755) == -1) {
		if (errno != EEXIST) {
			fprintf (stderr,
				 "failed to create directory %s: %m\n",
				 backup_directory);
			return (1);
		}
	}

	if (lchown (backup_directory, 0, 0) == -1) {
		fprintf (stderr, "failed to chown %s: %m\n",
			 backup_directory);
	}

	for (idx = optind; idx < argc; idx++) {
		s = xstrdup (argv[idx]);
		l = strlen (s) - 1;

		while (l > 1 && s[l] == '/')
			s[l--] = 0;

		if ((p = strrchr (s, '/')) != NULL && strlen (p) > 1) {
			base_off = p - s + 1;
		} else {
			base_off = 0;
		}

		if (nftw (argv[idx], mk_backup,
			  MAX_DIRS_OPEN, flags) == -1) {
			fprintf (stderr, "nftw failed\n");
			return (-1);
		}

		if (first_collision_dir) {
			for (dp = first_collision_dir; dp; dp = ndp) {
				ndp = dp->next;

				free (dp->path);
				free (dp);
			}
		}

		first_collision_dir = 0;
		last_collision_dir = 0;

		free (s);

		fix_dirs ();
	}

	return (0);
}
