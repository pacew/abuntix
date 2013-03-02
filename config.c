#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int conf_mit_mirrors;
int conf_screensavers;

int
y_or_n (char *prompt)
{
	char buf[1000];

	printf ("%s (y or n): ", prompt);
	fflush (stdout);
	if (fgets (buf, sizeof buf, stdin) == NULL)
		exit (1);
	if (strncasecmp (buf, "y", 1) == 0)
		return (1);
	return (0);
}

int
main (int argc, char **argv)
{
	FILE *outf;

	conf_mit_mirrors = y_or_n ("Do you want to use the MIT mirrors?");
	conf_screensavers = y_or_n ("Do you want fancy screensavers?");

	if ((outf = fopen ("TMP.conf", "w")) == NULL) {
		fprintf (stderr, "can't create TMP.conf\n");
		exit (1);
	}
	fprintf (outf, "mit_mirrors=%d\n", conf_mit_mirrors);
	fprintf (outf, "screensavers=%d\n", conf_screensavers);
	fclose (outf);

	return (0);
}
