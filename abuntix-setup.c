#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>

int error_flag;
char *arch;

char *pkgs_wanted_common[] = {
	"emacs24",
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
	"xscreensaver",
	"xscreensaver-data",
	"xscreensaver-data-extra",
	"xscreensaver-gl",
	"xscreensaver-gl-extra",
	"xscreensaver-screensaver-bsod",
	"xscreensaver-screensaver-dizzy",
	"xscreensaver-screensaver-webcollage",
	"texlive-xetex",
	"texlive-latex-extra",
};

char *pkgs_wanted_rich[] = {
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
};

void
usage (void)
{
	fprintf (stderr, "usage: dev-setup [-e]\n");
	exit (1);
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
mark_packages_wanted (char **names, int nnames)
{
	int idx;
	struct pkg *pp;

	for (idx = 0; idx < nnames; idx++) {
		pp = find_pkg (names[idx], 1);
		pp->wanted = 1;
	}
}

void
check_packages (void)
{
	char cmd[100000];
	char *outp;
	struct pkg *pp;
	int missing_flag;

	mark_packages_wanted (pkgs_wanted_common,
			      sizeof pkgs_wanted_common
			      / sizeof pkgs_wanted_common[0]);
	
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
	char *homedir;

	if ((homedir = getenv ("HOME")) == NULL) {
		fprintf (stderr, "missing HOME environment variable\n");
		exit (1);
	}
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

void
check_emacs (void)
{
	system ("/usr/bin/touch ~/.emacs");

	if (system ("/usr/bin/diff -q ~/.emacs emacsconfig")) {
		error_flag = 1;
		printf ("cp emacsconfig ~/.emacs\n\n");
		printf ("sudo cp emacsconfig /etc/skel/.emacs\n");
	}
}

void
check_pythonrc (void)
{
	system ("/usr/bin/touch ~/.pythonrc");

	if (system ("/usr/bin/diff -q ~/.pythonrc pythonrc")) {
		error_flag = 1;
		printf ("cp pythonrc ~/.pythonrc\n\n");
		printf ("sudo cp pythonrc /etc/skel/.pythonrc\n");
	}
}

void
check_xterm (void)
{
	if (!system ("[ -e /etc/X11/Xresources/xterm ]")) {
		error_flag = 1;
		printf ("sudo cp xterm.resources"
			" /etc/X11/Xresources/xterm\n\n");
	}
}

void
check_fluxbox (void)
{
	if (system ("[ -d $HOME/.fluxbox ]")) {
		error_flag = 1;
		printf ("cp -rf fluxbox $HOME/.fluxbox\n");
		printf ("sudo cp -rf fluxbox /etc/skel/.fluxbox\n");
		printf ("sudo cp $HOME/.fluxbox/backgrounds/beast.png"
			" /usr/share/images/fluxbox/beast.png\n");
	}
}

void
check_chrome (void)
{
	char *cmd;

	cmd = "/usr/bin/dpkg-query --show google-chrome > /dev/null 2>&1";

	if (system (cmd)) {
		printf ("wget https://dl.google.com/linux/direct/google-chrome-stable_current_%s.deb\n", arch);
		printf ("sudo dpkg -i google-chrome-stable_current_%s.deb\n\n", arch);
		error_flag = 1;
	}
}

void
check_env (void)
{
	char *cmd, *pythonstartup, *histsize, *histfilesize;

	histsize = getenv ("HISTSIZE");
	if (!histsize || strtol (histsize, (char **) NULL, 10) < 100000) {
		printf ("# HISTSIZE = %s, suggested increasing size.\n",
			histsize);
		printf ("echo 'export HISTSIZE=100000' >> $HOME/.bashrc\n\n");
		error_flag = 1;
	}

	histfilesize = getenv ("HISTFILESIZE");
	if (!histfilesize || strtol (histfilesize,
				     (char **) NULL, 10) < 200000) {
		printf ("# HISTFILESIZE = %s, suggest increasing size.\n",
			histfilesize);
		printf ("echo 'export HISTFILESIZE=200000' >> $HOME/.bashrc\n\n");
		error_flag = 1;
	}

	cmd = "grep -q 'alias gpg=gpg2' $HOME/.bashrc";
	if (system (cmd)) {
		printf ("# If this appears after running this command,"
			" check gpg version manually. Script may fail"
			" to detect version\n");
		printf ("echo 'alias gpg=gpg2' >> $HOME/.bashrc\n");
		error_flag = 1;
	}

	/* cmd = "sqlite --version"; */
	/* if (system (cmd)) */
	/* 	printf ("echo 'alias sqlite=sqlite3' >> $HOME/.bashrc"); */

	pythonstartup = getenv ("PYTHONSTARTUP");
	if (!pythonstartup || !strcmp (pythonstartup, ".pythonrc")) {
		printf ("echo 'export PYTHONSTARTUP=.pythronrc'"
			" >> $HOME/.bashrc\n");
		error_flag = 1;
	}
}

void
check_sources (void)
{
	char *cmd;

	cmd = "grep -q mirrors.mit.edu /etc/apt/sources.list >/dev/null 2>&1";
	if (system (cmd)) {
		printf ("cat /etc/apt/sources.list"
			" | sed 's#[^/]*archive.ubuntu.com#mirrors.mit.edu#g'"
			" > sources.list\n");
		printf ("sudo cp sources.list /etc/apt/sources.list\n");
		printf ("sudo apt-get update\n");
		error_flag = 1;
	}
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

	if (!system ("[ `uname -i` = x86_64 ]"))
		arch = "amd64";
	else
		arch = "i386";

	check_sources ();
	check_packages ();
	check_emacs ();
	check_pythonrc ();
	check_xterm ();
	check_fluxbox ();
	check_tex_papersize ();
	check_chrome ();
	check_env ();
	
	check_alternative ("editor", "emacs");
	check_alternative ("x-terminal-emulator", "uxterm");

	if (error_flag == 0)
		printf ("everything ok\n");

	return (0);
}
