/* 
 * fsUtils.c --
 *
 *	Utility procedures for fscheck
 *
 * Copyright 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/cmds/fscheck/RCS/fsUtils.c,v 1.10 92/06/09 21:48:15 jhh Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include "option.h"
#include "list.h"
#include "fscheck.h"
#include <string.h>
#include <host.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <stdio.h>
#include <sysStats.h>
#include <sys/param.h>

void WriteOutputFile();
int CloseOutputFile();

FILE outputFileInfo;
FILE *outputFile = &outputFileInfo;



/*
 *----------------------------------------------------------------------
 *
* ReadFileDescBitmap --
 *
 *	Read in the file descriptor bitmap.
 *
 * Results:
 *	A pointer to the file descriptor bit map.
 *
 * Side effects:
 *	Memory allocated for the bit map.
 *
 *----------------------------------------------------------------------
 */
unsigned char *
ReadFileDescBitmap(partFID, domainPtr)
    register Ofs_DomainHeader *domainPtr;	/* Ptr to domain to read bitmap for. */
{
    register unsigned char *bitmap;

    /*
     * Allocate the bitmap.
     */
    AllocByte(bitmap,unsigned char,domainPtr->fdBitmapBlocks * FS_BLOCK_SIZE);
    if (tooBig) {
	Output(stderr,"CheckFileSystem: Heap limit too small.\n");
	exit(EXIT_MORE_MEMORY);
    }
    if (Disk_BlockRead(partFID, domainPtr, domainPtr->fdBitmapOffset,
		  domainPtr->fdBitmapBlocks, (Address)bitmap) < 0) {
	OutputPerror("ReadFileDescBitmap: Read failed");
	exit(EXIT_READ_FAILURE);
    }
    return(bitmap);
}


/*
 *----------------------------------------------------------------------
 *
 * WriteFileDescBitmap --
 *
 *	Write out the file descriptor bitmap.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void
WriteFileDescBitmap(partFID, domainPtr, bitmap)
    int				partFID;	/* Raw handle on disk. */
    register Ofs_DomainHeader 	*domainPtr;	/* Domain to write bitmap for.*/
    register unsigned char 	*bitmap;	/* Bitmap to write. */
{
    if (Disk_BlockWrite(partFID, domainPtr, domainPtr->fdBitmapOffset,
		   domainPtr->fdBitmapBlocks, (Address)bitmap) < 0) {
	OutputPerror("WriteFileDescBitmap: Write failed");
	exit(EXIT_WRITE_FAILURE);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ReadBitmap --
 *
 *	Read the bitmap off disk.
 *
 * Results:
 *	A pointer to the bitmap.
 *
 * Side effects:
 *	Memory allocated for the bit map.
 *
 *----------------------------------------------------------------------
 */
unsigned char *
ReadBitmap(partFID, domainPtr)
    int				partFID;	/* Raw disk handle. */
    register Ofs_DomainHeader	*domainPtr;	/* Domain to read. */
{
    unsigned char *bitmap;
    AllocByte(bitmap,unsigned char,domainPtr->bitmapBlocks * FS_BLOCK_SIZE);
    if (tooBig) {
	Output(stderr,"CheckFileSystem: Heap limit too small.\n");
	exit(EXIT_MORE_MEMORY);
    }
    if (Disk_BlockRead(partFID, domainPtr, domainPtr->bitmapOffset,
		  domainPtr->bitmapBlocks, (Address) bitmap) < 0) {
	OutputPerror("ReadBitmap: Read failed");
	exit(EXIT_READ_FAILURE);
    }
    return(bitmap);
}


/*
 *----------------------------------------------------------------------
 *
 * WriteBitmap --
 *
 *	Write the bitmap to disk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void
WriteBitmap(partFID, domainPtr, bitmap)
    int				partFID;	/* Raw handle for disk. */
    register Ofs_DomainHeader	*domainPtr;	/* Domain to write. */
    unsigned char		*bitmap;	/* Bitmap to write. */
{
    if (Disk_BlockWrite(partFID, domainPtr, domainPtr->bitmapOffset,
		   domainPtr->bitmapBlocks, (Address) bitmap) < 0) {
	OutputPerror("WriteBitmap: Write failed");
	exit(EXIT_WRITE_FAILURE);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * WriteFileDesc --
 *
 *	Write the file descriptors to disk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	File descriptors on the modified list are all written to disk.
 *
 *----------------------------------------------------------------------
 */
void
WriteFileDesc(partFID, domainPtr, listPtr, descInfoPtr)
    int 			partFID;	/* Raw handle for disk. */
    register Ofs_DomainHeader 	*domainPtr;	/* Domain to write to. */
    List_Links			*listPtr;	/* Pointer to list of modified
						 * file descriptors to write
						 * out. */
    FdInfo			*descInfoPtr;	/* Pointer to info about all
						 * of the descriptors. */
					
{
    char		block[FS_BLOCK_SIZE];
    int			blockNum;
    int			offset;
    ModListElement	*modElemPtr;
    int			badBlock;

    LIST_FORALL(listPtr, (List_Links *)modElemPtr) {
	badBlock = 0;
	blockNum = domainPtr->fileDescOffset + 
			modElemPtr->fdNum / FSDM_FILE_DESC_PER_BLOCK;
	offset = (modElemPtr->fdNum & (FSDM_FILE_DESC_PER_BLOCK - 1)) *
		    FSDM_MAX_FILE_DESC_SIZE;
	/*
	 * Try to read the whole block at once unless we already know it's
	 * bad.  Read it a sector at a time if we hit an error earlier
	 * or if we get one now.
	 */
	if ((descInfoPtr[modElemPtr->fdNum].flags &
	     (FD_RELOCATE|FD_UNREADABLE)) == 0) {
	    if (Disk_BlockRead(partFID, domainPtr, blockNum, 1,
			       (Address) block) < 0) {
		OutputPerror("WriteFileDesc: Warning: read failed");
		badBlock = 1;
	    }
	} else {
	    badBlock = 1;
	}
	if (badBlock) {
	    (void) Disk_BadBlockRead(partFID, domainPtr, blockNum,
				     (Address) block);
	}
	bcopy((Address) modElemPtr->fdPtr, (Address) &block[offset],
		  sizeof(Fsdm_FileDescriptor));
	if (Disk_BlockWrite(partFID, domainPtr, blockNum, 1,
		       (Address) block) < 0) {
	    OutputPerror("WriteFileDesc: Write failed");
	    exit(EXIT_WRITE_FAILURE);
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * WriteSummaryInfo --
 *
 *	Update summary information on disk.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void
WriteSummaryInfo(partFID, labelPtr, domainPtr, numKblocks, numFiles)
    int			partFID;	/* Handle on raw disk */
    Disk_Label		*labelPtr;	/* The disk label. */
    Ofs_DomainHeader	*domainPtr;	/* Reference to domain header to
					 * fill in */
    int			numKblocks;	/* Number of 1 Kbyte blocks. */
    int			numFiles;	/* Number of files. */
{
    Ofs_SummaryInfo	*summaryInfoPtr;
    int 		status;

    summaryInfoPtr = Disk_ReadSummaryInfo(partFID, labelPtr);
    if (summaryInfoPtr == NULL) {
	OutputPerror("WriteSummaryInfo: Read failed");
	exit(EXIT_READ_FAILURE);
    }
    summaryInfoPtr->numFreeFileDesc = domainPtr->numFileDesc - numFiles -
	    numBadDesc;
    summaryInfoPtr->numFreeKbytes = 
	    domainPtr->dataBlocks * FS_FRAGMENTS_PER_BLOCK - numKblocks;
    if (clearDomainNumber) {
	Output(stderr,"Clearing domain number field.\n");
	summaryInfoPtr->domainNumber = -1;
    }
    /*
     * Do not set the checked bit if there is a hard error, or if we ran
     * out of memory or if the setCheckedBit flag is not set.
     */
    if (! (errorType < 0 || errorType == EXIT_OUT_OF_MEMORY || 
	   !setCheckedBit)) {
	if (verbose) {
	    Output(stderr,
		"Setting OFS_DOMAIN_JUST_CHECKED bit in summary sector.\n");
	}
	summaryInfoPtr->flags |= OFS_DOMAIN_JUST_CHECKED;
    }
    summaryInfoPtr->fixCount = fixCount;
    status = Disk_WriteSummaryInfo(partFID, labelPtr, summaryInfoPtr);
    if (status < 0) {
	OutputPerror("WriteSummaryInfo: Write failed");
	exit(EXIT_WRITE_FAILURE);
    }
    free((char *) summaryInfoPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * RecoveryCheck --
 *
 *	See if the disk was just checked. Print out information
 *	from summary sector.
 *
 * Results:
 *	Return 1 if the disk was just checked, 0 otherwise.
 *
 * Side effects:
 *	fixCount is set to the value in the summary sector.
 *
 *----------------------------------------------------------------------
 */
int
RecoveryCheck(partFID, labelPtr)
    int			partFID;	/* Handle on raw disk */
    Disk_Label		*labelPtr;	/* The disk label. */
{
    Ofs_SummaryInfo		*summaryInfoPtr;
    int				i;
    ReturnStatus		status = SUCCESS;
    char				name[MAXHOSTNAMELEN];

    summaryInfoPtr = Disk_ReadSummaryInfo(partFID, labelPtr);
    if (summaryInfoPtr == NULL) {
	OutputPerror("RecoveryCheck: Summary sector read failed.\n");
	exit(EXIT_READ_FAILURE);
    }
    fixCount = summaryInfoPtr->fixCount;
    if (verbose) {
	Output(stderr, "Summary Sector Info:\n");
	Output(stderr, "%s domain %d %s\n", summaryInfoPtr->domainPrefix,
	       summaryInfoPtr->domainNumber, 
	       (summaryInfoPtr->flags & OFS_DOMAIN_NOT_SAFE) ? "not-safe" : 
	       "safe");
        if (summaryInfoPtr->flags & OFS_DOMAIN_TIMES_VALID) {
	    Output(stderr, "Down %d seconds.\n",summaryInfoPtr->attachSeconds -
		   summaryInfoPtr->detachSeconds);
        } else {
	    Output(stderr, "Attach/Detach fields not valid.\n");
	}
	Output(stderr, "Fscheck has fixed disk %d times already.\n", fixCount);
    } else { 
	Output(stderr, "\"%s\"\n", summaryInfoPtr->domainPrefix);
    }
    gethostname(name, MAXHOSTNAMELEN);
    /*
     * Check and see if the disk is already attached.  
     */
    for (i = 0; status == SUCCESS; i++) {
	Fs_Prefix			prefix;
	Host_Entry 			*serverInfoPtr;

	bzero((char *) &prefix, sizeof(Fs_Prefix));
	status = Sys_Stats(SYS_FS_PREFIX_STATS, i, (Address) &prefix);
	if (status == SUCCESS) {
	    if (!strcmp(prefix.prefix, summaryInfoPtr->domainPrefix)) {
		serverInfoPtr = Host_ByID(prefix.serverID);
		if (serverInfoPtr == NULL) {
		    continue;
		}
		if (!strcmp(serverInfoPtr->name, name)) {
		    if (verbose) {
			Output(stderr, "Disk is already attached\n");
		    }
		    attached = TRUE;
		    break;
		}
	    }
	}
    }
    if (clearFixCount) {
	if (!silent) {
	    Output(stderr, "Fix count being reset to 0.\n");
	}
	fixCount = 0;
    }
    return(summaryInfoPtr->flags & OFS_DOMAIN_JUST_CHECKED);
}



/*
 *----------------------------------------------------------------------
 *
 * CheckFDBitmap --
 *
 *	Scan through the file descriptors and determine if all file 
 *	descriptors marked as allocated and free in the bit map are
 *	really that way.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
CheckFDBitmap(domainPtr, fdNum, block, bitmapPtrPtr)
    register	Ofs_DomainHeader	 *domainPtr;		/* Domain to check. */
    int				 fdNum;			/* File descriptor to
							 * check. */
    Address			 block;			/* Disk block that
							 * FD is in. */
    register	unsigned char 	 **bitmapPtrPtr;	/* Ptr to FD bitmap
							 * entry for fdNum. */
{
    int				i, j;
    register unsigned char 	*bitmaskPtr;
    int			   	allocated;
    Fsdm_FileDescriptor		*fdPtr;

    for (i = 0; 
	 i < FSDM_FILE_DESC_PER_BLOCK / BITS_PER_BYTE && 
		fdNum < domainPtr->numFileDesc;
	 i++, (*bitmapPtrPtr)++){
	for (j = 0, bitmaskPtr = bitmasks; 
	     j < BITS_PER_BYTE && fdNum < domainPtr->numFileDesc; 
	     j++, fdNum++, bitmaskPtr++) {

	    fdPtr = (Fsdm_FileDescriptor *)&block[(i * BITS_PER_BYTE + j) * FSDM_MAX_FILE_DESC_SIZE];
	    allocated = **bitmapPtrPtr & *bitmaskPtr;
	    if (allocated && (fdPtr->flags & FSDM_FD_FREE)) {
		if (bitmapVerbose) {
		    Output(stderr,
	   "Free file descriptor %d allocated in bitmap.  Bitmap corrected.\n",
				   fdNum);
		}
		foundError = 1;
		fdBitmapError = 1;
		**bitmapPtrPtr &= ~*bitmaskPtr;
	    } else if (!allocated && !(fdPtr->flags & FSDM_FD_FREE)) {
		if (bitmapVerbose) {
		    Output(stderr,
	   "Allocated file descriptor %d free in bitmap.  Bitmap corrected.\n",
				   fdNum);
		}
		foundError = 1;
		fdBitmapError = 1;
		**bitmapPtrPtr |= *bitmaskPtr;
	    }
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * SetBadDescBitmap --
 *
 *	Go through the bitmaps and flag all the bits for this block as
 *	allocated.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
void
SetBadDescBitmap(domainPtr, fdNum, bitmapPtrPtr)
    register	Ofs_DomainHeader	 *domainPtr;		/* Domain to fix. */
    int				 fdNum;			/* Bad file desc. */
    register	unsigned char 	 **bitmapPtrPtr;	/* Ptr to bitmap entry
							 * for bad file desc.*/
{
    int				i, j;
    register unsigned char 	*bitmaskPtr;

    for (i = 0; i < FSDM_FILE_DESC_PER_BLOCK / BITS_PER_BYTE && 
		fdNum < domainPtr->numFileDesc;
	     i++, (*bitmapPtrPtr)++){
	for (j = 0, bitmaskPtr = bitmasks; 
	     j < BITS_PER_BYTE && fdNum < domainPtr->numFileDesc; 
	     j++, fdNum++, bitmaskPtr++) {

	     **bitmapPtrPtr |= *bitmaskPtr;
	 }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MarkBitmap --
 *
 *	Mark the bits in the bit map.
 *	update the cylinder map to reflect which blocks are allocated.
 *
 * Results:
 *	-1 if couldn't mark the bitmap, 0 otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
int
MarkBitmap(fdNum, blockNum, bitmapPtr, numFrags, domainPtr)
    int		  fdNum;
    int		  blockNum;
    unsigned char *bitmapPtr;
    int		  numFrags;
    Ofs_DomainHeader  *domainPtr;
{
    register	unsigned char 	*bytePtr;
    register	unsigned char 	*bitmaskPtr;
    unsigned 	char	      	bitmask;
    int				i;
    int				fullBlockNum;
    int				fragOffset;
    int				dupBlocks;

    if (blockNum >= num1KBlocks || blockNum < 0) {
	if (verbose || lastErrorFD != fdNum) {
	    Output(stderr, "Block pointer %d invalid in file %d\n", 
			   blockNum, fdNum);
	    lastErrorFD = fdNum;
	}
	foundError = 1;
	return(-1);
    }
    bitmask = 0;
    fullBlockNum = blockNum / FS_FRAGMENTS_PER_BLOCK;
    bytePtr = GetBitmapPtr(domainPtr, bitmapPtr, fullBlockNum);
    fragOffset = blockNum & 0x3;
    if ((fullBlockNum % domainPtr->geometry.blocksPerCylinder) & 0x1) {
	fragOffset += 4;
    }
    bitmaskPtr = &bitmasks[fragOffset];

    dupBlocks = 0;
    for (i = 0; i < numFrags; i++, bitmaskPtr++) {
	if (*bitmaskPtr & *bytePtr || 
	    (fdNum != FSDM_ROOT_FILE_NUMBER) && blockNum == 0) { 
	    if (noCopy) {
		if (verbose || lastErrorFD != fdNum) {
		    Output(stderr,
	"File %d references previously allocated block.  Block %d deleted.\n", 
			   fdNum, blockNum + i);
		    lastErrorFD = fdNum;
		}
		foundError = 1;
		return -1;
	    }
	    dupBlocks++;
	}
	bitmask |= *bitmaskPtr;
    }
    if (dupBlocks > 0) {
	foundError = 1;
	/*
	 * All fragments are duplicates, so mark bitmap and return 1 so that
	 * these fragments are copied.
	 */
	if (verbose || lastErrorFD != fdNum) {
	    Output(stderr,"File %d contains duplicate block %d.\n",fdNum,
		   blockNum);
	}
	*bytePtr |= bitmask;
	return(1);
    }
    *bytePtr |= bitmask;
    return(0);
}

/*
 *----------------------------------------------------------------------
 *
 * Output ---
 *	
 *	Prints the output to the given stream, and if the outputFile is  
 *	not NULL it is also printed to the outputFile.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff gets printed on the given stream and the output stream.
 *
 *----------------------------------------------------------------------
 */

#ifndef lint

int
Output(va_alist)
    va_dcl			/* FILE *stream, then char *format, then any
				 * number of additional
				 * values to be printed as described by						 * format. */
{
    FILE *stream;
    char *format;
    va_list args;
    static bufferFull = FALSE;
    extern char *deviceName;
    extern char *partName;
    int	status;

    va_start(args);
    stream = va_arg(args, FILE *);
    format = va_arg(args, char *);
    if ((!bufferFull) && (outputFile != NULL)) {
	if (fprintf(outputFile, "%s%s: ", deviceName, partName) == -1) {
	    bufferFull = TRUE;
	}
	if (bufferFull != TRUE) {
	    if (vfprintf(outputFile, format, args) == -1) {
		bufferFull = TRUE;
	    }
	}
    }
    status = fprintf(stream, "%s%s: ", deviceName, partName);
    if (status != -1) {
	status = vfprintf(stream, format, args);
    }
    return status;
}
#else
/* VARARGS1 */
/* ARGSUSED */
int
Output(stream,format)
    FILE *stream;
    char *format;
{
    return 0;
}
#endif lint


/* 
 *----------------------------------------------------------------------
 *
 * OutputPerror --
 *
 *	Prints the given message on stderr and the outputFile stream 
 *	using the same format as OutputPerror().
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Stuff gets printed.
 *
 *----------------------------------------------------------------------
 */

#ifndef lint
void
OutputPerror(va_alist)
    va_dcl			/* char *format, then any
				 * number of additional
				 * values to be printed as described by						 * format. */
{
    char *format;
    va_list args;

    va_start(args);
    format = va_arg(args, char *);
    if ((format != 0) && (*format != 0)) {
	Output(stderr, format, args);
    }
    if ((errno < 0) || (errno >= sys_nerr)) {
	return;
    }
    Output(stderr, "%s\n", sys_errlist[errno]);
}

#else
/* VARARGS1 */
/* ARGSUSED */
void 
OutputPerror(msg)
    char *msg;		
{
}
#endif



/*
 *----------------------------------------------------------------------
 *
 * WriteOutputFile ---
 *	
 *	This procedure is invoked when the outputFile stream buffer is full.
 *      All we do here is set the status on the stream to -1, to indicate
 *      that the buffer is full. The buffer is actually written out when the
 *	stream is closed.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	Status in FILE is set to -1.
 *
 *----------------------------------------------------------------------
 */

/*ARGSUSED*/
void
WriteOutputFile(stream,flush)
    FILE *stream;		/* pointer to outputFile */
    int flush; 			/* ignore this */
{
    if (!silent && stream->status != -1 && 
        stream->lastAccess + 1 - stream->buffer == stream->bufSize) {
	fprintf(stderr,">>Output buffer overflow. %s\n",
	        "Subsequent output will not appear in output file.");
    }
    stream->status = -1;
}


/*
 *----------------------------------------------------------------------
 *
 * CloseOutputFile ---
 *
 * 	Flushes the buffer to disk. 
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
CloseOutputFile(stream)
    FILE *stream;		/* pointer to outputFile */
{
    Fsdm_FileDescriptor *fdPtr;
    static u_char fdBlock[FS_BLOCK_SIZE];
    static u_char buffer[FS_BLOCK_SIZE];
    static u_char tempBuffer[10];
    int 		bytesToWrite;
    int			bytesWritten;
    int			blockNum;
    int			offset;
    int			startBlock;
    int			startByte;
    int			i;
    int			bytesUsed;
    int			bytesDone;


    if (!writeDisk) {
	return;
    }
    if (outputFileNum == -1) {
	Output(stderr,
	    "File %s does not exist in root directory. %s\n",
	    outputFileName,
	    "Unable to write output to disk.");
	    return;
    }
    blockNum = domainPtr->fileDescOffset + 
	       outputFileNum / FSDM_FILE_DESC_PER_BLOCK;
    offset = (outputFileNum & (FSDM_FILE_DESC_PER_BLOCK - 1)) * 
	      FSDM_MAX_FILE_DESC_SIZE;
    if (debug) {
	Output(stderr,"Output file number is %d.\n", outputFileNum);
    }
    if (Disk_BlockRead(partFID, domainPtr, blockNum, 1, 
		       (Address) fdBlock) < 0) {
	OutputPerror("Unable to read output fd.");
	return;
    }
    fdPtr = (Fsdm_FileDescriptor *) &fdBlock[offset];
    if (fdPtr->direct[0] != FSDM_NIL_INDEX) {
	if (Disk_BlockRead(partFID, domainPtr,
			  VirtToPhys(domainPtr, fdPtr->direct[0]) /
			  FS_FRAGMENTS_PER_BLOCK, 1,
			  (Address) buffer) < 0) {
	    Output(stderr,"Can't read output file.");
	    return;
	}
	if (sscanf(buffer," %d",&bytesUsed) != 1) {
	    bytesUsed = 0;
	}
    } else {
	bytesUsed = 0;
    }
    if (fdPtr->lastByte + 1 - bytesUsed  < 
	stream->bufSize - stream->writeCount) {
	Output(stderr,
	    "Output file %s is not big enough for all the output, %d > %d\n",
	       outputFileName, stream->bufSize - stream->writeCount, 
	       fdPtr->lastByte + 1 - bytesUsed);
	bytesToWrite = fdPtr->lastByte + 1 - bytesUsed;
    } else {
	bytesToWrite = stream->bufSize - stream->writeCount;
    }
    if (bytesUsed + bytesToWrite > FSDM_NUM_DIRECT_BLOCKS * FS_BLOCK_SIZE) {
	Output(stderr,"Output exceeds direct blocks in file.\n");
	bytesToWrite = FSDM_NUM_DIRECT_BLOCKS * FS_BLOCK_SIZE - bytesUsed;
    }
    if (debug) {
	Output(stderr,"There are %d bytes of output to be written.\n",
	       bytesToWrite);
    }
    startBlock = bytesUsed / FS_BLOCK_SIZE;
    startByte = bytesUsed - startBlock * FS_BLOCK_SIZE;
    bytesDone = bytesUsed;

    for (i = startBlock, bytesWritten = 0; 
	 bytesDone <= fdPtr->lastByte && 
	 i < FSDM_NUM_DIRECT_BLOCKS; 
	 i++) {
	if (fdPtr->direct[i] == FSDM_NIL_INDEX) {
	    Output(stderr,"Output file has a hole -- you lose.\n");
	    continue;
	}
	if (debug) {
	    Output(stderr,"Writing to block %d.\n",fdPtr->direct[i]);
	}
	if (startByte > 0 || 
	    bytesToWrite - bytesWritten < FS_BLOCK_SIZE - startByte) {
	    int numBytes;
	    int bytesToZero;

	    numBytes = min(bytesToWrite - bytesWritten,
			   FS_BLOCK_SIZE - startByte);
	    if (Disk_BlockRead(partFID, domainPtr,
			  VirtToPhys(domainPtr, fdPtr->direct[i]) /
			  FS_FRAGMENTS_PER_BLOCK, 1,
			  (Address) buffer) < 0) {
		Output(stderr,"Can't read output file.");
		return;
	    }
	    bcopy((Address) &stream->buffer[bytesWritten],
		  (Address) &buffer[startByte], numBytes);
	    bytesToZero = min(fdPtr->lastByte + 1 - bytesDone - numBytes,
			      FS_BLOCK_SIZE - startByte - numBytes);
	    bzero((Address) &buffer[startByte + numBytes], bytesToZero);
	    if (Disk_BlockWrite(partFID, domainPtr, 
			       VirtToPhys(domainPtr,fdPtr->direct[i]) / 
			       FS_FRAGMENTS_PER_BLOCK, 1, buffer)  < 0) {
		OutputPerror("Unable to write to output file");
		return;
	    }
	    bytesWritten += numBytes;
	    bytesDone += numBytes + bytesToZero;
	    startByte = 0;
	} else {
	    if (Disk_BlockWrite(partFID, domainPtr, 
			       VirtToPhys(domainPtr,fdPtr->direct[i]) / 
			       FS_FRAGMENTS_PER_BLOCK, 1, 
			       (Address) &stream->buffer[bytesWritten]) < 0) {
		OutputPerror("Unable to write to output file");
		return;
	    }
	    bytesWritten += FS_BLOCK_SIZE;
	    bytesDone += FS_BLOCK_SIZE;
	}
    }
    if (fdPtr->direct[0] != FSDM_NIL_INDEX) {
	if (Disk_BlockRead(partFID, domainPtr,
			  VirtToPhys(domainPtr, fdPtr->direct[0]) /
			  FS_FRAGMENTS_PER_BLOCK, 1,
			  (Address) buffer) < 0) {
	    Output(stderr,"Can't read output file.");
	    return;
	}
	sprintf(tempBuffer,"%05d",bytesWritten + bytesUsed);
	bcopy(tempBuffer, buffer, 5);
	if (Disk_BlockWrite(partFID, domainPtr,
			  VirtToPhys(domainPtr, fdPtr->direct[0]) /
			  FS_FRAGMENTS_PER_BLOCK, 1,
			  (Address) buffer) < 0) {
	    Output(stderr,"Can't write output file.");
	    return;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * ExitHandler ---
 *
 * 	Called when program exits. Flushes output stream.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
ExitHandler()
{
    if (outputFile != NULL && rawOutput) {
	CloseOutputFile(outputFile);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ClearFd ---
 *
 * 	Clears the contents of the fd.
 *	
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
ClearFd(flags, fdPtr)
    int	flags;				/* File descriptor flags */ 
    Fsdm_FileDescriptor *fdPtr;		/* File descriptor to be cleared */
{
    int index;

    fdPtr->magic = FSDM_FD_MAGIC;
    fdPtr->flags = flags;
    fdPtr->firstByte = -1;
    fdPtr->lastByte = -1;
    fdPtr->numKbytes = 0;
    fdPtr->fileType = FS_FILE;
    fdPtr->uid = 0;
    fdPtr->gid = 0;
    fdPtr->numLinks = 0;
    for (index = 0; index < FSDM_NUM_DIRECT_BLOCKS ; index++) {
	fdPtr->direct[index] = FSDM_NIL_INDEX;
    }
    for (index = 0; index < FSDM_NUM_INDIRECT_BLOCKS ; index++) {
	fdPtr->indirect[index] = FSDM_NIL_INDEX;
    }
}

/*
 * A table indexed by a 4 bit value is used by the allocation routine to 
 * quickly determine the location of 1, 2, and 3K fragments in a byte.  
 * The indices of the fragments start from 0.  If there is no such fragment in 
 * the byte then a -1 is used.
 */

static int fragTable[16][3] = {
/* 0000 */ {-1, -1, -1},
/* 0001 */ {-1, -1, 0},
/* 0010 */ {3, 0, -1},
/* 0011 */ {-1, 0, -1},
/* 0100 */ {0, 2, -1},
/* 0101 */ {0, -1, -1},
/* 0110 */ {0, -1, -1},
/* 0111 */ {0, -1, -1},
/* 1000 */ {-1, -1, 1},
/* 1001 */ {-1, 1, -1},
/* 1010 */ {1, -1, -1},
/* 1011 */ {1, -1, -1},
/* 1100 */ {-1, 2, -1},
/* 1101 */ {2, -1, -1},
/* 1110 */ {3, -1, -1},
/* 1111 */ {-1, -1, -1}
};
/*
 * Macros to get to the 4-bit fragment masks of the two 4K blocks that are 
 * stored in a byte.
 */

#define	UpperBlockFree(byte)	(((byte) & 0xf0) == 0x00)
#define	LowerBlockFree(byte)	(((byte) & 0x0f) == 0x00)
#define	BothBlocksFree(byte)	(((byte) & 0xff) == 0x00)
#define	GetUpperFragMask(byte) (((byte) >> 4) & 0x0f)
#define	GetLowerFragMask(byte) ((byte) & 0x0f)

/*
 *----------------------------------------------------------------------
 *
 * AllocBlock --
 *
 *	Allocates free fragments.
 *
 * Results:
 *	-1 if a free block cannot be found, 
 *	the virtual block number of the free block otherwise
 *
 * Side effects:
 *	Marks the fragments as allocated in the bitmap
 *
 *----------------------------------------------------------------------
 */

int
AllocBlock(domainPtr, fragments, blockBitmapPtr)
    Ofs_DomainHeader 	*domainPtr; 		/* Ptr at domain info */
    int			fragments;		/* Number of fragments needed */
    u_char		*blockBitmapPtr;	/* Cylinder data block bitmap */
{
    int 	mask;
    int 	i;
    int		j;
    int 	blocksPerCylinder;
    int		bitmapBytes;
    int 	offset;
    int		fragSize;
    u_char	*bitmapPtr;

    if (fragments < 1 || fragments > FS_FRAGMENTS_PER_BLOCK) {
	Output(stderr,"Internal error: call to AllocBlock w/ fragments = %d\n",
	       fragments);
        exit(EXIT_HARD_ERROR);
    }
    blocksPerCylinder = domainPtr->geometry.blocksPerCylinder;
    bitmapBytes = (unsigned int) (blocksPerCylinder + 1) / 2;
    mask = ((1 << fragments) - 1);
    fragSize = fragments;

    /*
     * Look for fragment of correct size, then size +1, size +2, etc.
     */
    while (fragSize <= FS_FRAGMENTS_PER_BLOCK) {
	for (i = 0, bitmapPtr = blockBitmapPtr; 
	     i < domainPtr->dataCylinders; 
	     i++) {

	    for (j = 0; j < bitmapBytes; j++, bitmapPtr++) {
		/*
		 * Block 0 belongs to the root directory so don't allocate it 
		 * even if it is free.
		 */
		if (j + i != 0) {
		    if (fragSize == 4) {
			if (UpperBlockFree(*bitmapPtr)) {
			    mask <<= FS_FRAGMENTS_PER_BLOCK - fragments;
			    *bitmapPtr |= mask << 4;
			    return (i * blocksPerCylinder + j * 2) * 
				    FS_FRAGMENTS_PER_BLOCK; 
			}
		    } else {
			offset = 
			    fragTable[GetUpperFragMask(*bitmapPtr)][fragSize-1];
			if (offset > -1) {
			    mask <<= FS_FRAGMENTS_PER_BLOCK - offset - 
				     fragments;
			    *bitmapPtr |= mask << 4;
			    return (i * blocksPerCylinder + j * 2) * 
				    FS_FRAGMENTS_PER_BLOCK + offset; 
			}
		    }
		}
		/*
		 * There may be an odd number of blocks per cylinder.  If so
		 * and are at the end of the bit map for this cylinder, then
		 * we can bail out now.
		 */
    
		if (j == (bitmapBytes - 1) && (blocksPerCylinder & 0x1)) {
		    continue;
		}
		if (fragSize == 4) {
		    if (LowerBlockFree(*bitmapPtr)) {
			mask <<= FS_FRAGMENTS_PER_BLOCK - fragments;
			*bitmapPtr |= mask;
			return (i * blocksPerCylinder + j * 2 + 1) * 
				FS_FRAGMENTS_PER_BLOCK; 
		    }
		} else {
		    offset = 
			fragTable[GetLowerFragMask(*bitmapPtr)][fragSize-1];
		    if (offset > -1) {
			mask <<= FS_FRAGMENTS_PER_BLOCK - offset - fragments;
			*bitmapPtr |= mask;
			return (i * blocksPerCylinder + j * 2 + 1) * 
				FS_FRAGMENTS_PER_BLOCK + offset; 
		    }
		}
	    }
	}
	fragSize++;
    }
    /*
     * Disk is full
     */
    return -1;
}


/*
 *----------------------------------------------------------------------
 *
 * AddToCopyList --
 *
 *	Adds information about the block to be copied to the copy list.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Copy list element is malloced, added to copy list.
 *
 *----------------------------------------------------------------------
 */

void
AddToCopyList(parentType,fdPtr, fdNum, blockNum, index, blockType,fragments,
	      copyUsedPtr)
    ParentType		parentType; 	/* either a fd or an indirect block */
    Fsdm_FileDescriptor	*fdPtr;		/* ptr to parent fd */
    int			fdNum;		/* number of parent fd */
    int			blockNum;	/* number of parent block */
    int			index;		/* index of pointer  in parent */
    BlockIndexType	blockType;	/* type of block being copied */
    int			fragments;	/* number of fragments to copy*/
    Boolean		*copyUsedPtr;	/* Was copy of fd used ? */
{
    CopyListElement	*copyPtr;

    Alloc(copyPtr,CopyListElement,1);
    if (!tooBig) {
	List_InitElement((List_Links *) copyPtr);
	copyPtr->fdPtr = fdPtr;
	copyPtr->parentType = parentType;
	if (parentType == FD) {
	    copyPtr->parentNum = fdNum;
	    *copyUsedPtr = TRUE;
	} else {
	    copyPtr->parentNum = blockNum;
	}
	copyPtr->index = index;
	copyPtr->blockType = blockType;
	copyPtr->fragments = fragments;
	List_Insert((List_Links *) copyPtr,
		    LIST_ATREAR(copyList));
    }
}


/*
 *----------------------------------------------------------------------
 *
 * FindOutputFile --
 *
 *	Finds the output file for raw output.  This routine should only
 *	be called if the disk has been checked previously, since the
 *	output file will be found in CheckDirTree otherwise.
 *	
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

void
FindOutputFile()
{
    Fsdm_FileDescriptor	*rootFDPtr;
    FdInfo		fdInfo;
    DirIndexInfo	dirIndex;
    Fslcl_DirEntry	*dirEntryPtr;
    int			length;
    int			blockNum;
    int			offset;
    static char		block[FS_BLOCK_SIZE];
    extern void		OpenDir();
    extern void		NextDirEntry();


    if (!rawOutput) {
	return;
    }
    if (outputFileName == NULL) {
	return;
    }
    length = strlen(outputFileName);
    blockNum = domainPtr->fileDescOffset + 
		    FSDM_ROOT_FILE_NUMBER / FSDM_FILE_DESC_PER_BLOCK;
    offset = (FSDM_ROOT_FILE_NUMBER & (FSDM_FILE_DESC_PER_BLOCK - 1)) * 
		FSDM_MAX_FILE_DESC_SIZE;
    if (Disk_BlockRead(partFID, domainPtr, blockNum, 1, 
		       (Address) block) < 0) {
       return;
    }
    rootFDPtr = (Fsdm_FileDescriptor *) &block[offset];
    OpenDir(rootFDPtr, &fdInfo, &dirIndex, &dirEntryPtr);
    while (dirEntryPtr != (Fslcl_DirEntry *) NULL) {
	if ((dirEntryPtr->nameLength == length) &&
	    (strncmp(outputFileName, dirEntryPtr->fileName, length) == 0)) {

	    outputFileNum = dirEntryPtr->fileNumber;
	    return;
	}
	NextDirEntry(&dirIndex, &dirEntryPtr);
    }
}

