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
#define BACKUP_DIR "/big"

#define MAX_DIRS_OPEN 100

char *backup_root = BACKUP_DIR;
char *backup_dir, *newest, backup_branch[100];

struct dir_data {
	struct dir_data *next;
	char *path;
	time_t atime, mtime;
	int mode;
};

struct dir_data *first_dir, *first_collision_dir, *last_collision_dir;

int base_off;

void
usage (void)
{
	printf ("usage: bakim [FILE]...\n");
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
xstrdup (char *old)
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

char *
base26 (int c, char *s)
{
	s[0] = (int) ((c % (26 * 26)) / 26) + 'a';
	s[1] = (c % 26) + 'a';
	s[2] = 0;

	return (xstrdup (s));
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
build_path (const char *path, struct dir_data *dp)
{
	const char *s;
	char *p, new[PATH_MAX], dir_name[PATH_MAX], orig_dir[PATH_MAX];
	struct stat sb;
	struct dir_data *dir;

	s = path;

	while ((p = strchr (s, '/')) != NULL) {
		strncpy (new, path, p - path);
		new[p-path] = 0;

		if (strlen (dp->path) + strlen (new) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dir_name, "%s/%s", dp->path, new);

		if (lstat (dir_name, &sb) == -1) {
			if (errno != ENOENT) {
				fprintf (stderr, "error with lstat on %s: %m\n",
					 new);
				exit (1);
			}

			if (strlen (backup_dir) + strlen (new)
			    + 100 >= PATH_MAX) {
				fprintf (stderr, "path exceeds PATH_MAX\n");
				exit (1);
			}

			sprintf (orig_dir, "%s/%s", backup_dir, new);
			dir = find_dir (orig_dir);

			if (mkdir (dir_name, dir->mode) == -1) {
				fprintf (stderr, "failed to create"
					 " directory %s: %m\n", dir_name);
				exit (1);
			}
		} else {
			if (!S_ISDIR (sb.st_mode))
				return (-1);
		}

		while (*p == '/')
			p++;

		s = p;
	}

	return (0);
}

struct dir_data *
find_slot (const char *fpath)
{
	int count;
	struct dir_data *dp;
	char dst_name[PATH_MAX], suffix[3];
	const char *path;
	struct stat dst_sb;

	path = fpath + base_off;
	count = 0;

	for (dp = first_collision_dir; dp; dp = dp->next) {
		if (strlen (dp->path) + strlen (path) + 100 >= PATH_MAX) {
			fprintf (stderr, "path exceeds PATH_MAX\n");
			exit (1);
		}

		sprintf (dst_name, "%s/%s", dp->path, path);

		if (lstat (dst_name, &dst_sb) == -1) {
			if (errno != ENOENT) {
				fprintf (stderr, "error with lstat on %s: %m\n",
					 dst_name);
				exit (1);
			}

			if (build_path (path, dp) != -1)
				return (dp);
		}

		count++;
	}

	while (count <= 675) {
		dp = xcalloc (1, sizeof *dp);
		dp->path = xcalloc (1, strlen (backup_dir) + 10);

		base26 (count, suffix);
		sprintf (dp->path, "%s-%s", backup_dir, suffix);

		if (lstat (dp->path, &dst_sb) == -1) {
			if (errno != ENOENT) {
				fprintf (stderr, "error with lstat on %s: %m\n",
					 dp->path);
				exit (1);
			}

			if (build_path (path, dp) != -1)
				return (dp);
		}

		if (!first_collision_dir)
			first_collision_dir = dp;

		if (last_collision_dir)
			last_collision_dir->next = dp;

		last_collision_dir = dp;

		count++;
	}

	if (count >= 675) {
		fprintf (stderr, "maximum file duplicates on the"
			 " same day exceeded for file %s\n", fpath);
		exit (1);
	}

	if (mkdir (dp->path, 0755) == -1) {
		fprintf (stderr, "failed to create directory %s: %m\n",
			 dp->path);
		exit (1);
	}

	return (dp);
}

void
fallback_backup (const char *fpath, const struct stat *sb,
		 int tflag, struct FTW *ftwbuf, char *newbr_name)
{
	char dst_name[PATH_MAX], *p, newbr_tar[PATH_MAX];
	const char *path;
	int idx;
	struct dir_data *dp;
	struct utimbuf times;

	path = fpath + base_off;

	dp = find_slot (fpath);

	if (strlen (dp->path) + strlen (path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	sprintf (dst_name, "%s/%s", dp->path, path);

	if (strlen (dp->path) + strlen (path)
	    + strlen ("../") * ftwbuf->level + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	p = newbr_tar;
	for (idx = 0; idx < ftwbuf->level + 1; idx++) {
		strcpy (p, "../");
		p += 3;
	}
	sprintf (p, "%s/%s", dp->path + strlen (backup_root) + 1, path);

	copy_file (fpath, dst_name);

	times.actime = sb->st_atime;
	times.modtime = sb->st_mtime;

	if (utime (dst_name, &times) == -1) {
		fprintf (stderr, "failed to set timestamps on %s: %m\n",
			 dst_name);
	}

	if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
		fprintf (stderr, "failed to chown %s: %m\n", dst_name);
	}

	set_immutable (dst_name);

	delete_file_or_dir (newbr_name);

	if (symlink (newbr_tar, newbr_name) == -1) {
		fprintf (stderr, "failed to create symlink %s: %m\n",
			 newbr_name);
	}
}

static int
mk_backup (const char *fpath, const struct stat *sb,
		int tflag, struct FTW *ftwbuf)
{
	char dst_name[PATH_MAX], lnk_tar[PATH_MAX], old_tar[PATH_MAX],
		newbr_name[PATH_MAX], newbr_tar[PATH_MAX], *p;
	const char *path;
	struct utimbuf times;
	int r, exists, idx;
	struct stat dst_sb;

	if (strncmp (fpath, backup_root, strlen (backup_root)) == 0) {
		return (0);
	}

	path = fpath + base_off;

	if (strlen (path) + strlen (backup_dir) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	sprintf (dst_name, "%s/%s", backup_dir, path);

	if (lstat (dst_name, &dst_sb) == -1) {
		if (errno == ENOENT) {
			exists = 0;
		} else {
			fprintf (stderr, "error with lstat on %s: %m\n",
				 dst_name);
			exit (1);
		}
	} else {
		exists = 1;
	}

	if (strlen (newest) + strlen (path) + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}
	sprintf (newbr_name, "%s/%s", newest, path);

	if (strlen (backup_branch) + strlen (path)
	    + strlen ("../") * ftwbuf->level + 100 >= PATH_MAX) {
		fprintf (stderr, "path exceeds PATH_MAX\n");
		exit (1);
	}

	p = newbr_tar;
	for (idx = 0; idx < ftwbuf->level + 1; idx++) {
		strcpy (p, "../");
		p += 3;
	}
	sprintf (p, "%s/%s", backup_branch, path);

	switch (tflag) {
	case FTW_F:
		if (exists) {
			if (S_ISREG (sb->st_mode)
			    && sb->st_mtime == dst_sb.st_mtime
			    && sb->st_size == dst_sb.st_size) {
				return (0);
			} else {
				fallback_backup (fpath, sb, tflag, ftwbuf,
						 newbr_name);
				return (0);
			}
		}

		copy_file (fpath, dst_name);

		times.actime = sb->st_atime;
		times.modtime = sb->st_mtime;

		if (utime (dst_name, &times) == -1) {
			fprintf (stderr, "failed to set timestamps on %s: %m\n",
				 dst_name);
		}

		if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n", dst_name);
		}

		set_immutable (dst_name);

		delete_file_or_dir (newbr_name);

		if (symlink (newbr_tar, newbr_name) == -1) {
			fprintf (stderr, "failed to create symlink %s: %m\n",
				 newbr_name);
			return (0);
		}

		break;
	case FTW_D:
		if (exists) {
			if (S_ISDIR (sb->st_mode)) {
				touched_dir (dst_name, sb);
				return (0);
			} else {
				fprintf (stderr, "failed to create %s, %s"
					 " exists with same name\n", dst_name,
					 S_ISREG (sb->st_mode)
					 ? "file" : "symlink");
				return (0);
			}
		}

		if (mkdir (dst_name, sb->st_mode) == -1) {
			fprintf (stderr, "failed to create directory %s: %m\n",
				 dst_name);
			exit (1);
		}

		if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n", dst_name);
		}

		if (lstat (dst_name, &dst_sb) == -1) {
			fprintf (stderr, "error with lstat on %s: %m\n",
				 dst_name);
			exit (1);
		}

		touched_dir (dst_name, sb);

		delete_file_or_dir (newbr_name);

		if (mkdir (newbr_name, sb->st_mode) == -1) {
			fprintf (stderr, "failed to create directory %s: %m\n",
				 newbr_name);
			return (0);
		}

		if (lchown (newbr_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n",
				 newbr_name);
		}

		if (lstat (newbr_name, &dst_sb) == -1) {
			fprintf (stderr, "error with lstat on %s: %m\n",
				 newbr_name);
			exit (1);
		}

		touched_dir (newbr_name, sb);

		break;
	case FTW_SL:
		r = readlink (fpath, lnk_tar, sb->st_size + 1);

		if (r < 0) {
			fprintf (stderr, "failed to read link %s: %m\n",
				 dst_name);
			exit (1);
		}

		if (r > sb->st_size) {
			fprintf (stderr, "symlink increased in size "
				 "between lstat and readlink\n");
			exit (1);
		}

		lnk_tar[sb->st_size] = 0;

		if (exists) {
			if (S_ISLNK (sb->st_mode)) {
				r = readlink (dst_name, old_tar,
					      sb->st_size + 1);

				if (r < 0) {
					fprintf (stderr, "failed to read"
						 " link %s: %m\n", dst_name);
					return (0);
				}

				if (strcmp (lnk_tar, old_tar) == 0) {
					return (0);
				}
			} else {
				fprintf (stderr, "failed to create symlink %s,"
					 " something already exists with"
					 " same name\n", dst_name);
				return (0);
			}
		}

		if (symlink (lnk_tar, dst_name) == -1) {
			fprintf (stderr, "failed to create symlink %s: %m\n",
				 dst_name);
			exit (1);
		}

		if (lchown (dst_name, sb->st_uid, sb->st_gid) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n", dst_name);
		}

		delete_file_or_dir (newbr_name);

		if (symlink (newbr_tar, newbr_name) == -1) {
			fprintf (stderr, "failed to create symlink %s: %m\n",
				 newbr_name);
			return (0);
		}

		break;
	default:
		return (0);
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

	if (optind < argc) {
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

		time (&rawtime);
		timeinfo = localtime (&rawtime);
		sprintf (backup_branch, "%04d-%02d-%02d",
			 timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
			 timeinfo->tm_mday);

		l = strlen (backup_root) + strlen (backup_branch) + 10;
		if ((backup_dir = calloc (1, l)) == NULL) {
			fprintf (stderr, "failed to allocate backup_dir\n");
			return (1);
		}

		sprintf (backup_dir, "%s/%s", backup_root, backup_branch);

		if (mkdir (backup_dir, 0755) == -1) {
			if (errno != EEXIST) {
				fprintf (stderr,
					 "failed to create directory %s: %m\n",
					 backup_dir);
				return (1);
			}
		}

		if (lchown (backup_dir, 0, 0) == -1) {
			fprintf (stderr, "failed to chown %s: %m\n",
				 backup_dir);
		}

		for (idx = optind; idx < argc; idx++) {
			s = strdup (argv[idx]);
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

			free (s);

			fix_dirs ();
		}
	} else {
		usage ();
	}

	return (0);
}
