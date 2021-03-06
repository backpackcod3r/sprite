/* 
 * fsstat.c --
 *
 *	Print out the statistics kept for the filesystem.
 *
 * Copyright (C) 1986 Regents of the University of California
 * All rights reserved.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/cmds/fsstat/RCS/fsstat.c,v 1.8 92/05/15 11:10:08 kupfer Exp $ SPRITE (Berkeley)";
#endif not lint

#include <sprite.h>
#include <fs.h>
#include <fsCmd.h>
#include <stdio.h>
#include <option.h>
#include <vm.h>
#include <sysStats.h>
#include <kernel/vm.h>
#include <kernel/fs.h>
#include <kernel/fsStat.h>

Boolean deleteHist = FALSE;
Boolean printHostInfo = FALSE;
/*
 * Parameters for printing histogram.  This will catch all times we'll
 * encounter and will fit everything in an 80-column screen.  The program
 * will still check for non-zero values outside this region and print
 * a warning if one is encountered.  DEFAULT_WIDTH is 79 since we'd rather
 * not print in the last column, and each entry takes 5 chars.
 */
 
#define DEFAULT_WIDTH 79

int printExtraFileStats = FALSE;
int numTimesToPrint = FS_HIST_TIME_BUCKETS; /* by default, print everything. */
int numSizesToPrint = DEFAULT_WIDTH/5;

Option optionArray[] = {
    {OPT_TRUE, "d", (Address)&deleteHist, "Print deletion histogram"},
    {OPT_TRUE, "H", (Address)&printHostInfo, "Print kernel version and uptime info"},
    {OPT_TRUE, "F", (Address)&printExtraFileStats, "Print file life-time info"},
    {OPT_INT, "T", (Address)&numTimesToPrint,
	     "Number of times for which to print data"},
    {OPT_INT, "S", (Address)&numSizesToPrint,
	     "Number of sizes for which to print data"},
};
int numOptions = sizeof(optionArray) / sizeof(Option);

ReturnStatus status;

Fs_Stats fsStats;
Vm_Stat	vmStats;
Fs_TypeStats fsTypeStats;

int GetRatio();

#define PrintRatio(x,y) printf("  %11d %3d%%", (x), GetRatio((x), (y)))

main(argc, argv)
    int argc;
    char *argv[];
{
    int status = SUCCESS;
    register int t1;
    int i;
    unsigned int vmBlocksWritten;
    unsigned int vmBlocksRead;
    int pageSize;
    char    	  version[128];
    char    	  hostname[128];
    double overflowFactor;

    argc = Opt_Parse(argc, argv, optionArray, numOptions);

    /*
     * For "pseudo-double" variables -- two integers -- we need to store
     * them in this program as real double variables.  The problem is,
     * we can't just convert 0x80000000 from an unsigned int to float, because
     * our C compiler goofs and makes the result negative.  Therefore, some
     * tricks to take the FS_STAT_OVERFLOW constant and turn it into a number
     * we can use to multiply by the overflow counter.  Shift it right, then
     * multiply it by the equivalent number once it's a "double".  
     *
     * Note: the definition of FS_STAT_OVERFLOW confuses the gcc compiler
     *	and results in a negative value of overflowFactor if used directly,
     *	even after shifting and multiplying.  The definition is:
     *	FS_STAT_OVERFLOW (1 << (sizeof(unsigned int) * 8 - 1))
     */
    overflowFactor = (0x80000000 >> 1) * 2.0;
    
    if (printHostInfo) {
	if (Sys_Stats(SYS_GET_VERSION_STRING, sizeof(version), version) ==
	    SUCCESS){
	    printf("Kernel version: %s\n", version);
	}
	fflush(stdout);
	(void)system("date;loadavg");
    }

    status = Fs_Command(FS_RETURN_STATS, sizeof(Fs_Stats), &fsStats);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Fs_Command failed");
	exit(status);
    }
    status = Vm_Cmd(VM_GET_STATS, &vmStats);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Vm_Cmd failed");
	exit(status);
    }
    Vm_PageSize(&pageSize);
    vmBlocksWritten = vmStats.pagesWritten * (pageSize / FS_BLOCK_SIZE);
    if (printExtraFileStats) {
	status = Fs_Command(FS_RETURN_LIFE_TIMES, sizeof(Fs_TypeStats), &fsTypeStats);
	if (status != SUCCESS) {
	    fprintf(stderr, "This kernel doesn't do FS_RETURN_LIFE_TIMES\n");
	    printExtraFileStats = 0;
	}
    }


  {
    register Fs_BlockCacheStats *block;
    
    block = &fsStats.blockCache;
    printf("Block Cache, %.2f Mbytes\n", (float)block->numCacheBlocks *
				FS_BLOCK_SIZE / (1024 * 1024));
    printf(" BLOCKS %u min %u max %u(%u) free %u\n",
		block->numCacheBlocks, block->minCacheBlocks,
		block->maxCacheBlocks, block->maxNumBlocks,
		block->numFreeBlocks);
    t1 = block->readHitsOnDirtyBlock + block->readHitsOnCleanBlock;
    printf(" READS   %8u dr_hits %8u cl_hits  %8u\t\thit ratio %u\n",
		block->readAccesses,
		block->readHitsOnDirtyBlock,
		block->readHitsOnCleanBlock,
		100 * t1 / block->readAccesses);
    printf(" WRITES  %8u  p-hits %8u p-misses %8u\t\ttfc ratio %u\n",
		block->writeAccesses,
		block->partialWriteHits,
		block->partialWriteMisses,
		block->writeAccesses ?
		100 * block->blocksWrittenThru / block->writeAccesses : 0);
    printf(" WRITETHRU %u ", block->blocksWrittenThru);
    if (block->blocksWrittenThru) {
	printf("data %u %u%% index %u %u%% desc %u %u%% dir %u %u%% vm %u %u%%\n",
		block->dataBlocksWrittenThru,
		100 * block->dataBlocksWrittenThru / block->writeAccesses,
		block->indBlocksWrittenThru,
		100 * block->indBlocksWrittenThru / block->writeAccesses,
		block->descBlocksWrittenThru,
		100 * block->descBlocksWrittenThru / block->writeAccesses,
		block->dirBlocksWrittenThru,
		100 * block->dirBlocksWrittenThru / block->writeAccesses,
		vmBlocksWritten,
		100 * vmBlocksWritten / block->writeAccesses);
    }
    printf(" ZERO FILL read %u write %u/%u append %u over %u\n",
		block->readZeroFills,
		block->writeZeroFills1, block->writeZeroFills2,
		block->appendWrites,
		block->overWrites);
    printf(" READ AHEAD %u hits %u all in cache %u/%u\t\t\thit ratio %u\n",
		block->readAheads,
		block->readAheadHits,
		block->allInCacheCalls,
		block->allInCacheTrue,
		block->readAheads ?
		100 * block->readAheadHits / block->readAheads : 0);
    printf(" FRAGMENT upgrades %u hits %u zero fills %u\n",
		block->fragAccesses,
		block->fragHits,
		block->fragZeroFills);
    if (block->fileDescReads > 0) {
	printf(" FILE DESC reads %u hits %u (%2u%%) writes %u hits %u (%2u%%)\n", 
		    block->fileDescReads, block->fileDescReadHits,
		    100 * block->fileDescReadHits / block->fileDescReads,
		    block->fileDescWrites, block->fileDescWriteHits,
		    block->fileDescWrites ?
			100 * block->fileDescWriteHits / block->fileDescWrites
			: 0);
    }
    if (block->indBlockAccesses > 0) {
	printf(" INDIRECT BLOCKS  Accesses %u hits %u\t\t\thit ratio %u\n", 
		    block->indBlockAccesses, block->indBlockHits,
		    100 * block->indBlockHits / block->indBlockAccesses);
    }
    if (block->dirBlockAccesses > 0) {
	printf(" DIRECTORY BLOCKS Accesses %u hits %u\t\t\thit ratio %u\n", 
		    block->dirBlockAccesses, block->dirBlockHits,
		    100 * block->dirBlockHits / block->dirBlockAccesses);
    }
    printf(" VM asked %u we tried %u gave up %u Pitched %u\n",
		block->vmRequests, block->triedToGiveToVM, block->vmGotPage,
		block->blocksPitched);
    printf(" ALLOC from free %u new %u lru %u part free %u\n",
		block->totFree, block->unmapped,
		block->lru, block->partFree);
  }
  {
    register Fs_AllocStats *alloc = &fsStats.alloc;
    if (alloc->blocksAllocated > 0) {
	printf("Block Allocation Statistics:\n");
	printf(" BLOCKS alloc %u free %u\n", alloc->blocksAllocated,
		   alloc->blocksFreed);
	printf(" CYLINDERS searched %u hashes %u bit-searches %u\n",
		   alloc->cylsSearched, alloc->cylHashes,
		   alloc->cylBitmapSearches);
	printf(
" FRAGS alloc %u free %u fr->block %u block->fr %u upgrades %u bad hints %u\n",
		   alloc->fragsAllocated, alloc->fragsFreed,
		   alloc->fragToBlock, alloc->fullBlockFrags,
		   alloc->fragUpgrades, alloc->badFragList);
    }
  }
  {
    register Fs_NameOpStats *cltName = &fsStats.cltName;
    printf("Client Naming Operations:\n");
    printf(" Open R %u W %u RW %u chdir %d\n",
		cltName->numReadOpens, cltName->numWriteOpens,
		cltName->numReadWriteOpens,
		cltName->chdirs);
    printf(" Make dir %u dev %u hardLink %u symLink %u\n",
		cltName->makeDirs, cltName->makeDevices,
		cltName->hardLinks, cltName->symLinks);
    printf(" Remove %u rename %u rmdir %u\n",
		cltName->removes, cltName->renames,
		cltName->removeDirs);
    printf(" Get attr name %u stream %u Set attr name %u stream %u\n",
		cltName->getAttrs, cltName->getAttrIDs,
		cltName->setAttrs, cltName->setAttrIDs);
  }
  {
    register Fs_NameOpStats *srvName = &fsStats.srvName;
    if (srvName->numReadOpens > 0) {
	printf("Server Naming Operations:\n");
	printf(" Opens %u\n",
		    srvName->numReadOpens);
	printf(" Make dir %u dev %u hardLink %u symLink %u\n",
		    srvName->makeDirs, srvName->makeDevices,
		    srvName->hardLinks, srvName->symLinks);
	printf(" Remove %u rename %u rmdir %u\n",
		    srvName->removes, srvName->renames,
		    srvName->removeDirs);
	printf(" Get attr name %u stream %u Set attr name %u stream %u\n",
		    srvName->getAttrs, srvName->getAttrIDs,
		    srvName->setAttrs, srvName->setAttrIDs);
	printf(" Get I/O attr %u Set I/O attr %u\n",
		    srvName->getIOAttrs, srvName->setIOAttrs);
    }
  }
  {
    register Fs_LookupStats *lookup = &fsStats.lookup;
    register Fs_NameCacheStats *name = &fsStats.nameCache;
    register int lookupMisses;
    if (lookup->number > 0) {
	printf("Name Lookup Statistics:\n");
	lookupMisses = lookup->notFound + lookup->redirect +
			 lookup->remote + lookup->parent;
	printf(" Paths %u hits %d (%u%%) components %u avg %.2f $MACHINE %u\n",
		lookup->number, lookup->number - lookupMisses,
		100 * (lookup->number - lookupMisses) / lookup->number,
		lookup->numComponents,
		(float)lookup->numComponents / (float)lookup->number,
		lookup->numSpecial);
	printf(" For create %u delete %u link %u rename %u\n",
		lookup->forCreate, lookup->forDelete,
		lookup->forLink, lookup->forRename);
	printf(" Not found %u link expand %u redirect %u remote %u parent %u\n",
		lookup->notFound, lookup->symlinks,
		lookup->redirect, lookup->remote, lookup->parent);
    }
    if (name->accesses > 0) {
	printf(" Name Cache tries %u hits %u (%u%%) replaced %u (%u%%) size %u\n",
		   name->accesses, name->hits,
		   100 * name->hits / name->accesses,
		   name->replacements,
		   100 * name->replacements / name->accesses,
		   name->size);
    }
  }
  {
    register Fs_HandleStats *handle = &fsStats.handle;
    printf("File Handle Statistics:\n");
    printf(" Number %u create %u install %u hits %u version %u flush %u\n",
	       handle->exists, handle->created, handle->installCalls,
	       handle->installHits, handle->versionMismatch,
	       handle->cacheFlushes);
    printf(" Fetch %u hits %u lock %u waits %u unlock %u release %u\n",
	       handle->fetchCalls, handle->fetchHits, handle->locks,
	       handle->lockWaits, handle->unlocks, handle->release);
    printf(" Segment fetches %u hits %u\t\t\t\thit ratio %u\n",
	       handle->segmentFetches, handle->segmentHits,
	       GetRatio(handle->segmentHits, handle->segmentFetches));
  }
  {
    register Fs_PrefixStats *prefix = &fsStats.prefix;
    printf("Prefix Statistics:\n");
    printf(" Absolute %u relative %u redirect %u loop %u timeout %u stale %u found %u\n",
	       prefix->absolute, prefix->relative, prefix->redirects,
	       prefix->loops, prefix->timeouts, prefix->stale, prefix->found);
  }

  {
    unsigned int	numBytes;
    unsigned int	arr[3];

    status = Fs_Command(FS_GET_FRAG_INFO, sizeof(arr), arr);
    if (status != SUCCESS) {
	Stat_PrintMsg(status, "Call to Fs_Command for frag info failed");
	exit(status);
    }
    numBytes = arr[0] * FS_BLOCK_SIZE;
    printf("Internal fragmentation statistics: Total bytes %u\n", numBytes);
    printf(" Total bytes wasted   %7u percent %u\n",
	    arr[1], arr[1] * 100 / numBytes);
    printf(" Waste relative to 1K %7u percent %u\n",
	    arr[2], arr[2] * 100 / numBytes);
  }
#define NUM_BYTE_COUNT_COLUMNS 4

  {
      register Fs_GeneralStats *gen = &fsStats.gen;
      double cacheBytes, thruBytes;
      double rmtRatio, diskRatio;
    
      printf("Bytes:      cache     remote           disk       raw disk        devices\n");
      cacheBytes = fsStats.blockCache.bytesRead +
	      overflowFactor * fsStats.blockCache.bytesReadOverflow;
      thruBytes = gen->remoteBytesRead +
	      overflowFactor * gen->remoteReadOverflow;
      rmtRatio = (cacheBytes > 0) ? (100. * thruBytes / cacheBytes) : 0;
      printf(" Mb Read  %8.2f %8.2f %3d%%", cacheBytes / (1024 * 1024),
	       thruBytes / (1024 * 1024), (unsigned int)rmtRatio);

      thruBytes = gen->fileBytesRead + overflowFactor * gen->fileReadOverflow;
      diskRatio = (cacheBytes > 0) ? (100. * thruBytes / cacheBytes) : 0;
      printf(" %8.2f %3d%%", thruBytes / (1024 * 1024),
			      (unsigned int)diskRatio);

      thruBytes = gen->physBytesRead;
      diskRatio = (cacheBytes > 0) ? (100. * thruBytes / cacheBytes) : 0;

      printf(" %8.2f %3d%%", thruBytes / (1024 * 1024),
			      (unsigned int)diskRatio);

      printf(" %8.2f\n", (float)gen->deviceBytesRead / (1024 * 1024));

      cacheBytes = fsStats.blockCache.bytesWritten +
	      overflowFactor * fsStats.blockCache.bytesWrittenOverflow;
      thruBytes = gen->remoteBytesWritten +
	      overflowFactor * gen->remoteWriteOverflow;
      if (cacheBytes > 0) {
	  rmtRatio = 100. * thruBytes / cacheBytes;
      } else {
	  rmtRatio = 0.;
      }
      printf(" Mb Write %8.2f %8.2f %3d%%", cacheBytes / (1024 * 1024),
	       thruBytes / (1024 * 1024), (unsigned int)rmtRatio);
      thruBytes = gen->fileBytesWritten +
	      overflowFactor * gen->fileWriteOverflow;
      if (fsStats.blockCache.bytesWritten > 0) {
	  diskRatio = 100. * thruBytes / cacheBytes;
      } else {
	  diskRatio = 0.;
      }
      printf(" %8.2f %3d%%", thruBytes / (1024 * 1024),
			  (unsigned int)diskRatio);

      thruBytes = gen->physBytesWritten;
      if (cacheBytes > 0) {
	  diskRatio = 100. * thruBytes / cacheBytes;
      } else {
	  diskRatio = 0.;
      }
      printf(" %8.2f %3d%%", thruBytes / (1024 * 1024), (unsigned int)diskRatio);
      printf(" %8.2f\n", (float)gen->deviceBytesWritten / (1024 * 1024));

      printf("OBJECTS stream %d (clt %d) file %d dir %d rmtFile %d ioClt %d\n",
	    fsStats.object.streams, fsStats.object.streamClients,
	    fsStats.object.files, fsStats.object.directory,
	    fsStats.object.rmtFiles, fsStats.object.fileClients);
      printf("OBJECTS  pipe %d dev %d pdevCtrl %d pdev %d remote %d Total %d\n",
	    fsStats.object.pipes, fsStats.object.devices,
	    fsStats.object.controls,
	    fsStats.object.pseudoStreams, fsStats.object.remote,
	    fsStats.object.streams + fsStats.object.files +
	    fsStats.object.directory +
	    fsStats.object.rmtFiles + fsStats.object.pipes +
	    fsStats.object.devices + fsStats.object.controls +
	    2 * fsStats.object.pseudoStreams + fsStats.object.remote);
      printf("HANDLES %d (%d) limbo %d scav %d (dirs %d) looks/scav %.2f\n",
	    fsStats.handle.exists,
	    fsStats.handle.maxNumber,
	    fsStats.handle.limbo,
	    fsStats.handle.lruHits,
	    fsStats.object.dirFlushed,
	     (fsStats.handle.lruScans != 0
	      ? ((float)fsStats.handle.lruChecks /
		 (float)fsStats.handle.lruScans)
	      : 0.0));
  }
  {
      register Fs_RecoveryStats *recovPtr = &fsStats.recovery;
      if (recovPtr->number > 0 || recovPtr->wants > 0) {
	  printf("RECOVERED %d times, wants %d ok %d bad %d abort %d\n",
	      recovPtr->number, recovPtr->wants, recovPtr->waitOK,
	      recovPtr->waitFailed, recovPtr->waitAbort);
	  printf("RECOVERED %d handles, %d failed %d timed out\n",
	      recovPtr->succeeded, recovPtr->failed, recovPtr->timeout);
      }
      if ((recovPtr->clientRecovered + recovPtr->clientCrashed) > 0) {
	  printf("CLIENTS %d crashed, %d reopened\n", recovPtr->clientCrashed,
	      recovPtr->clientRecovered);
      }
  }
  /*
   * ONLY FILE TYPE I/O AND DELETION STUFF BELOW HERE
   */
  if (!printExtraFileStats) {
      exit(status);
  } else {
      register Fs_TypeStats *type = &fsTypeStats;
      register Fs_GeneralStats *gen = &fsStats.gen;
      int ratio;
      static char *typeStrings[] = { "temp", "swap", "obj", "bin", "other"};
      /*
       * # of columns for type-specific counts
       */
      unsigned int byteTotals[NUM_BYTE_COUNT_COLUMNS];


      printf(
	       "File type       Cache(R)          Cache(W)           Disk(R)           Disk(W)");

      /*
       * Calculate the totals for each column so we can get accurate fractions
       * on a per-filetype basis.
       */
      
      for (i = 0; i < NUM_BYTE_COUNT_COLUMNS; i++) {
	  byteTotals[i] = 0;
      }
      for (i = 0; i < FS_STAT_NUM_TYPES; i++) {
	  byteTotals [0] += type->cacheBytes[FS_STAT_READ][i];
	  byteTotals [1] += type->cacheBytes[FS_STAT_WRITE][i];
	  byteTotals [2] += type->diskBytes[FS_STAT_READ][i];
	  byteTotals [3] += type->diskBytes[FS_STAT_WRITE][i];
      }

      for (i = 0; i < FS_STAT_NUM_TYPES; i++) {
	  printf("\n%-6s ", typeStrings[i]);
	  PrintRatio(type->cacheBytes[FS_STAT_READ][i], byteTotals[0]);
	  PrintRatio(type->cacheBytes[FS_STAT_WRITE][i], byteTotals[1]);
	  PrintRatio(type->diskBytes[FS_STAT_READ][i], byteTotals[2]);
	  PrintRatio(type->diskBytes[FS_STAT_WRITE][i], byteTotals[3]);
      }
      if (gen->fileBytesDeleted > 0) {
	  printf("\n\nFile type      Deleted");
	  for (i = 0; i < FS_STAT_NUM_TYPES; i++) {
	      printf("\n%-5s    ", typeStrings[i]);
	      PrintRatio(type->bytesDeleted[i],
			 gen->fileBytesDeleted);
	  }
	  printf("\nTotal       %10u 100%%\n", gen->fileBytesDeleted);
      } else {
	  printf("\n 0 bytes deleted from local disks.\n");
      }
      if (deleteHist && gen->fileBytesDeleted > 0) {
	  register int timeIndex, sizeIndex;
	  int fileType;
	  unsigned int cumBySize[FS_HIST_SIZE_BUCKETS];
	  unsigned int cumByTime[FS_HIST_TIME_BUCKETS];
	  unsigned int cumBySizeType[FS_HIST_SIZE_BUCKETS][FS_STAT_NUM_TYPES];
	  unsigned int cumByTimeType[FS_HIST_TIME_BUCKETS][FS_STAT_NUM_TYPES];
	  register unsigned int rowTotal;
	  unsigned int total = 0;
#ifdef notdef
	  unsigned int numBlocks;   /* number of blocks corresponding to an index */
#endif
	  unsigned int blockCount;  /* number blocks in array at current location */
	  unsigned int tooBig;	   /* subtotal of last few (unprintable) columns */
	  char separator[DEFAULT_WIDTH + 1];
	  static char *timeStrings[] = {
	      "1 sec ",
	      "2 secs",
	      "3 secs",
	      "4 secs",
	      "5 secs",
	      "6 secs",
	      "7 secs",
	      "8 secs",
	      "9 secs",
	      "10 secs",
	      "20 secs",
	      "30 secs",
	      "40 secs",
	      "50 secs",
	      "1 min ",
	      "2 mins",
	      "3 mins",
	      "4 mins",
	      "5 mins",
	      "6 mins",
	      "7 mins",
	      "8 mins",
	      "9 mins",
	      "10 mins",
	      "20 mins",
	      "30 mins",
	      "40 mins",
	      "50 mins",
	      "1 hr ",
	      "2 hrs",
	      "3 hrs",
	      "4 hrs",
	      "5 hrs",
	      "6 hrs",
	      "7 hrs",
	      "8 hrs",
	      "9 hrs",
	      "10 hrs",
	      "15 hrs",
	      "20 hrs",
	      "1 day ",
	      "2 days",
	      "3 days",
	      "4 days",
	      "5 days",
	      "6 days",
	      "7 days",
	      "8 days",
	      "9 days",
	      "10 days",
	      "20 days",
	      "30 days",
	      "40 days",
	      "50 days",
	      "60 days",
	      "90 days",
	      "120 days",
	      "180 days",
	      "240 days",
	      "300 days",
	      "360 days",
	      ">360 days",
	  };
	
	  bzero((Address) cumBySize, sizeof (cumBySize));
	  bzero((Address) cumByTime, sizeof (cumByTime));
	  bzero((Address) cumBySizeType, sizeof (cumBySizeType));
	  bzero((Address) cumByTimeType, sizeof (cumByTimeType));

	  /*
	   * May set times to be greater than actual max, since it is
	   * changing around in the kernel.
	   */
	  if (numTimesToPrint > FS_HIST_TIME_BUCKETS) {
	      numTimesToPrint = FS_HIST_TIME_BUCKETS;
	  }

	  for (i = 0; i < DEFAULT_WIDTH; i++) {
	      separator[i] = '-';
	  }
	  separator[DEFAULT_WIDTH] = '\0';

	  /*
	   * Go through the histogram and for each time, print out one line
	   * per file type with all the counts for that file type.
	   * Then print out a summary line for that time period.
	   * Since we only print out the first several columns, if we
	   * have data for files too big to display normaly, we lump them into
	   * a single column at the end.
	   */
	
	  printf(
    "\n\014\nDeletion histogram:%59s\n <= #secs   <1K <2K <4K <8K 16K ...%43s",
		   "Wtd", ">>  Total");
	  for (timeIndex = 0; timeIndex < FS_HIST_TIME_BUCKETS; timeIndex++) {
	      if (timeIndex < numTimesToPrint) {
		  printf("\n%s\n", separator);
	      }
	      for (fileType = 0; fileType < FS_STAT_NUM_TYPES; fileType ++) {
		  tooBig = 0;
		  if (timeIndex < numTimesToPrint) {
		      printf("%10s  ", typeStrings[fileType]);
		  }
		  if (timeIndex > 0) {
		      cumByTimeType[timeIndex][fileType] =
			      cumByTimeType[timeIndex - 1][fileType];
		  }
		  for (sizeIndex = 0; sizeIndex < FS_HIST_SIZE_BUCKETS - 1;
		       sizeIndex++) {
		       blockCount = type->deleteHist[timeIndex][sizeIndex]
		       [fileType];
		       if (blockCount > 0) {
			   cumBySizeType[sizeIndex][fileType] += blockCount;
			   cumBySize[sizeIndex] += blockCount;
		       }
		       if ((timeIndex < numTimesToPrint) &&
			   (sizeIndex < numSizesToPrint - 1)) {
			   printf("%3u ", cumBySizeType[sizeIndex][fileType]);
		       } else if (blockCount > 0) {
			   if (sizeIndex >= numSizesToPrint - 1) {
			       tooBig += blockCount;
			   } else {
			       fprintf(stderr,
				      "Warning: element <%u,%u,%u> = %u.\n",
				      timeIndex, sizeIndex, fileType,
				      blockCount);
			   }
		       }
		   }
		  rowTotal = type->deleteHist[timeIndex]
			  [FS_HIST_SIZE_BUCKETS-1][fileType];
		  cumByTimeType[timeIndex][fileType] += rowTotal;
		  total += rowTotal;
		  cumByTime[timeIndex] += rowTotal;
		  if (timeIndex < numTimesToPrint) {
		      printf("%3u %6u\n", tooBig,
			       cumByTimeType[timeIndex][fileType]);
		  }
	      }
	      /*
	       * Print out subtotals for this time.
	       */
	      tooBig = 0;
	      if (timeIndex < numTimesToPrint) {
		  printf("%9s   ", timeStrings[timeIndex]);
		  for (sizeIndex = 0; sizeIndex < FS_HIST_SIZE_BUCKETS - 1;
		       sizeIndex++) {
		       if (sizeIndex < numSizesToPrint - 1) {
			   printf("%3u ", cumBySize[sizeIndex]);
		       } else if (sizeIndex >= numSizesToPrint) {
			   tooBig += blockCount;
		       }
		   }
		  printf("%3u %6u", tooBig, total);
	      }
	  }
	  printf("\n\014\nPercentiles for bytes deleted, by type:\n\n");
	  printf("      Time   %6s %6s %6s %6s %6s   Cumulative         Cum. Total\n",
		   typeStrings[0],  typeStrings[1],
		   typeStrings[2], typeStrings[3], typeStrings[4]);
	  for (timeIndex = 0, rowTotal = 0;
	       timeIndex < numTimesToPrint; timeIndex++) {
	       rowTotal += cumByTime[timeIndex];
	       printf("%10s    ", timeStrings[timeIndex]);
	       for (fileType = 0; fileType < FS_STAT_NUM_TYPES; fileType++) {
		   ratio = GetRatio(cumByTimeType[timeIndex][fileType],
			      cumByTimeType[FS_HIST_TIME_BUCKETS - 1]
			      		   [fileType]);
		   printf("%5u  ", ratio);
	       }
	       printf("      %5u          %9u\n",
			GetRatio(rowTotal, total), rowTotal);
	   }
      }
		
  }
  exit(status);
}




/*
 *----------------------------------------------------------------------
 *
 * GetRatio --
 *
 *	Given two integers, return an integer that is 100 * the quotient of
 *	the two numbers, or 0 if the first number is 0, or 100 if the
 *	first number is greater than the second number.
 *
 * Results:
 *	The ratio, normalized to a percentage, is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
GetRatio(numerator, denominator)
    unsigned int numerator;
    unsigned int denominator;
{
    double ratio; 
			
    if (numerator > denominator) { 
	return(100); 
    }
    if (denominator > 0 && numerator > 0) { 
	ratio = (100. * numerator) / denominator + 0.5;
	if (ratio < 0) {
	    ratio = (100000. * (numerator/1000)) /
		    (1000. * (denominator/1000)) + 0.5;
	}
    } else { 
	ratio = 0.; 
    }
    return ((int) ratio);
} 
	
