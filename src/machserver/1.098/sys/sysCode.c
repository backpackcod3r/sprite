/* 
 * sysCode.c --
 *
 *	Miscellaneous routines for the system.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/kernel/sys/RCS/sysCode.c,v 9.3 90/10/09 12:00:41 jhh Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <dbg.h>
#include <sys.h>
#include <sysInt.h>
#include <rpc.h>
#include <sync.h>
#include <sched.h>
#include <vm.h>
#include <net.h>
#include <stdio.h>

/*
 * Should be in some header file?
 */
extern	void	SysInitSysCall();



/*
 * ----------------------------------------------------------------------------
 *
 * Sys_Init --
 *
 *	Initializes system-dependent data structures.
 *
 *	The number of calls to disable interrupts is set to 1 for 
 *	each processor, since Sys_Init is assumed to be called with 
 *	interrupts off and to be followed with an explicit call to 
 *	enable interrupts.
 *
 *	Until ENABLE_INTR() is called without a prior DISABLE_INTR() (i.e.,
 *	when it is called outside the context of a MASTER_UNLOCK), interrupts
 *	will remain disabled.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	For each processor, the number of disable interrupt calls outstanding
 *	is initialized.  
 *
 * ----------------------------------------------------------------------------
 */

void 
Sys_Init()
{
    SysInitSysCall();
}

/*
 *----------------------------------------------------------------------
 *
 * Sys_GetHostId --
 *
 *	This returns the Sprite Host Id for the system.  This Id is
 *	guaranteed to be unique accross all Sprite Hosts participating
 *	in the system.  This is plucked from the RPC system now,
 *	but perhaps should be determined from the filesystem.
 *
 * Results:
 *	The Sprite Host Id.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
Sys_GetHostId()
{
    return(rpc_SpriteID);
}

/*
 *----------------------------------------------------------------------
 *
 * Sys_HostPrint --
 *
 *	Print out a statement concerning a host.  This maps to a
 *	string hostname if possible, and prints out the message.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	printf.
 *
 *----------------------------------------------------------------------
 */

static int lastDay[13] = {0,31,28,31,30,31,30,31,31,30,31,30,31};

void
Sys_HostPrint(spriteID, string)
    int spriteID;
    char *string;
{
    char hostName[128];
    Time time;
    int offset;
    int seconds;
    Boolean dstFlag;
    Time_Parts timeParts;

    Timer_GetTimeOfDay(&time, &offset, &dstFlag);
    seconds = time.seconds + offset * 60;
    Time_ToParts(seconds, FALSE, &timeParts);
    /*
     * Until Time_ToParts makes the month count from 1, not zero.
     */
    timeParts.month += 1;
    /*
     * Gag, my own (simplified) daylight savings correction.
     */
    if (dstFlag) {
	if ((timeParts.month >= 4) &&	/* All of April */
	    (timeParts.month <= 10)) {	/* thru October */
	    timeParts.hours++;
	    if (timeParts.hours >= 24) {
		timeParts.hours = 0;
		timeParts.dayOfMonth++;
		if (timeParts.dayOfMonth > lastDay[timeParts.month]) {
		    timeParts.month++;
		    timeParts.dayOfMonth = 1;
		}
	    }
	}
    }
    printf("%d/%d/%d %d:%02d:%02d ", timeParts.month, timeParts.dayOfMonth,
	    timeParts.year, timeParts.hours, timeParts.minutes,
	    timeParts.seconds);

    Net_SpriteIDToName(spriteID, 128, hostName);
    if (*hostName == '\0') {
	printf("Sprite Host <%d> %s", spriteID, string);
    } else {
	printf("%s (%d) %s", hostName, spriteID, string);
    }
}

