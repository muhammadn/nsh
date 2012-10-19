/* $nsh: main.c,v 1.44 2012/06/01 18:03:09 chris Exp $ */
/*
 * Copyright (c) 2002-2008 Chris Cappuccio <chris@nmedia.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <sys/socket.h>
#include <sys/syslimits.h>
#include <sys/ttycom.h>
#include <sys/signal.h>
#include "editing.h"
#include "stringlist.h"
#include "externs.h"

void usage(void);

jmp_buf toplevel;

char *vers = "20120523";
int bridge = 0;		/* bridge mode for interface() */
int verbose = 0;	/* verbose mode */
int priv = 0, cli_rtable = 0;
int editing;
pid_t pid;

History *histi = NULL;
History *histc = NULL;
HistEvent ev;
EditLine *elc = NULL;
EditLine *eli = NULL;
char *cursor_pos = NULL;

void intr(void);

int
main(int argc, char *argv[])
{
	int top, ch, iflag = 0;
	char rc[PATH_MAX];

	if(getuid() != 0) 
		printf("%% Functionality may be limited without root privileges.\n");

	pid = getpid();

	while ((ch = getopt(argc, argv, "i:v")) != -1)
		switch (ch) {
		case 'i':
			iflag = 1;
			strlcpy(rc, optarg, PATH_MAX);
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	/* create temporal tables (if they aren't already there) */
	if (db_create_table_rtables() < 0)
		printf("%% database rtables creation failed\n");
	if (db_create_table_flag_x("ctl") < 0)
		printf("%% database ctl creation failed\n");
	if (db_create_table_flag_x("dhcrelay") < 0)
		printf("%% database dhcrelay creation failed\n");

	printf("%% NSH v%s\n", vers);

	if (argc > 0)
		usage();

	if (iflag) {
		/*
		 * Interpret config file and exit
		 */
		char *argv_demote[] = { "group", "carp", "carpdemote", "128" };
		char *argv_restore[] = { "no", "group", "carp", "carpdemote", "128" };

		struct daemons *daemons;

		for (daemons = ctl_daemons; daemons->name != 0; daemons++)
			if (daemons->tmpfile)
				rmtemp(daemons->tmpfile);

		priv = 1;

		/*
		 * Set carp group carpdemote to 128 during initialization
		 */
		group(sizeof(argv_demote) / sizeof(argv_demote[0]), argv_demote);

		cmdrc(rc);

		/*
		 * Initialization over
		 */
		group(sizeof(argv_restore) / sizeof(argv_restore[0]), argv_restore);

		exit(0);
	}

	top = setjmp(toplevel) == 0;
	if (top) {
		(void)signal(SIGWINCH, setwinsize);
		(void)signal(SIGINT, (sig_t)intr);
		(void)setwinsize(0);
	} else
		putchar('\n');

	for (;;) {
		command();
		top = 1;
	}

	/* NOTREACHED */
	return 0;
}

void
usage(void)
{
	(void)fprintf(stderr, "usage: %s [-v] [-i rcfile]\n", __progname);
	(void)fprintf(stderr, "           -v indicates verbose operation\n");
	(void)fprintf(stderr, "           -i rcfile loads configuration from rcfile\n");
	exit(1);
}

void
intr(void)
{
	longjmp(toplevel, 1);
}
