/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that this notice is preserved and that due credit is given
 * to the University of California at Berkeley. The name of the University
 * may not be used to endorse or promote products derived from this
 * software without specific prior written permission. This software
 * is provided ``as is'' without express or implied warranty.
 */

#ifndef lint
static char sccsid[] = "@(#)cmdtab.c	5.2 (Berkeley) 5/5/88";
#endif /* not lint */

/*
 * lpc -- command tables
 */

#include "lpc.h"

int	Abort(), clean(), enable(), disable(), down(), help();
int	quit(), restart(), start(), status(), stop(), topq(), up();

char	aborthelp[] =	"terminate a spooling daemon immediately and disable printing";
char	cleanhelp[] =	"remove cruft files from a queue";
char	enablehelp[] =	"turn a spooling queue on";
char	disablehelp[] =	"turn a spooling queue off";
char	downhelp[] =	"do a 'stop' followed by 'disable' and put a message in status";
char	helphelp[] =	"get help on commands";
char	quithelp[] =	"exit lpc";
char	restarthelp[] =	"kill (if possible) and restart a spooling daemon";
char	starthelp[] =	"enable printing and start a spooling daemon";
char	statushelp[] =	"show status of daemon and queue";
char	stophelp[] =	"stop a spooling daemon after current job completes and disable printing";
char	topqhelp[] =	"put job at top of printer queue";
char	uphelp[] =	"enable everything and restart spooling daemon";

struct cmd cmdtab[] = {
	{ "abort",	aborthelp,	Abort,		1 },
	{ "clean",	cleanhelp,	clean,		1 },
	{ "enable",	enablehelp,	enable,		1 },
	{ "exit",	quithelp,	quit,		0 },
	{ "disable",	disablehelp,	disable,	1 },
	{ "down",	downhelp,	down,		1 },
	{ "help",	helphelp,	help,		0 },
	{ "quit",	quithelp,	quit,		0 },
	{ "restart",	restarthelp,	restart,	0 },
	{ "start",	starthelp,	start,		1 },
	{ "status",	statushelp,	status,		0 },
	{ "stop",	stophelp,	stop,		1 },
	{ "topq",	topqhelp,	topq,		1 },
	{ "up",		uphelp,		up,		1 },
	{ "?",		helphelp,	help,		0 },
#ifndef sprite
	{ 0 },
#endif
};

int	NCMDS = sizeof (cmdtab) / sizeof (cmdtab[0]);
