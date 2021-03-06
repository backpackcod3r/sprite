/* 
 * vmstat.c --
 *
 *	Statistics generation for the virtual memory module.
 *
 * Copyright (C) 1986 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/cmds/vmstat/RCS/vmstat.c,v 1.8 90/07/03 16:26:25 mendel Exp $ SPRITE (Berkeley)";
#endif not lint

#include <vm.h>
#include <vmStat.h>
#include <kernel/vm.h>
#include "/sprite/src/kernel/vm/vmInt.h"
#include <option.h>
#include <sys/time.h>
#include <stdio.h>

/*
 * Flags to command-line options:
 */
int	checkTime = -1;			/* If != -1 indicates how often to
					 * print a line of stats.  0 means 
					 * just print one line and quit, 
					 * non-zero means print a line
					 * every checkTime seconds. */
int	printSegInfo = 0;		/* Non-zero means print info for
					 * all segments. */
int	whenToPrintHdr = 25;		/* Number of lines of stats after 
					 * which to print the header. */
int	faultThreshold = 0;		/* Number of page faults after which
					 * to print line of stats. */
int	pageOutThreshold = 0;		/* Number of page outs after which
					 * to print line of stats. */
int	relToLastPrint = 0;		/* Non-zero means print stats relative
					 * to last time printed, not last
					 * interval of lengh checkTime. */
int	printMod = 0;			/* Non-zero means replace printing out
					 * of kernel stack pages with number
					 * of modified pages in memory. */
int	maxTimes = -1;			/* Maximum number can skip printing
					 * a line of info because of 
					 * faultThreshold or pageOutThreshold.*/
int	verbose = 0;			/* Print extra more obscure vm 
					 * stats. */

Option optionArray[] = {
    {OPT_TRUE, "s", (Address)&printSegInfo,
	"Print out information about all in-use segments"},
    {OPT_TRUE, "v", (Address)&verbose,
	"Print out extra, more obscure vm statistics"},
    {OPT_INT, "t", (Address)&checkTime,
	"Print out incremental vm info at this interval"},
    {OPT_INT, "T", (Address)&maxTimes,
	"Maximum times skip printing because of -f or -p flags. Used with -t."},
    {OPT_INT, "l", (Address)&whenToPrintHdr,
      "Lines to print before should print header (default: 25). Used with -t."},
    {OPT_INT, "f", (Address)&faultThreshold, 
   "Page faults per second before print out info (default: 0).  Used with -t."},
    {OPT_INT, "p", (Address) &pageOutThreshold,
    "Page outs per second before print out info (default: 0).  Used with -t."},
    {OPT_TRUE, "P", (Address)&relToLastPrint,
 "Print out all info since last print, not since last interval. Used with -t."},
    {OPT_TRUE, "m", (Address)&printMod,
    "Print out number of modified pages, not kern stack pages.  Used with -t."},
};
int	numOptions = Opt_Number(optionArray);

#define	NUM_SEGMENTS	256
Vm_Stat 	vmStat;

int	kbPerPage;

/*
 *----------------------------------------------------------------------
 *
 * main --
 *
 *	The main program for vmstat.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints information on standard output.
 *
 *----------------------------------------------------------------------
 */
main(argc, argv)
    int argc;
    char *argv[];
{
    int		pageSize;

    (void)Opt_Parse(argc, argv, optionArray, numOptions, 0);
    Vm_PageSize(&pageSize);
    kbPerPage = pageSize / 1024;

    if (printSegInfo) {
	PrintSegInfo();
    } else if (checkTime == 0) {
	PrintWide();
	printf("\n");
    } else if (checkTime == -1) {
	PrintSummary();
    } else {
	while (1) {
	    PrintWide();
	    sleep(checkTime);
	}
    }
    exit(0);
}


/*
 *----------------------------------------------------------------------
 *
 * PrintSegInfo --
 *
 *	Print vm info about all segments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints information on standard output.
 *
 *----------------------------------------------------------------------
 */
PrintSegInfo()
{
    int			actCodePages = 0;
    int			inactCodePages = 0;
    int			actCodeSegs = 0;
    int			inactCodeSegs = 0;
    int			heapPages = 0;
    int			heapSegs = 0;
    int			stackPages = 0;
    int			stackSegs = 0;
    Vm_SegmentInfo	seg;
    int			i;
    char		*typePtr;
    char		*fileNamePtr;
    ReturnStatus	status;

    printf("%-10s%-10s%-10s%-10s%-10s%s\n\n", "SEG-NUM", "TYPE",
		"  SIZE", "RES-SIZE", "NUM-REFS", "OBJECT-FILE-NAME");
    for (i = 1; i < NUM_SEGMENTS; i++) {
	fileNamePtr = " ";
	status = Vm_GetSegInfo(NULL, i, sizeof(Vm_SegmentInfo), &seg);
	if (status != SUCCESS) {
	    (void) fprintf(stderr,
			   "Couldn't read segment info for segment %d: %s\n",
			   i,  Stat_GetMsg(status));
	    return;
	}
	if (seg.flags & VM_SEG_FREE) {
	    continue;
	}
	switch (seg.type) {
	    case VM_CODE:
	        if (seg.flags & VM_SEG_INACTIVE) {
		    typePtr = "Inactive";
		    inactCodePages += seg.resPages;
		    inactCodeSegs++;
		} else {
		    typePtr = "Code";
		    actCodePages += seg.resPages;
		    actCodeSegs++;
		}
		fileNamePtr = seg.objFileName;
		seg.objFileName[VM_OBJ_FILE_NAME_LENGTH - 1] = '\0';
		break;
	    case VM_HEAP:
		typePtr = "Heap";
		heapPages += seg.resPages;
		heapSegs++;
		break;
	    case VM_STACK:
		typePtr = "Stack";
		stackPages += seg.resPages;
		stackSegs++;
		break;
	}
	printf("%-10d%-10s   %-7d   %-7d   %-7d%s\n",
		       i, typePtr,seg.numPages * kbPerPage,
		       seg.resPages * kbPerPage, seg.refCount, fileNamePtr);
    }
    printf("\n%-20s%-15s%-15s\n\n", "TYPE", "NUM-SEGS", "RES-SIZE");
    printf("%-20s%-15d%-15d\n", "Active-code", actCodeSegs, actCodePages);
    printf("%-20s%-15d%-15d\n", "Inactive-code", inactCodeSegs,
	     inactCodePages);
    printf("%-20s%-15d%-15d\n", "Heap", heapSegs, heapPages);
    printf("%-20s%-15d%-15d\n", "Stack", stackSegs, stackPages);
    printf("%-20s%-15d%-15d\n", "TOTAL", 
	     actCodeSegs + inactCodeSegs + heapSegs + stackSegs,
	 (actCodePages + inactCodePages + heapPages + stackPages) * kbPerPage);
}

static Vm_Stat	prevStat;


/*
 *----------------------------------------------------------------------
 *
 * PrintWide --
 *
 *	Print a one summary of vm stats.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints information on standard output.
 *
 *----------------------------------------------------------------------
 */
PrintWide()
{
    static	int	numLines = 0;
    static	int	numCalls = 0;
    Vm_Stat		vmStat;

    (void)Vm_Cmd(VM_GET_STATS, &vmStat);

    if (checkTime != 0 && numCalls != maxTimes) {
	if ((vmStat.totalFaults - prevStat.totalFaults) / checkTime < 
		faultThreshold &&
	    (vmStat.pagesWritten - prevStat.pagesWritten) / checkTime < 
	        pageOutThreshold) {
	    if (relToLastPrint) {
		bcopy((Address)&vmStat, (Address)&prevStat, sizeof(Vm_Stat));
	    }
	    numCalls++;
	    return;
	}
    }
    numCalls = 0;
    if (numLines % whenToPrintHdr == 0) {
	if (numLines > 0) {
	    printf("\n");
	}
	if (printMod) {
	printf("%-8s%-8s%-8s%-8s%-8s%-8s%-8s%-8s%-8s%-8s",
	    " AVAIL", "  FREE", "  USER", " MOD%", " KMEM",
	    "  FS$", "PF-NUM",
	    "PF-SWP", " PF-FS", " POUTS");
	} else {
	    printf("%-8s%-8s%-8s%-8s%-8s%-8s%-8s%-8s%-8s%-8s",
		" AVAIL", "  FREE", "  USER",  " KMEM",
		" KSTK", "  FS$", "PF-NUM",
		"PF-SWP", " PF-FS", " POUTS");
	}
    }
    numLines++;

    if (printMod) {
	int	numModifiedPages;

	(void)Vm_Cmd(VM_COUNT_DIRTY_PAGES, &numModifiedPages);
	printf("\n%6d%8d%8d%8.2f%7d%8d%9d%8d%8d%8d",
		 vmStat.numPhysPages * kbPerPage,
		 vmStat.numFreePages * kbPerPage, 
		 (vmStat.numDirtyPages + vmStat.numUserPages) * kbPerPage,
		 (float)numModifiedPages /
		   (float)(vmStat.numDirtyPages + vmStat.numUserPages) * 100.0,
		 (vmStat.kernMemPages + vmStat.kernStackPages) * kbPerPage, 
		 (vmStat.fsMap - vmStat.fsUnmap) * kbPerPage,
		 vmStat.totalFaults - prevStat.totalFaults, 
		 vmStat.psFilled - prevStat.psFilled,
		 vmStat.fsFilled - prevStat.fsFilled,
		 vmStat.pagesWritten - prevStat.pagesWritten);
    } else {
	printf("\n%6d%8d%8d%7d%8d%8d%9d%8d%8d%8d",
		 vmStat.numPhysPages * kbPerPage,
		 vmStat.numFreePages * kbPerPage, 
		 (vmStat.numDirtyPages + vmStat.numUserPages) * kbPerPage,
		 vmStat.kernMemPages * kbPerPage, 
		 vmStat.kernStackPages * kbPerPage,
		 (vmStat.fsMap - vmStat.fsUnmap) * kbPerPage,
		 vmStat.totalFaults - prevStat.totalFaults, 
		 vmStat.psFilled - prevStat.psFilled,
		 vmStat.fsFilled - prevStat.fsFilled,
		 vmStat.pagesWritten - prevStat.pagesWritten);
    }
    bcopy((Address)&vmStat, (Address)&prevStat, sizeof(Vm_Stat));
    fflush(stdout);
}


/*
 *----------------------------------------------------------------------
 *
 * PrintSummary --
 *
 *	Print a verbose summary of all vm statistics.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Prints information on standard output.
 *
 *----------------------------------------------------------------------
 */
PrintSummary()
{
    int			actCodePages = 0;
    int			inactCodePages = 0;
    int			actCodeSegs = 0;
    int			inactCodeSegs = 0;
    int			heapPages = 0;
    int			heapSegs = 0;
    int			stackPages = 0;
    int			stackSegs = 0;
    Vm_SegmentInfo	seg;
    int			i;
    Vm_Stat 		vmStat;
    int			totPages;
    int			inusePages;
    int			numModifiedPages;
    int			totPercent;
    int			totFaults;
    int			heapPercent;
    int			stkPercent;
    int			quickPercent;	
    int			totHits;
    int			totPrefetches;
    int			hitPct;
    ReturnStatus	status;

    Vm_Cmd(VM_GET_STATS, &vmStat);
    Vm_Cmd(VM_COUNT_DIRTY_PAGES, &numModifiedPages);
    for (i = 1; i < NUM_SEGMENTS; i++) {
	status = Vm_GetSegInfo(NULL, i, sizeof(Vm_SegmentInfo), &seg);
	if (status != SUCCESS) {
	    (void) fprintf(stderr,
			   "Couldn't read segment info for segment %d: %s\n",
			   i,  Stat_GetMsg(status));
	    return;
	}
	if (seg.flags & VM_SEG_FREE) {
	    continue;
	}
	switch (seg.type) {
	    case VM_CODE:
	        if (seg.flags & VM_SEG_INACTIVE) {
		    inactCodePages += seg.resPages;
		    inactCodeSegs++;
		} else {
		    actCodePages += seg.resPages;
		    actCodeSegs++;
		}
		break;
	    case VM_HEAP:
		heapPages += seg.resPages;
		heapSegs++;
		break;
	    case VM_STACK:
		stackPages += seg.resPages;
		stackSegs++;
		break;
	}
    }
    printf("MEMORY STATS:\n");
    printf("\tPage Size:\t%-8d\n", 1024 * kbPerPage);
    printf("\tMemory Size:\t%-8d\n", vmStat.numPhysPages * kbPerPage);
    printf("\tKernel Memory:\t%-8d (Code+Data=%d Stacks=%d Reserved=%d)\n",
	     (vmStat.kernMemPages + vmStat.kernStackPages +
		    vmStat.numReservePages) * kbPerPage,
	     vmStat.kernMemPages * kbPerPage, 
	     vmStat.kernStackPages * kbPerPage,
	     vmStat.numReservePages * kbPerPage);
    inusePages = vmStat.numDirtyPages + vmStat.numUserPages;
    printf("\tUser Memory:\t%-8d (Dirty=%d Clean=%d)\n",
	    inusePages * kbPerPage, 
	    numModifiedPages * kbPerPage, 
	    (inusePages - numModifiedPages) * kbPerPage);
    printf("\tFS Memory:\t%-8d (Min=%d Max=%d)\n", 
		(vmStat.fsMap - vmStat.fsUnmap) * kbPerPage, 
		vmStat.minFSPages * kbPerPage,
		vmStat.maxFSPages * kbPerPage);
    printf("\tFree Memory:\t%d\n", vmStat.numFreePages * kbPerPage);
    if (verbose) {
	printf("\tMod page stats:\tPot-mod=%d Not-mod=%d Not-hard-mod=%d\n",
	    vmStat.potModPages, vmStat.notModPages, vmStat.notHardModPages);
    }
    printf("SEGMENT STATS:\n");
    printf("\tAct-code:\tNum-segs=%-3d Res-size=%d\n", actCodeSegs,
					      actCodePages * kbPerPage);
    printf("\tInact-code:\tNum-segs=%-3d Res-size=%d\n", inactCodeSegs,
					      inactCodePages * kbPerPage);
    printf("\tHeap:\t\tNum-segs=%-3d Res-size=%d\n", heapSegs, 
					      heapPages * kbPerPage);
    printf("\tStack:\t\tNum-segs=%-3d Res-size=%d\n", stackSegs, 
					      stackPages*kbPerPage);
    printf("FAULT STATS:\n");
    printf("\tTotal:\t\t%d\n", vmStat.totalFaults);
    printf("\tFault-type:\tZero=%-6d FS=%-8d Swap=%-9d Quick=%d\n", 
	     vmStat.zeroFilled, vmStat.fsFilled,
	     vmStat.psFilled, vmStat.quickFaults -
		(vmStat.numCOWStkFaults + vmStat.numCOWHeapFaults +
		 vmStat.numCORStkFaults + vmStat.numCORHeapFaults));
    printf("\t\t\tCOW=%-7d COR=%-7d COR-mod=%-5d\n",
	     vmStat.numCOWStkFaults + vmStat.numCOWHeapFaults,
	     vmStat.numCORStkFaults + vmStat.numCORHeapFaults,
	     vmStat.numCORCOWStkFaults + vmStat.numCORCOWHeapFaults);
    printf("\tSeg-type:\tCode=%-6d Heap=%-6d Stack=%-6d\n", 
	     vmStat.codeFaults, vmStat.heapFaults, vmStat.stackFaults);
    printf("\tCollisions:\t%d\n", vmStat.collFaults);
    printf("PAGE-OUTS:\n");
    printf("\tPages written:\t%d\n", vmStat.pagesWritten);
    if (verbose) {
	printf("\tClean Wait:\t%d\n", vmStat.cleanWait);
	printf("\tCleaner starts:\t%d\n", vmStat.pageoutWakeup);
    }
    printf("COPY-ON-WRITE:\n");
    totPages = vmStat.numCOWStkPages + vmStat.numCOWHeapPages;
    totFaults = vmStat.numCOWStkFaults + vmStat.numCOWHeapFaults;
    if (vmStat.numCOWHeapPages > 0) {
	heapPercent = 100.0 * ((float)vmStat.numCOWHeapFaults / 
				      vmStat.numCOWHeapPages);
    } else {
	heapPercent = 0;
    }
    if (vmStat.numCOWStkPages > 0) {
	stkPercent = 100.0 * ((float)vmStat.numCOWStkFaults / 
				      vmStat.numCOWStkPages);
    } else {
	stkPercent = 0;
    }
    if (totPages > 0) {
	totPercent = 100.0 * ((float)totFaults / totPages);
    } else {
	totPercent = 0;
    }
    if (totFaults > 0) {
	quickPercent = 100.0 * ((float)vmStat.quickCOWFaults / totFaults);
    } else {
	quickPercent = 0;
    }
    printf("\tCOW:\t\tHeap (%d/%d)=%d%%\tStack (%d/%d)=%d%%\n",
	    vmStat.numCOWHeapFaults, vmStat.numCOWHeapPages, heapPercent,
	    vmStat.numCOWStkFaults, vmStat.numCOWStkPages, stkPercent);
    printf("\t\t\tTot  (%d/%d)=%d%%\tQuick (%d/%d)=%d%%\n",
	    totFaults, totPages, totPercent,
	    vmStat.quickCOWFaults, totFaults, quickPercent);
    /*
     * Copy on reference.
     */
    totPages = vmStat.numCORStkPages + vmStat.numCORHeapPages;
    totFaults = vmStat.numCORStkFaults + vmStat.numCORHeapFaults;
    if (vmStat.numCORHeapPages > 0) {
	heapPercent = 100.0 * ((float)vmStat.numCORHeapFaults / 
				      vmStat.numCORHeapPages);
    } else {
	heapPercent = 0;
    }
    if (vmStat.numCORStkPages > 0) {
	stkPercent = 100.0 * ((float)vmStat.numCORStkFaults / 
				      vmStat.numCORStkPages);
    } else {
	stkPercent = 0;
    }
    if (totPages > 0) {
	totPercent = 100.0 * ((float)totFaults / totPages);
    } else {
	totPercent = 0;
    }
    printf("\tCOR:\t\tHeap (%d/%d)=%d%%\tStack (%d/%d)=%d%%\n",
	    vmStat.numCORHeapFaults, vmStat.numCORHeapPages, heapPercent,
	    vmStat.numCORStkFaults, vmStat.numCORStkPages, stkPercent,
	    totFaults, totPages, totPercent);
    printf("\t\t\tTot  (%d/%d)=%d%%\n",
	    totFaults, totPages, totPercent);
    totPages = vmStat.numCORStkFaults + vmStat.numCORHeapFaults;
    totFaults = vmStat.numCORCOWStkFaults + vmStat.numCORCOWHeapFaults;
    if (vmStat.numCORCOWHeapFaults > 0) {
	heapPercent = 100.0 * ((float)vmStat.numCORCOWHeapFaults / 
				      vmStat.numCORHeapFaults);
    } else {
	heapPercent = 0;
    }
    if (vmStat.numCORCOWStkFaults > 0) {
	stkPercent = 100.0 * ((float)vmStat.numCORCOWStkFaults / 
				      vmStat.numCORStkFaults);
    } else {
	stkPercent = 0;
    }
    if (totPages > 0) {
	totPercent = 100.0 * ((float)totFaults / totPages);
    } else {
	totPercent = 0;
    }
    if (vmStat.numCORCOWHeapFaults > 0) {
	printf("\tCOR-mod:\tHeap (%d/%d)=%d%%\tStack (%d/%d)=%d%%\n",
	    vmStat.numCORCOWHeapFaults, vmStat.numCORHeapFaults, heapPercent,
	    vmStat.numCORCOWStkFaults, vmStat.numCORStkFaults, stkPercent,
	    vmStat.numCORCOWHeapFaults + vmStat.numCORCOWStkFaults);
	printf("\t\t\tTot  (%d/%d)=%d%%\n",
	    vmStat.numCORCOWHeapFaults + vmStat.numCORCOWStkFaults,
	    vmStat.numCORHeapFaults + vmStat.numCORStkFaults, totPercent);
    }
    if (verbose) {
	printf("ALLOCATION STATS:\n");
	printf("\tVm allocs:\t%d\t(Free=%d From-FS=%d From-alloc-list=%d)\n",
		 vmStat.numAllocs, vmStat.gotFreePage, vmStat.gotPageFromFS, 
		 vmStat.pageAllocs);
	printf("\tVM-FS stats:\tFS-asked=%d Had-free-page=%d\n",
		 vmStat.fsAsked, vmStat.haveFreePage);
	printf("\t\t\tFS-map=%d FS-unmap=%d\n", vmStat.fsMap, vmStat.fsUnmap);
	printf("\tList searches:\t%d (Free=%d In-use=%d)\n",
		 vmStat.numListSearches, vmStat.usedFreePage, 
		 vmStat.numListSearches - vmStat.usedFreePage);
	printf("\tExtra-searches:\t%d (Lock=%d Ref=%d Dirty=%d)\n",
		 vmStat.lockSearched + vmStat.refSearched + vmStat.dirtySearched,
		 vmStat.lockSearched, vmStat.refSearched, vmStat.dirtySearched);
	printf("LIST STATS:\n");
	printf("\tAlloc-list:\t%d\n", vmStat.numUserPages);
	printf("\tDirty-list:\t%d\n", vmStat.numDirtyPages);
	printf("\tFree-list:\t%d\n", vmStat.numFreePages);
	printf("\tReserve-list:\t%d\n", vmStat.numReservePages);
#ifdef notdef
	totPrefetches = vmStat.codePrefetches + vmStat.heapFSPrefetches +
			vmStat.heapSwapPrefetches + vmStat.stackPrefetches;
	if (totPrefetches > 0) {
	    totHits = vmStat.codePrefetchHits + vmStat.heapFSPrefetchHits +
		      vmStat.heapSwapPrefetchHits + vmStat.stackPrefetchHits;
	    printf("Prefetch stats:\n");
	    if (vmStat.codePrefetches > 0) {
		hitPct = 100 * ((float)vmStat.codePrefetchHits / 
				(float)vmStat.codePrefetches);
		printf("    code (%d/%d)=%d%%\n",
			vmStat.codePrefetchHits, vmStat.codePrefetches, hitPct);
	    }
	    if (vmStat.heapFSPrefetches > 0) {
		hitPct = 100 * ((float)vmStat.heapFSPrefetchHits / 
				(float)vmStat.heapFSPrefetches);
		printf("    heap-fs (%d/%d)=%d%%\n",
		    vmStat.heapFSPrefetchHits, vmStat.heapFSPrefetches, hitPct);
	    }
	    if (vmStat.heapSwapPrefetches > 0) {
		hitPct = 100 * ((float)vmStat.heapSwapPrefetchHits / 
				(float)vmStat.heapSwapPrefetches);
		printf("    heap-swp (%d/%d)=%d%%\n",
		    vmStat.heapSwapPrefetchHits, vmStat.heapSwapPrefetches, 
		    hitPct);
	    }
	    if (vmStat.stackPrefetches > 0) {
		hitPct = 100 * ((float)vmStat.stackPrefetchHits / 
				(float)vmStat.stackPrefetches);
		printf("    stack (%d/%d)=%d%%\n",
		    vmStat.stackPrefetchHits, vmStat.stackPrefetches, hitPct);
	    }
	    hitPct = 100 * ((float)totHits / (float)totPrefetches);
	    printf("    total (%d/%d)=%d%%\n",
		    totHits, totPrefetches, hitPct);
	    printf("    aborts=   %d\n", vmStat.prefetchAborts);
	}
#endif
	printf("PAGE MAPPING STATS:\n");
	printf("\tMap page wait:\t%d\n", vmStat.mapPageWait);
#ifdef sun2
	printf("HARDWARE_STATS:\n");
	printf("\tCtxts stolen:\t%d\n", vmStat.machDepStat.stealContext);
	printf("\tPmegs stolen:\t%d\n", vmStat.machDepStat.stealPmeg);
#endif
#ifdef sun3
	printf("HARDWARE_STATS:\n");
	printf("\tCtxts stolen:\t%d\n", vmStat.machDepStat.stealContext);
	printf("\tPmegs stolen:\t%d\n", vmStat.machDepStat.stealPmeg);
#endif
#ifdef sun4
	printf("HARDWARE_STATS:\n");
	printf("\tCtxts stolen:\t%d\n", vmStat.machDepStat.stealContext);
	printf("\tPmegs stolen:\t%d\n", vmStat.machDepStat.stealPmeg);
#endif
#ifdef spur
	printf("HARDWARE_STATS:\n");
	printf("\tRef bit faults:\t%d\n", vmStat.machDepStat.refBitFaults);
	printf("\tDirty bit faults:\t%d\n", vmStat.machDepStat.dirtyBitFaults);
#endif
#ifdef ds3100
	printf("HARDWARE_STATS:\n");
#ifdef notdef 
	/* 
	 * Not currently implemented by kernel.
	 */
	printf("\tTlb entries stolen:\t%d\n", vmStat.machDepStat.stealTLB);
#endif
	printf("\tPids stolen:\t%d\n", vmStat.machDepStat.stealPID);
#endif
    }
}

