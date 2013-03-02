/* http://archive.ubuntu.com/ubuntu/indices/ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

int conf_mit_mirrors;
int conf_screensavers;

char *homedir;

int error_flag;
char *machine;
char *arch;
char *release;

char *pkgs_common[] = {
	"openssh-server",
	"fluxbox",
	"git-core",
	"apt-file",
	"traceroute",
	"nmap",
	"gnupg2",
	"gitk",
	"thunar",
	"mercurial",
	"feh",
	"ttf-dejavu",
	"ttf-dejavu-extra",
	"ttf-dejavu-core",
	"libgconf2-4",
	"gnuplot",
	"imagemagick",
	"texlive-xetex",
	"texlive-latex-extra",
	NULL
};

char *pkgs_screensavers[] = {
	"xscreensaver",
	"xscreensaver-data",
	"xscreensaver-data-extra",
	"xscreensaver-gl",
	"xscreensaver-gl-extra",
	"xscreensaver-screensaver-bsod",
	"xscreensaver-screensaver-dizzy",
	"xscreensaver-screensaver-webcollage",
	NULL
};

char *pkgs_rich[] = {
	"conky",
	"apache2",
	"postgresql",
	"sqlite3",
	"meld",
	"xautomation",
	"gimp",
	"python-pip",
	"cmus",
	"vlc",
	NULL
};

void
usage (void)
{
	fprintf (stderr, "usage: abuntix-setup [-e]\n");
	exit (1);
}

int
file_equal (char *name1, char *name2)
{
	FILE *in1 = NULL;
	FILE *in2 = NULL;
	int c1, c2;
	int equal_flag;

	equal_flag = 0;

	in1 = fopen (name1, "r");
	in2 = fopen (name2, "r");

	if (in1 == NULL || in2 == NULL)
		goto done;

	while (!feof (in1) && !feof (in2)) {
		c1 = getc (in1);
		c2 = getc (in2);
		
		if (c1 == EOF && c2 == EOF) {
			equal_flag = 1;
			break;
		}

		if (c1 != c2)
			break;
	}

done:
	if (in1)
		fclose (in1);
	if (in2)
		fclose (in2);
	return (equal_flag);
}


void
check_alternative (char *prog, char *wanted)
{
	char src[1000];
	char dest[1000];

	sprintf (src, "/etc/alternatives/%s", prog);

	if (readlink (src, dest, sizeof dest) < 0
	    || strstr (dest, wanted) == NULL) {
		error_flag = 1;
		printf ("# want %s -> %s\n", prog, wanted);
		printf ("sudo update-alternatives --config %s\n", prog);
		printf ("\n");
	}
}

struct pkg {
	struct pkg *next;
	char *name;
	int installed;
	int wanted;
};

struct pkg *pkgs;

struct pkg *
find_pkg (char *name, int create)
{
	struct pkg *pp;

	for (pp = pkgs; pp; pp = pp->next) {
		if (strcmp (pp->name, name) == 0)
			return (pp);
	}

	if (create == 0)
		return (NULL);
	if ((pp = calloc (1, sizeof *pp)) == NULL
	    || (pp->name = strdup (name)) == NULL) {
		fprintf (stderr, "out of memory\n");
		exit (1);
	}

	pp->next = pkgs;
	pkgs = pp;

	return (pp);
}

void
get_installed_packages (void)
{
	char *cmd;
	FILE *f;
	char buf[1000], *p;
	struct pkg *pp;

	cmd = "dpkg-query --show -f='${Package}\t${Version}\n'";
	
	if ((f = popen (cmd, "r")) == NULL) {
		printf ("can't run %s\n", cmd);
		exit (1);
	}

	while (fgets (buf, sizeof buf, f) != NULL) {
		p = buf;
		while (*p && !isspace (*p))
			p++;
		*p = 0;
		pp = find_pkg (buf, 1);
		pp->installed = 1;
	}
	pclose (f);
}

void
mark_package_wanted (char *name)
{
	struct pkg *pp;

	pp = find_pkg (name, 1);
	pp->wanted = 1;
}

void
mark_packages_wanted (char **names)
{
	int idx;

	for (idx = 0; names[idx]; idx++)
		mark_package_wanted (names[idx]);
}

void
check_packages (void)
{
	char cmd[100000];
	char *outp;
	struct pkg *pp;
	int missing_flag;

	mark_packages_wanted (pkgs_common);
	
	if (system ("apt-cache show emacs24 > /dev/null 2>&1") == 0) {
		mark_package_wanted ("emacs24");
	} else {
		mark_package_wanted ("emacs");
	}

	if (conf_screensavers)
		mark_packages_wanted (pkgs_screensavers);

	get_installed_packages ();
	
	outp = cmd;
	outp += sprintf (outp, "sudo apt-get install");
	missing_flag = 0;
	for (pp = pkgs; pp; pp = pp->next) {
		if (pp->wanted && ! pp->installed) {
			missing_flag = 1;
			outp += sprintf (outp, " %s", pp->name);
		}
	}

	if (missing_flag) {
		error_flag = 1;
		printf ("%s\n", cmd);
		printf ("\n");
	}
}

void
check_tex_papersize (void)
{
	FILE *f;
	int i;
	char *files[] = { "TMP.tex", "TMP.dvi", "TMP.log", "TMP.ps" };
	int nfiles = sizeof files / sizeof files[0];
	int success;
	char buf[1000], val[1000];
	char fname[1000];

	sprintf (fname, "%s/.texmf-config", homedir);
	if (access (fname, F_OK) >= 0) {
		error_flag = 1;
		printf ("# %s exists, but that is not safe\n", fname);
	}

	success = 0;

	for (i = 0; i < nfiles; i++)
		remove (files[i]);

	f = fopen ("TMP.tex", "w");
	fprintf (f, "test\\bye\n");
	fclose (f);
	if (system ("(tex TMP.tex && dvips -o TMP.ps TMP.dvi)"
		    "   > /dev/null 2>&1") != 0) {
		error_flag = 1;
		printf ("(tex not installed yet)\n");
		goto done;
	} 
		
	if ((f = fopen ("TMP.ps", "r")) == NULL) {
		error_flag = 1;
		printf ("error running tex: TMP.ps was not created\n");
		goto done;
	}
	success = 0;
	while (fgets (buf, sizeof buf, f) != NULL) {
		if (sscanf (buf, "%%%%DocumentPaperSizes: %s", val) == 1
		    && strcmp (val, "Letter") == 0) {
			success = 1;
			break;
		}
	}
	fclose (f);
	if (success == 0) {
		error_flag = 1;
		printf ("# set tex paper size to Letter (including dvips)\n");
		printf ("# see TMP.ps\n");
		printf ("texconfig-sys\n");
	}

done:
	if (0 && success) {
		for (i = 0; i < nfiles; i++)
			remove (files[i]);
	}
}

int
in_homedir (char *dest)
{
	int n;

	n = strlen (homedir);
	if (strncmp (dest, homedir, n) == 0 && dest[n] == '/')
		return (1);
	return (0);
}

void
check_file (char *desired, char *dest)
{
	if (! file_equal (desired, dest)) {
		error_flag = 1;
		if (in_homedir (dest)) {
			printf ("cp %s %s\n", desired, dest);
		} else {
			printf ("sudo cp %s %s\n", desired, dest);
		}
	}
}

void
check_dot_emacs (void)
{
	char dest[1000];

	sprintf (dest, "%s/.emacs", homedir);
	check_file ("emacsconfig", dest);

	sprintf (dest, "/etc/skel/.emacs");
	check_file ("emacsconfig", dest);
}

void
check_pythonrc (void)
{
	char dest[1000];

	sprintf (dest, "%s/.pythonrc", homedir);
	check_file ("pythonrc", dest);

	sprintf (dest, "/etc/skel/.pythonrc");
	check_file ("pythonrc", dest);
}

void
check_xterm (void)
{
	char dest[1000];

	sprintf (dest, "/etc/X11/Xresources/xterm");
	check_file ("xterm.resources", dest);
}

void
check_fluxbox (void)
{
	char dest[1000];

	sprintf (dest, "%s/.fluxbox/init", homedir);
	if (! file_equal (dest, "fluxbox/init")) {
		error_flag = 1;
		printf ("cp -rf fluxbox/. %s/.fluxbox\n", homedir);
	}

	sprintf (dest, "/etc/skel/.fluxbox/init");
	if (! file_equal (dest, "fluxbox/init")) {
		error_flag = 1;
		printf ("sudo cp -rf fluxbox /etc/skel/.fluxbox\n");
	}
}

void
check_chrome (void)
{
	char pkgname[1000];
	struct pkg *pp;

	if ((pp = find_pkg ("google-chrome-stable", 0)) == NULL
	    || pp->installed == 0) {
		error_flag = 1;
		sprintf (pkgname, "google-chrome-stable_current_%s.deb", arch);
		printf ("wget https://dl.google.com/linux/direct/%s\n",
			pkgname);
		printf ("sudo dpkg -i %s\n\n", pkgname);
	}
}

int
string_present (char *filename, char *str)
{
	FILE *inf;
	int found;
	char buf[1000];

	if ((inf = fopen (filename, "r")) == NULL)
		return (0);

	found = 0;
	while (fgets (buf, sizeof buf, inf) != NULL) {
		if (strstr (buf, str) != NULL) {
			found = 1;
			break;
		}
	}

	fclose (inf);

	return (found);
}

void
check_string (char *filename, char *str)
{
	char *p;

	if (! string_present (filename, str)) {
		error_flag = 1;
		printf ("echo '");
		for (p = str; *p; p++) {
			if (*p == '\'' || *p == '\\')
				putchar ('\\');
			putchar (*p);
		}
		printf ("' >> %s\n", filename);
	}
}


void
check_bashrc (void)
{
	char bashrc[1000];

	sprintf (bashrc, "%s/.bashrc", homedir);
	check_string (bashrc, "export HISTSIZE=100000");
	check_string (bashrc, "export HISTFILESIZE=200000");
	check_string (bashrc, "export PYTHONSTARTUP=.pythronrc");
}

void
check_mit_mirrors (void)
{
	if (! string_present ("/etc/apt/sources.list", "mirrors.mit.edu")) {
		error_flag = 1;
		printf ("sed 's:[^/]*archive.ubuntu.com:mirrors.mit.edu:g'"
			" < /etc/apt/sources.list"
			" > sources.list\n");
		printf ("sudo cp sources.list /etc/apt/sources.list\n");
		printf ("sudo apt-get update\n");
	}
}

char *
get_from_cmd (char *cmd)
{
	FILE *inf;
	char buf[1000];
	int len;

	if ((inf = popen (cmd, "r")) == NULL) {
		fprintf (stderr, "error running: %s\n", cmd);
		exit (1);
	}
	if (fgets (buf, sizeof buf, inf) == NULL) {
		fprintf (stderr, "%s: no data\n", cmd);
		exit (1);
	}
	fclose (inf);

	len = strlen (buf);
	while (len > 0 && isspace (buf[len-1]))
		buf[--len] = 0;
	return (strdup (buf));
}

void
abuntix_config (void)
{
	FILE *inf;
	char buf[1000];
	char *name, *val;
	char *p;
	int len;

	if ((inf = fopen ("TMP.conf", "r")) == NULL) {
		system ("./config");
		if ((inf = fopen ("TMP.conf", "r")) == NULL) {
			fprintf (stderr, "can't open TMP.conf\n");
			exit (1);
		}
	}
	while (fgets (buf, sizeof buf, inf) != NULL) {
		len = strlen (buf);
		while (len > 0 && isspace (buf[len-1]))
			buf[--len] = 0;
		name = buf;
		for (p = buf; *p && *p != '='; p++)
			;
		if (*p)
			*p++ = 0;
		val = p;

		if (strcmp (name, "mit_mirrors") == 0)
			conf_mit_mirrors = atoi (val);
		else if (strcmp (name, "screensavers") == 0)
			conf_screensavers = atoi (val);
	}
	fclose (inf);
}

int
main (int argc, char **argv)
{
	char c;

	while ((c = getopt (argc, argv, "e")) != EOF) {
		switch (c) {
		default:
			usage ();
		}
	}

	if (optind > argc || optind != argc)
		usage ();

	if ((homedir = getenv ("HOME")) == NULL) {
		fprintf (stderr, "missing $HOME\n");
		exit (1);
	}

	abuntix_config ();

	machine = get_from_cmd ("uname --machine");
	if (strcmp (machine, "x86_64") == 0) {
		arch = "amd64";
	} else {
		arch = "i386";
	}

	/* a string like "12.04" */
	release = get_from_cmd ("lsb_release --release --short");

	if (conf_mit_mirrors)
		check_mit_mirrors ();
	check_packages ();
	check_dot_emacs ();
	check_pythonrc ();
	check_xterm ();
	check_fluxbox ();
	check_tex_papersize ();
	check_chrome ();
	check_bashrc ();
	
	check_alternative ("editor", "emacs");
	check_alternative ("x-terminal-emulator", "uxterm");

	if (error_flag == 0)
		printf ("everything ok\n");

	return (0);
}
