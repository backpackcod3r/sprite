/* 
 * global.c --
 *
 *	Routines to manage the global loadavg data base.  Only one
 *	process is permitted to open the global pdev in master
 *	mode and service requests on it.  All hosts run a daemon that
 *	communicates periodically with the global server.  Clients
 *	communicate with the global server to obtain load info and request
 *	idle hosts.  Idle hosts may be released explicitly by ioctls with
 *	the server, or implicitly upon closing the pdev connection.
 *
 * 	The global server maintains an array of structures, one per host,
 *	with the load information for each host.  The hosts are linked into
 *	idle lists, which are distinguished by architecture type and
 *	the type of processes running on the hosts. If a host is completely
 *	idle, then it is available for any type of process.  On the other
 *	hand, it might be used by a low priority process, and it could still
 *	be used by a higher-priority process but not by another low priority
 *	process.  In each case, the availability is determined by the
 *	number of processors on the host, so a host with 4 processors and
 *	three background jobs is presumed to be capable of a fourth background
 *	job or up to 4 higher-priority jobs.  The queue for completely
 *	idle hosts is sorted in order of idle time, but the other queues
 *	are just FIFO since if a host already has stuff on it then it
 * 	is less likely to matter how long it's already been idle. (Maybe
 *	this isn't true; in that case, sorting the other queues is
 *	always possible.)
 *
 *	In addition, the server maintains information about each
 *	process that is using idle hosts, and each user.  The per-user
 *	information is not yet used but may be used to implement a fairness
 *	policy at some future date.  The per-process information is associated
 *	with the pdev handle for the client.  If the pdev is closed,
 *	all hosts used by that process are marked available.
 *
 * Copyright 1989, 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/daemons/migd/RCS/global.c,v 2.5 92/06/16 20:51:54 jhh Exp $ SPRITE (Berkeley)";
#endif /* not lint */


#include <sprite.h>
#include <string.h>
#include <list.h>
#include <errno.h>
#include <status.h>
#include "syslog.h"
#include <bstring.h>
#include <fs.h>
#include <kernel/fs.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/dir.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <host.h>
#include <signal.h>
#include <pdev.h>
#include <kernel/net.h>
#include "mig.h"
#include "migd.h"
#include "migPdev.h"

/* 
 * Define a structure for keeping track of info we need to send to
 * clients.  Each message is a single int.
 */
typedef struct {
    List_Links links;		/* Link to next message. */
    int msg;			/* Integer-valued message to tell client. */
} MessageBlock;

/* 
 * A structure is used to keep track of outstanding requests,
 * i.e., idle hosts that have been assigned to a process and not yet
 * returned.   It points back to the open stream info and to two linked
 * lists, one that's per stream and one that's per host.
 */
typedef struct RequestInfo {
    List_Links  nextClientRequest;	/* Link to next request by same
					   client. */
    List_Links  nextHostRequest;	/* Link to next request for this
					   host. */
    List_Links  nextInUse;		/* Link to next request for this
					   machine type and priority. */
    int timestamp;			/* Time of request. */
    int priority;			/* Priority of processes. */
    int numProcessors;			/* Number of processors assigned. */
    int idleTime;			/* Idle time of host at time of
					   request (in case of eviction,
					   for statistics). */
    int flags;				/* Flags applied to request. */
    union {
	Migd_OpenStreamInfo *cltPtr; 	/* Pointer back to open stream info
					   for client. */
	int processID;			/* ID of process, if agent. */
    } client;
    struct Migd_Info *migdPtr;		/* Pointer back to host assigned. */
} RequestInfo;

#define NEXT_HOST_REQUEST_TO_INFO(listPtr) \
	( (RequestInfo *)((int)(listPtr) - sizeof(List_Links)) )

#define NEXT_HOST_IN_USE_TO_INFO(listPtr) \
	( (RequestInfo *)((int)(listPtr) - 2 * sizeof(List_Links)) )

/*
 * Arguments to RevokePermission().  Indicates what to do with client and
 * what to do with statistics gathering. 
 */
typedef enum {
    REVOKE_EVICT,		/* Evicting processes. */
    REVOKE_STOLEN,		/* Claiming for fairness purposes. */
    REVOKE_IDLE,		/* Thought host was unused for a long time. */
} RevokeAction;

/*
 * Define the number of seconds we'll wait before notifying a process
 * about new hosts being available if we just stole hosts from it.
 */
#define MIGD_STOLE_WINDOW 30


static int maxArchTypes = MIG_MAX_ARCH_TYPES;  	/* number of elements
						   in arrays per-type */
static char **archTypesArray;   /* array of strings mapping types to indexes
				   in these arrays */

static int maxHosts = NET_NUM_SPRITE_HOSTS; /* number of hosts in arrays
					       per-host */
static int maxKnownHost = 0;	/* Highest known host ID. */

static Migd_Info **migInfoArray;	/* array of host info pointers */
static List_Links *idleHostsArray[MIG_NUM_PRIORITIES];
					/* array of lists of idle hosts,
					   indexed by type and priority. */
static List_Links *inUseArray[MIG_NUM_PRIORITIES];
					/* array of lists of host requests,
					   indexed by type and priority. */
static List_Links *waitingLists[MIG_NUM_PRIORITIES];
					/* array of lists of waiting clients,
					   indexed by type. */

static int hostsUp = 0;		/* Total number of hosts up, used for
				   determining when it's safe to exit after
				   termination. */
static int hostCounts[MIG_MAX_ARCH_TYPES][MIG_NUM_STATES];
				/* Counts per-architecture, per-state. */

static int lastRequestHost = -1; /* ID of last host making a request. */

/*
 * Global variables.
 */
int global_Debug = 0;		/* enable debugging info? */

int global_CheckpointInterval = MIG_TIMEOUT;	/* Interval for saving
						   checkpoints. */

Mig_Stats global_Stats;		/* Statistics, maintained mostly by this
				   file. */

#ifndef MIGD_CHECKPOINT_FILE
#define MIGD_CHECKPOINT_FILE "/sprite/admin/migd/check"
#endif /* MIGD_CHECKPOINT_FILE */

#ifndef MIGD_LOCK_FILE
#define MIGD_LOCK_FILE "/sprite/admin/migd/lock"
#endif /* MIGD_LOCK_FILE */

#ifndef MIGD_LOG_FILE
#define MIGD_LOG_FILE "/sprite/admin/migd/log"
#endif /* MIGD_LOG_FILE */

/* 
 * Things to keep track of the files used (errors, and lock file).
 */
char *global_ErrorFile = NULL;	/* Place to write host-daemon errors,
				   initialized in main. */
static int  lockDesc;		/* Descriptor for lock file. */
static struct stat descAtts;	/* Attributes of lock file. */

static void CreateMigdRecord();  /* Allocate a Migd_Info record. */
static void RestoreCheckPoint(); /* Restore checkpoint from file. */
static void SaveCheckPoint(); 	 /* Save checkpoint to file. */
static void CheckHostStatus(); 	 /* Check the status of a host after an
				    update. */
static int  RemoveHost(); 	 /* Remove host from list of hosts. */
static int  HostIsIdle(); 	 /* Check for host being idle. */
static void InsertIdle(); 	 /* Add host to list of idle hosts. */
static void WakeupWaiters(); 	 /* Wake up processes that want more hosts. */
static void RecordEvictions(); 	 /* Remove host from idle list and notify
				    clients of evictions. */
static void ForceHostIdle(); 	 /* Make a host seem idle. */
static void TellClient(); 	 /* Notify a client about a change in
				    status. */
static int CheckFairness(); 	 /* Check on host allocation fairness. */
static void RevokePermission();	 /* Notify a client that its permission to
				    use a host has been revoked. */
static Migd_Info *CltToMigd();	 /* Map from client info to migd record. */

static void InitStats();	/* Initialize statistics. */
static void DumpStats();	/* Dump statistics to file. */
static int  ReadStats();	/* Read statistics from file. */


/*
 *----------------------------------------------------------------------
 *
 * Global_Init --
 *
 *	Initialize state of the global server.   Called when a process
 *	forks and the child tries to become the master for the global
 *	pdev.
 *
 * Results:
 *	0 for successful completion, -1 for error, with errno indicating
 *	the nature of the error.
 *
 * Side effects:
 *	Opens pseudo-device, allocates arrays and assigns architecture strings
 *	to indexes.
 *
 *----------------------------------------------------------------------
 */

int
Global_Init()
{
    Host_Entry *hostPtr;
    int i, j;
    Time	period;
    char pidString[10];
    FILE *logFile;

    migd_Pid = getpid();
    
    if (migd_LogToFiles) {
	freopen(migd_GlobalErrorName, "a", stderr);
#ifdef SEEK_REOPEN	
	fseek(stderr, 0L, L_XTND);
#endif /* SEEK_REOPEN */
	if (fcntl(fileno(stderr), F_SETFL, FAPPEND) < 0) {
	    perror("fcntl");
	}
    }
    
    if (global_Debug > 0) {
	int t = time(0);
	fprintf(stderr, "Global_Init - process %x version %u on host %s:\n",
		migd_Pid, migd_Version, migd_HostName);
	fprintf(stderr, "\trun at %s", ctime(&t));
    }

    /*
     * We're trying to become the master for the global pdev.
     */
    migd_GlobalMaster = 1;

    /*
     * Open pdev.  If someone else beats us to it, that's fine, let
     * him handle it.
     */
    if (MigPdev_OpenMaster() < 0){
	return(-1);
    }

    /*
     * Create a regular file that we use for detecting changes in master,
     * since unlinking a pdev may cause the file descriptor to be reused
     * prematurely.  If we can't create it, there's an error.  It may
     * be that right after we unlink it, someone else creates it, in
     * which case that process will become the global master.  The pdev
     * is always opened successfully before the lock file is removed
     * and rewritten.
     */
    (void) unlink(MIGD_LOCK_FILE);
    lockDesc = open(MIGD_LOCK_FILE, O_WRONLY|O_CREAT|O_EXCL, 0644);
    if (lockDesc < 0) {
	perror("open (lock file)");
	return(-1);
    }
    sprintf(pidString, "%x\n", migd_Pid);
    if (write(lockDesc, pidString, strlen(pidString) + 1) !=
	strlen(pidString) + 1) {
	perror("write");
	return(-1);
    }
    
    if (fstat(lockDesc, &descAtts) < 0) {
	SYSLOG1(LOG_ERR, "Exiting: unable to stat lock descriptor: %s.\n",
	       strerror(errno));
	exit(1);
    }

    /*
     * Log a record saying we've started.  This is to a separate file from
     * the usual error/debug log.
     */
    logFile = fopen(MIGD_LOG_FILE, "a");
    if (logFile == (FILE *) NULL) {
	fprintf(stderr, "Error opening %s: %s\n", MIGD_LOG_FILE, 
		strerror(errno));
    } else {
	int t = time(0);
	fprintf(logFile, "process %x version %u on host %s:\n",
		migd_Pid, migd_Version, migd_HostName);
	fprintf(logFile, "\trun at %s", ctime(&t));
	fclose(logFile);
    }
    
    /*
     * We are the master.  Initialize the arrays of hosts and
     * architecture types.
     */
    archTypesArray = (char **) Malloc(sizeof(char *) * maxArchTypes);
    bzero((char *) archTypesArray, sizeof(char *) * maxArchTypes);

    migInfoArray = (Migd_Info **) Malloc(sizeof(Migd_Info *) *
					(maxHosts + 1));
    bzero((char *) migInfoArray, sizeof(Migd_Info *) *
	  (maxHosts + 1));

    /*
     * Go through the host database and set up an empty entry for
     * each host.  Also, find all the architecture types to establish
     * a mapping from host to type.
     */
    Host_Start();
    while (1) {
	hostPtr = Host_Next();
	if (hostPtr == (Host_Entry *) NULL) {
	    break;
	}
	CreateMigdRecord(hostPtr);
    }
    Host_End();

    for (i = 0; i < MIG_NUM_PRIORITIES; i++) {
	idleHostsArray[i] = (List_Links *) Malloc(maxArchTypes *
						  sizeof(List_Links));
	inUseArray[i] = (List_Links *) Malloc(maxArchTypes *
						  sizeof(List_Links));
	waitingLists[i] = (List_Links *) Malloc(maxArchTypes *
						sizeof(List_Links));
	for (j = 0; j < maxArchTypes; j++) {
	    List_Init(&idleHostsArray[i][j]);
	    List_Init(&inUseArray[i][j]);
	    List_Init(&waitingLists[i][j]);
	}
    }

    /*
     * Initialize statistics based on our own idea of the world.  This is
     * used to ensure that things haven't changed (for example, the
     * mapping of architecture types to id's).  
     */
    InitStats();

    
    /*
     * Find out the last time we heard from each host, in order to
     * list downtimes for hosts that aren't currently up.  Get the stats
     * saved by the last daemon.
     */
    RestoreCheckPoint();

    period.seconds = global_CheckpointInterval;
    period.microseconds = 0;
    Fs_TimeoutHandlerDestroy(migd_TimeoutToken);
    migd_TimeoutToken = Fs_TimeoutHandlerCreate(period, TRUE, SaveCheckPoint,
						(ClientData) NULL);

    return (0);
}


/*
 *----------------------------------------------------------------------
 *
 * InitStats --
 *
 *	Initialize structures relating to statistics.  This is called
 * 	at startup, if an error occurs reading the last checkpoint, or
 *	via an ioctl.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The statistics are zeroed, and a few fields are set.
 *
 *----------------------------------------------------------------------
 */

static void
InitStats()
{
    int i;
    
    bzero((char *) &global_Stats, sizeof(global_Stats));
    global_Stats.version = migd_Version;
    global_Stats.checkpointInterval = global_CheckpointInterval;
    global_Stats.firstRun = time(0);
    global_Stats.restarts = 1;
    for (i = 0; i < maxArchTypes && i < MIG_MAX_ARCH_TYPES &&
	 archTypesArray[i] != (char *) NULL; i++) {
	strncpy(global_Stats.archStats[i].arch, archTypesArray[i],
		MIG_MAX_ARCH_LEN);
    }
    global_Stats.maxArchs = i;

    /*
     * All other fields are zero.
     */
}

/*
 *----------------------------------------------------------------------
 *
 * CreateMigdRecord --
 *
 *	Allocate and initialize a Migd_Info record given a Host_Entry
 *	record.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates memory.  May create a new archType.  May increase
 *	count of maximum known hostID.
 *
 *----------------------------------------------------------------------
 */

static void
CreateMigdRecord(hostPtr)
    Host_Entry *hostPtr;
{
    int archType;
    int host;
    Migd_Info *migdPtr;
    int i;
    char *hp;
    int nameLen;

    host = hostPtr->id;
    if (host <= 0) {
	SYSLOG1(LOG_ERR, "invalid host ID from Host_Next: %d.\n",
	       host);
	return;
    } else if (host > maxHosts) {
	Migd_Info **newHostArray;

	/*
	 * Exceeded bounds of array.  Make it double the highest
	 * host known and continue.
	 */
	SYSLOG2(LOG_WARNING,
	       "Host identifier (%d) exceeded maximum host ID (%d).  Need to recompile program\n.",
	       host,
	       maxHosts);
	newHostArray = (Migd_Info **) Malloc(sizeof(Migd_Info *) *
					     (host * 2 + 1));
	bcopy((char *) migInfoArray, (char *) newHostArray,
	      sizeof(Migd_Info *) * (maxHosts + 1));
	bzero((char *) &newHostArray[maxHosts], sizeof(Migd_Info *) *
	      (host  + 1));
	free((char *) migInfoArray);
	migInfoArray = newHostArray;
	maxHosts = host * 2;
    }
		
    for (i = 0; i < maxArchTypes; i++) {
	if (archTypesArray[i] == (char *) NULL) {
	    archTypesArray[i] = Malloc(strlen(hostPtr->machType) + 1);
	    strcpy(archTypesArray[i], hostPtr->machType);
	    archType = i;
	    break;
	}
	if (strcmp(hostPtr->machType, archTypesArray[i]) == 0) {
	    archType = i;
	    break;
	}
    }
    if (i == maxArchTypes) {
	char **newArray;
	/*
	 * Exceeded bounds of array.  Double its size and continue.
	 */
	SYSLOG0(LOG_WARNING,
	       "Too many architecture types -- should recompile program\n.");
	newArray = (char **) Malloc(sizeof(char *) * maxArchTypes * 2);
	bcopy((char *) archTypesArray, (char *) newArray,
	      sizeof(char *) * maxArchTypes);
	bzero((char *) &newArray[maxArchTypes], sizeof(char *) *
	      maxArchTypes);
	free((char *) archTypesArray);
	archTypesArray = newArray;
	archTypesArray[maxArchTypes] =
	    Malloc(strlen(hostPtr->machType) + 1);
	strcpy(archTypesArray[maxArchTypes], hostPtr->machType);
	archType = maxArchTypes;
	maxArchTypes *= 2;
    }

    /*
     * Allocate and initialize a record for this host.  We assume
     * it is down until we hear from it.
     */
    migdPtr = mnew(Migd_Info);
    bzero((char *) migdPtr, sizeof(Migd_Info));
    migdPtr->archType = archType;
    migdPtr->info.state = MIG_HOST_DOWN;
    hostCounts[archType][MIG_HOST_DOWN]++;
    migdPtr->info.hostID = host;
    if (host > maxKnownHost) {
	maxKnownHost = host;
    }
    migdPtr->lastHostAssigned = -1;
    List_Init(&migdPtr->links);
    List_Init(&migdPtr->clientList);

    for (hp = hostPtr->name, nameLen = 0; *hp != '\0' && *hp != '.'; hp++,
	 nameLen++) {
    }
    migdPtr->name = Malloc(nameLen + 1);
    bcopy(hostPtr->name, migdPtr->name, nameLen);
    migdPtr->name[nameLen] = '\0';

    migInfoArray[host] = migdPtr;
	
}


/*
 *----------------------------------------------------------------------
 *
 * Global_End --
 *
 *	Terminate service of the master end of the pdev.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The pdev is removed and closed.
 *
 *----------------------------------------------------------------------
 */

void
Global_End()
{
    if (global_Debug > 0) {
	PRINT_PID;
	fprintf(stderr, "Global_End -\n");
    }

    
    
    MigPdev_End();
    if (global_Debug > 0) {
	PRINT_PID;
	fprintf(stderr, "Global_End - exiting.\n");
    }
    (void) unlink(MIGD_LOCK_FILE);
    DATE();
    exit(0);

}


/*
 *----------------------------------------------------------------------
 *
 * Global_Quit --
 *
 *	Called at exit.  If any outstanding daemons exist, it tells
 *	them to quit as well.  This is normally done because of a
 *	signal such as SIGINT (i.e., more for debugging than
 *	production use).  Note, it uses the sprite signal numbering
 *	scheme because it bypasses the compatibility library.  If
 *	there are outstanding clients it doesn't actually exit itself;
 *	instead the daemon waits until the last client exits (or
 *	another signal occurs).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *      Clients are signalled and should exit ASAP.
 *
 *----------------------------------------------------------------------
 */

void
Global_Quit()
{
    if (hostsUp == 0) {
	Global_End();
    } else {
	MigPdev_SignalClients(SIG_TERM);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Global_GetLoadInfo --
 *
 *	Get the information for one or more hosts and put it in a buffer.
 *	This routine is called via a callback during an ioctl.  
 *
 * Results:
 *	The size of the output buffer used is returned in *outBufSizePtr.
 *	On error, a non-zero error status is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_GetLoadInfo(cltPtr, command, inBuffer, inBufSize, outBuffer,
		   outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results. */
    int *outBufSizePtr;		/* Size of the output buffer, modified with
				   amount used. */
{
    Mig_InfoRequest *infoReqPtr;/* Request from client. */
    int i;			/* Counter. */
    int total;			/* Number of entries found. */
    int firstHost;		/* Sprite ID of first host to return info
				   for. */
    int numRecs;		/* Maximum number of entries to return. */
    char *bufPtr;		/* Pointer into output buffer. */
    Mig_Info *infoPtr;		/* Pointer into output buffer. */
    int *totalPtr;		/* Pointer into output buffer. */
    
    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr,
	       "Global_GetLoadInfo: called from process %x\n",
	       cltPtr->processID);
    }
    if (inBufSize != sizeof(Mig_InfoRequest)) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_GetLoadInfo: bad input buffer size (%d) from process %x\n",
		   inBufSize, cltPtr->processID);
	}
	return(EINVAL);
    }
    global_Stats.getLoadRequests++;
    infoReqPtr = (Mig_InfoRequest *) inBuffer;
    firstHost = infoReqPtr->firstHost;
    numRecs = infoReqPtr->numRecs;
    if (firstHost <= 0 ||
	*outBufSizePtr < (sizeof(int) + numRecs * sizeof(Mig_Info))) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_GetLoadInfo: bad firstHost argument (%d) or output  buffer size (%d)\n",
		   firstHost,*outBufSizePtr);
	    SYSLOG2(LOG_WARNING,
		   "\tfor %d records, from process %x\n", numRecs,
		    cltPtr->processID);
	}
	return(EINVAL);
    }

    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr,
	       "Global_GetLoadInfo: requested %d hosts starting at %d\n",
	       numRecs, firstHost);
    }
    bufPtr = outBuffer;
    totalPtr = (int *) bufPtr;
    bufPtr += 2 * sizeof(int);
    infoPtr = (Mig_Info *) bufPtr;
    for (i = firstHost, total = 0; i <= maxKnownHost && total < numRecs;
	 i++) { 
	if (migInfoArray[i] != (Migd_Info *) NULL &&
	    migInfoArray[i]->info.loadVec.timestamp != 0) {
	    bcopy((char *) &migInfoArray[i]->info, (char *) infoPtr,
		  sizeof(Mig_Info));
	    infoPtr++;
	    total++;
	    if (global_Debug > 4) {
		PRINT_PID;
		fprintf(stderr,
		       "Global_GetLoadInfo: entry %d is %s\n",
		       total, migInfoArray[i]->name);
	    }
	}
    }
    *totalPtr = total;
    *outBufSizePtr = 2 * sizeof(int) + total * sizeof(Mig_Info);
    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr,
	       "Global_GetLoadInfo: returning %d bytes for %d entries.\n",
	       *outBufSizePtr, total);
    }
    return(0);
    
}


/*
 *----------------------------------------------------------------------
 *
 * Global_GetIdle --
 *
 *	Get one or more idle hosts and put their IDs in a buffer.
 *	This routine is called via a callback during an ioctl.  
 *
 * Results:
 *	The size of the output buffer used is returned in *outBufSizePtr.
 *	On error, a non-zero error status is returned.
 *
 * Side effects:
 *	The hosts obtained may be shuffled to another queue.
 *	Allocates various structures.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_GetIdle(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results. */
    int *outBufSizePtr;		/* Size of the output buffer, modified with
				   amount used. */
{
    Mig_IdleRequest *idleReqPtr;/* Request from client. */
    int *outPtr;		/* Pointer into output buffer. */
    int total;			/* Number of entries found. */
    int numHosts;		/* Maximum number of host IDs to return. */
    int priority;		/* Priority of process. */
    int tryPrio;		/* Temporary variable for priority. */
    List_Links *idleList;	/* Pointer to header of a list of idle
				   hosts. */
    List_Links *listPtr;	/* Current element in list. */
    List_Links *nextPtr;	/* Next element in list. */
    int	archType;		/* Numeric identifier for client
				   architecture. */
    int	migVersion;		/* Migration version of requester's host. */
    Migd_Info *migdPtr;		/* Pointer to internal form of info for a
				   host. */
    Migd_Info *reqMigdPtr;	/* Pointer to info for requester's machine. */
    Mig_Info *infoPtr;		/* Pointer to external form of info for
				   host. */
    RequestInfo *reqPtr;	/* Pointer to info for a request. */
    Mig_ArchStats *archPtr;	/* For statistics. */
    int retry;			/* Used for looping on requests. */

    if (inBufSize != sizeof(Mig_IdleRequest)) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_GetIdle: bad input buffer size (%d) from process %x\n",
		   inBufSize, cltPtr->processID);
	}
	return(EINVAL);
    }
    idleReqPtr = (Mig_IdleRequest *) inBuffer;
    numHosts = idleReqPtr->numHosts;


    reqMigdPtr = migInfoArray[cltPtr->host];
    if (reqMigdPtr == (Migd_Info *) NULL ||
	reqMigdPtr->info.state == MIG_HOST_DOWN) {
	if (global_Debug > 0) {
	    char buf[10];
	    SYSLOG1(LOG_WARNING,
		   "Global_GetIdle: no daemon for requester's host (%s).\n",
		    reqMigdPtr == (Migd_Info *) NULL ?
		    sprintf(buf, "%d", cltPtr->host) : reqMigdPtr->name);
	}
	return(ENOTCONN);
    }
	
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr,
	       "Global_GetIdle: request for %d hosts from process %x on %s\n",
	       numHosts, cltPtr->processID, reqMigdPtr->name);
    }

    priority = idleReqPtr->priority;
    if (numHosts <= 0 ||
	*outBufSizePtr < (1 + numHosts) * sizeof(int) ||
	priority < MIG_LOW_PRIORITY ||
	priority > MIG_HIGH_PRIORITY) {
	if (global_Debug > 0) {
	    PRINT_PID;
	    fprintf(stderr,
		   "Global_GetIdle: bad number of hosts requested (%d), priority (%d), or output buffer size (%d) from process %x\n",
		   numHosts, priority, *outBufSizePtr, cltPtr->processID);
	}
	return(EINVAL);
    }

    archType = reqMigdPtr->archType;
    migVersion = reqMigdPtr->info.migVersion;

    if (migd_DoStats) {
	archPtr = &global_Stats.archStats[archType];
	if (cltPtr->numRequested == 0) {
	    if (global_Debug > 2) {
		PRINT_PID;
		fprintf(stderr,
		       "Global_GetIdle: first request from process %x.\n",
		       cltPtr->processID);
	    }
	    archPtr->numClients++;
	}
	cltPtr->numRequested += numHosts;
	if (cltPtr->numInUse + numHosts > cltPtr->maxRequests) {
	    cltPtr->maxRequests = cltPtr->numInUse + numHosts;
	}
	global_Stats.totalRequests += numHosts;
    }

    /*
     * Try to assign as many hosts as requested.  (At some point we
     * might put in some sort of fairness constraints here.)  We
     * look at hosts that are available for the given priority or lower,
     * meaning that a normal priority job can run on a machine that's
     * already being used for a background job but not vice-versa.
     */

    outPtr = (int *) (outBuffer + sizeof(int));

    total = 0;
    retry = 1;
    while (retry) {

	tryPrio = MIG_LOW_PRIORITY;
	while (total < numHosts && tryPrio <= priority) {
	    idleList = &idleHostsArray[tryPrio][archType];
	    nextPtr = List_First(idleList);
	    while (total < numHosts && !List_IsAtEnd(idleList, nextPtr)) {
		listPtr = nextPtr;
		nextPtr = List_Next(nextPtr);
		migdPtr = (Migd_Info *) listPtr;
		if (global_Debug > 3) {
		    PRINT_PID;
		    fprintf(stderr, "Examining %s.\n",
			    migdPtr->name);
		}
		if (migdPtr->info.hostID == idleReqPtr->virtHost ||
		    migdPtr->info.hostID == cltPtr->host) {
		    if (global_Debug > 3) {
			PRINT_PID;
			fprintf(stderr, "Skipping over %s.\n",
				migdPtr->name);
		    }
		    continue;
		}
		if (migdPtr->info.migVersion != migVersion) {
		    if (global_Debug > 3) {
			PRINT_PID;
			fprintf(stderr,
				"Migration version mismatch: %s version %d, %s version %d.\n",
				migdPtr->name, migdPtr->info.migVersion,
				reqMigdPtr->name, migVersion);
		    }
		    continue;
		}
		infoPtr = (Mig_Info *) &migdPtr->info;
		infoPtr->foreign[priority]++;
		if (infoPtr->foreign[priority] >= infoPtr->maxProcs) {
		    /*
		     * Used up all the processors on this host for processes
		     * of this type, so put the host on the queue for
		     * the next higher priority.
		     */
		    List_Remove(listPtr);
		    hostCounts[archType][infoPtr->state]--;
		    if (priority < MIG_HIGH_PRIORITY) {
			List_Insert(listPtr,
				    LIST_ATREAR(&idleHostsArray
						[priority + 1][archType]));
			infoPtr->state = MIG_HOST_PART_USED;
		    } else {
			infoPtr->state = MIG_HOST_FULL;
		    }
		    hostCounts[archType][infoPtr->state]++;
		}
		/*
		 * Allocate a new request info structure and set it up
		 * so we can find this request via the host's record or via the
		 * client process.  Only chain it to the client process if
		 * it isn't an "agent" request, since we reclaim hosts on the
		 * client's chain of requests when the connection is closed.
		 *
		 * XXX We'd like to assign multiple processors in a single shot
		 * here...
		 */
		reqPtr = mnew(RequestInfo);
		reqPtr->priority = priority;
		reqPtr->migdPtr = migdPtr;
		reqPtr->flags = idleReqPtr->flags;
		reqPtr->idleTime = migdPtr->info.loadVec.noInput;
		reqPtr->numProcessors = 1;
		if (! (reqPtr->flags & MIG_PROC_AGENT)) {
		    reqPtr->client.cltPtr = cltPtr;
		    List_InitElement(&reqPtr->nextClientRequest);
		    List_Insert(&reqPtr->nextClientRequest,
				LIST_ATREAR(&cltPtr->currentRequests));
		} else {
		    reqPtr->client.processID = cltPtr->processID;
		    migdPtr->flags |= MIGD_CHECK_COUNT;
		}
		migdPtr->flags &= ~MIGD_WAS_EMPTY;
		if (migd_DoStats) {
		    int noInput;

		    global_Stats.totalObtained++;
		    noInput = (infoPtr->loadVec.noInput + 30) / 60 ;
		    reqPtr->timestamp = time(0);
		    reqPtr->idleTime = noInput;
		    ADD_WITH_OVERFLOW(archPtr->counters.hostIdleObtained, noInput);
		    ADD_WITH_OVERFLOW(archPtr->squared.hostIdleObtained,
				      noInput * noInput);
		    /*
		     * Is this host being assigned to the last host that was
		     * using it?
		     */
		    if (migdPtr->lastHostAssigned == cltPtr->host) {
			if (global_Debug > 4) {
			    PRINT_PID;
			    fprintf(stderr, "\t** Repeat assignment:\n");
			}
			global_Stats.numRepeatAssignments++;
		    } else if (migdPtr->lastHostAssigned == -1) {
			if (global_Debug > 4) {
			    PRINT_PID;
			    fprintf(stderr, "\t** First assignment to host:\n");
			}
			global_Stats.numFirstAssignments++;
		    }
		    migdPtr->lastHostAssigned = cltPtr->host;

		    if (cltPtr->host == lastRequestHost) {
			if (global_Debug > 4) {
			    PRINT_PID;
			    fprintf(stderr, "\t** Repeat requesting host:\n");
			}
			global_Stats.numRepeatRequests++;
		    }
		    lastRequestHost = cltPtr->host;
		}

		List_InitElement(&reqPtr->nextHostRequest);
		List_Insert(&reqPtr->nextHostRequest,
			    LIST_ATREAR(&migdPtr->clientList));

		List_InitElement(&reqPtr->nextInUse);
		List_Insert(&reqPtr->nextInUse,
			    LIST_ATREAR(&inUseArray[priority + 1][archType]));
	    
		if (global_Debug > 2) {
		    PRINT_PID;
		    fprintf(stderr, "\t** Assigning %s to process %x.\n",
			    migdPtr->name, cltPtr->processID);
		}
		total++;
		*outPtr = infoPtr->hostID;
		outPtr++;
	    }
	    tryPrio++;
	}
	if (total < numHosts) {
	    retry = CheckFairness(cltPtr, priority);
	} else {
	    retry = 0;
	}

    }
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "Global_GetIdle: allocated %d hosts to process %x\n",
	       total, cltPtr->processID);
    }

    outPtr = (int *) outBuffer;
    *outPtr = total;
    *outBufSizePtr = (total + 1) * sizeof(int);

    cltPtr->numInUse += total;
    if (migd_DoStats) {
	cltPtr->numObtained += total;
	if (cltPtr->numInUse > cltPtr->maxObtained) {
	    cltPtr->maxObtained = cltPtr->numInUse;
	}
    }
    
    if (total < numHosts && cltPtr->waitPtr == (Migd_WaitList *) NULL) {
	List_Links *waitingList = &waitingLists[priority][archType];
	Migd_WaitList *waitPtr = mnew(Migd_WaitList);

	if (global_Debug > 2) {
	    PRINT_PID;
	    fprintf(stderr, "\t** Process %x put on waiting list.\n",
		   cltPtr->processID);
	}
	List_InitElement(&waitPtr->links);
	waitPtr->cltPtr = cltPtr;
	cltPtr->waitPtr = waitPtr;
	List_Insert(&waitPtr->links, LIST_ATREAR(waitingList));
	cltPtr->denied = 1;
    }


    return(0);
    
}


/*
 *----------------------------------------------------------------------
 *
 * CheckFairness --
 *
 *	Check on how hosts are allocated to see if hosts should be
 *	reclaimed.
 *
 * Results:
 *	Returns 1 if more hosts are available, or 0 if not.
 *
 * Side effects:
 *	May send message to migration daemons to ask them to evict.
 *	Flags the owner of the processes that we kick off so that
 *	that process won't be assigned hosts anytime soon if there
 *	are other processes that want hosts.
 *
 *----------------------------------------------------------------------
 */

static int
CheckFairness(cltPtr, priority)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int priority;		/* Priority of process allocated. */
{
    RequestInfo *reqPtr;
    Migd_Info *cltMigdPtr;	/* Pointer to info for requesting client's
				   machine. */
    Migd_Info *migdPtr;		/* Pointer to info for various other
				   machines. */
    int	migVersion;		/* Migration version of requester's host. */
    Migd_OpenStreamInfo *reqCltPtr; /* Client that made request we're looking
				       at. */
    RequestInfo *bestReqPtr = NULL; /* Best host we've seen so far. */
    Migd_OpenStreamInfo *bestCltPtr; /* Client using that host with this
					priority. */
    List_Links *listPtr;	/* Pointer to chain through list. */
    int okayToUse;		/* Flag while stepping through loop. */
    int checkPrio;		/* Priority index in loop. */


    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr,
	       "CheckFairness called: client %x priority %d.\n",
	   cltPtr->processID, priority);
    }
	


    cltMigdPtr = migInfoArray[cltPtr->host];
    migVersion = cltMigdPtr->info.migVersion;

    /*
     * Initialize bestCltPtr to ourselves so we only look at clients
     * with more hosts in use than we have.  We also fudge our count
     * to ensure that we don't steal from someone with 2 hosts to give a
     * host to someone with one host.  
     */
    bestCltPtr = cltPtr;
    cltPtr->numInUse++;
    
    LIST_FORALL(&inUseArray[priority + 1][cltMigdPtr->archType], listPtr) {
	reqPtr = NEXT_HOST_IN_USE_TO_INFO(listPtr);
	if (reqPtr->flags & (MIG_PROC_AGENT)) {
	    if (global_Debug > 3) {
		PRINT_PID;
		fprintf(stderr,
		       "CheckFairness skipping request on host %d, flags 0x%x.\n",
			reqPtr->migdPtr->info.hostID, reqPtr->flags);
	    }
	    continue;
	}
	reqCltPtr = reqPtr->client.cltPtr;
	migdPtr = migInfoArray[reqCltPtr->host];
	if (migdPtr->info.migVersion != migVersion) {
	    if (global_Debug > 3) {
		PRINT_PID;
		fprintf(stderr,
		       "CheckFairness skipping request on host %d, migration version %d.\n",
			reqPtr->migdPtr->info.hostID, migdPtr->info.migVersion);
	    }
	    continue;
	}
	/*
	 * We want to see if the host that was already is assigned is the
	 * same as the host making the new request, in which case there's
	 * no point in reclaiming it since the new requester won't use it.
	 */
	if (reqPtr->migdPtr->info.hostID == cltPtr->host) {
	    if (global_Debug > 3) {
		PRINT_PID;
		fprintf(stderr,
		       "CheckFairness skipping request using host %d assigned to process %x: same host as new requester.\n",
			reqPtr->migdPtr->info.hostID, reqCltPtr->processID);
	    }
	    continue;
	}
	/*
	 * Only reclaim a host if it's assigned to only one process, at
	 * least for now.  Otherwise it will evict a process and we'll
	 * never notify the process's agent.  We check for a count of 1
	 * at the priority we're evicting and 0 elsewhere.
	 */
	for (okayToUse = 1, checkPrio = MIG_LOW_PRIORITY;
	     okayToUse && checkPrio <= MIG_HIGH_PRIORITY;
	     checkPrio++) {
	    if (reqPtr->migdPtr->info.foreign[checkPrio] >
		((priority == checkPrio) ? 1 : 0)) {
		if (global_Debug > 1) {
		    PRINT_PID;
		    fprintf(stderr,
			    "CheckFairness skipping request using host %d because more than one client assigned to this host.\n",
			    reqPtr->migdPtr->info.hostID);
		}
		okayToUse = 0;
	    }
	}
	if (!okayToUse) {
	    continue;
	}
	if (global_Debug > 5) {
	    PRINT_PID;
	    fprintf(stderr,
		   "CheckFairness comparing client %x (%d hosts) to client %x (%d hosts).\n",
		    reqCltPtr->processID, reqCltPtr->numInUse,
		    bestCltPtr->processID, bestCltPtr->numInUse);
	}
	if (reqCltPtr->numInUse > bestCltPtr->numInUse) {
	    if (global_Debug > 4) {
		PRINT_PID;
		fprintf(stderr,
		       "CheckFairness choosing client %x using %d hosts, this one %d.\n",
		   reqCltPtr->processID, reqCltPtr->numInUse,
			bestCltPtr->numInUse);
	    }
	    bestCltPtr = reqCltPtr;
	    bestReqPtr = reqPtr;
	} else if (reqCltPtr == bestCltPtr) {
	    bestReqPtr = reqPtr;
	    if (global_Debug > 4) {
		PRINT_PID;
		fprintf(stderr,
		       "CheckFairness changing to evict client %x on host %d.\n",
		   reqCltPtr->processID, reqPtr->migdPtr->info.hostID);
	    }
	}
    }
    /*
     * Undo the fudge factor.
     */
    cltPtr->numInUse--;
    if (bestCltPtr != cltPtr) {
	if (global_Debug > 1) {
	    PRINT_PID;
	    fprintf(stderr,
		   "CheckFairness evicting processes on host %s.\n",
		    bestReqPtr->migdPtr->name);
	}
	TellClient(bestReqPtr->migdPtr->cltPtr, 0);
	RevokePermission(bestReqPtr, REVOKE_STOLEN);
	List_Remove(&bestReqPtr->nextHostRequest);
	if (HostIsIdle(bestReqPtr->migdPtr)) {
	    List_Remove((List_Links *) bestReqPtr->migdPtr);
	}
	InsertIdle(bestReqPtr->migdPtr);
	free(bestReqPtr);
	return(1);
    }
    return(0);
}


/*
 *----------------------------------------------------------------------
 *
 * Global_DoneIoctl --
 *
 *	Note that a process is through with one or more hosts.
 *	This routine is called via a callback during an ioctl.
 *	It's really just a stub to call a regular routine without
 * 	the extra args.
 *
 * Results:
 *	On error, a non-zero error status is returned, else 0.
 *
 * Side effects:
 *	The hosts are put back on appropriate queues.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_DoneIoctl(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results, not used. */
    int *outBufSizePtr;		/* Size of the output buffer, set to 0. */
{
    int *hostPtr;		/* Pointer into input buffer. */
    int i;			/* Counter. */
    int numArgs;		/* Number of args (host IDs). */
    int status;			/* Status from subroutine calls. */

    if ((inBufSize % sizeof(int)) != 0 || inBufSize <= 0) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_DoneIoctl: bad input buffer size (%d) from process %x\n",
		   inBufSize, cltPtr->processID);
	}
	return(EINVAL);
    }
    numArgs = inBufSize / sizeof(int);
    hostPtr = (int *) inBuffer;
    for (i = 0; i < numArgs; i++, hostPtr++) {
	status = Global_Done(cltPtr, *hostPtr, 0);
	if (status != 0) {
	    return(status);
	}
    }
    return(0);
}



/*
 *----------------------------------------------------------------------
 *
 * Global_Done --
 *
 *	Note that a process is through with one or more hosts.
 *	Takes a single host specification, which may be MIG_ALL_HOSTS
 *	to indicate that all hosts are to be returned.
 *
 * Results:
 *	Returns 0.
 *
 * Side effects:
 *	The hosts may be moved to a different queue.  Memory is freed.
 *
 *----------------------------------------------------------------------
 */

int
Global_Done(cltPtr, host, exiting)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client involved. */
    int host;			/* Host specification. */
    int exiting;		/* Whether this call is because the process
				   is exiting. */
{
    int priority;		/* Priority of process. */
    List_Links *cltList;	/* Pointer to header of client's list of
				   hosts it's using. */
    List_Links *cltListPtr;	/* Pointer into list. */
    List_Links *listPtr;	/* Temporary var: pointer to element of list. */
    List_Links *nextPtr;	/* For iterations, pointer to next element
				   of list. */
    int numReturned = 0;	/* Number of hosts returned. */
    Migd_Info *migdPtr;		/* Pointer to internal form of info for
				   a host. */
    Mig_Info *infoPtr;		/* Pointer to external form of info for
				   host. */
    RequestInfo *reqPtr;	/* Pointer to info for a request. */
    MessageBlock *msgPtr;	/* Pointer to message enqueued for client. */
    int returnCode = 0;		/* Value to return at end. */
    int requests;		/* Number of hosts requested, for statistics. */
    int obtained;		/* Number of hosts obtained, for statistics. */
    Mig_ArchStats *archStatsPtr;/* Pointer to per-architecture statistics. */

    
    if (global_Debug > 2) {
	if (host) {
	    PRINT_PID;
	    fprintf(stderr,
		   "Global_Done: return %s to free pool for process %x\n",
		   migInfoArray[host]->name, cltPtr->processID);
	} else {
	    PRINT_PID;
	    fprintf(stderr,
		   "Global_Done: return all hosts to free pool for process %x\n",
	       cltPtr->processID);
	}
    }

    /*
     * Check the list of hosts this client is using.
     */

    
    cltList = &cltPtr->currentRequests;
    if (List_IsEmpty(cltList)) {
	if (global_Debug > 2) {
	    PRINT_PID;
	    fprintf(stderr,
		   "Global_Done: process %x has empty client list\n",
		   cltPtr->processID);
	}
	goto done;
    }
	

    
    cltListPtr = List_First(cltList);

    /*
     * Go through list of requests looking for this host, or for each
     * host if so specified.
     */

    while (!List_IsAtEnd(cltList, cltListPtr)) {

	reqPtr = (RequestInfo *) cltListPtr;
	migdPtr = reqPtr->migdPtr;
	infoPtr = &migdPtr->info;
	priority  = reqPtr->priority;
	if (host != MIG_ALL_HOSTS && infoPtr->hostID != host) {
	    cltListPtr = List_Next(cltListPtr);
	    continue;
	}
	/*
	 * Found a record for a host assignment we want to remove.
	 * First extract it from the client's list and from the list
	 * associated with the host being used.
	 */
	listPtr = List_Next(cltListPtr);
	List_Remove(cltListPtr);
	cltListPtr = listPtr;
	listPtr = &reqPtr->nextHostRequest;
	List_Remove(listPtr);
	listPtr = &reqPtr->nextInUse;
	List_Remove(listPtr);
	numReturned++;

	infoPtr->foreign[priority] -= reqPtr->numProcessors;
	if (infoPtr->foreign[priority] == infoPtr->maxProcs - 1) {
	    /*
	     * Now have a free processor on this host for processes
	     * of this type, so put the host on the queue for
	     * this priority.
	     */
	    if (HostIsIdle(migdPtr)) {
		List_Remove((List_Links *) migdPtr);
	    }
	    if (infoPtr->loadVec.allowMigration) {
		InsertIdle(migdPtr);
	    } else {
		migdPtr->info.state = MIG_HOST_ACTIVE;
	    }
	} else if (global_Debug > 1) {
	    PRINT_PID;
	    fprintf(stderr,
		   "Foreign count %d of priority %d on %s not just below maxProcs (%d).\n",
		   infoPtr->foreign[priority], priority, migdPtr->name,
		   infoPtr->maxProcs);
	}
	if (migd_DoStats) {
	    int curTime = time(0);
	    int timeUsed = curTime - reqPtr->timestamp;
	    global_Stats.archStats[migdPtr->archType].counters.timeUsed +=
		timeUsed;
	    global_Stats.archStats[migdPtr->archType].squared.timeUsed +=
		timeUsed * timeUsed;
	}
	
	free ((char *) reqPtr);
    }

    done:
    if (exiting) {
	/*
	 * Clean up after the process -- get rid of messages, and the message
	 * list.  Record statistics.
	 */
	listPtr = List_First(&cltPtr->messages);
	while (!List_IsAtEnd(&cltPtr->messages, listPtr)) {
	    nextPtr = List_Next(listPtr);
	    msgPtr = (MessageBlock *) listPtr;
	    if (msgPtr->msg == host || host == MIG_ALL_HOSTS) {
		List_Remove(listPtr);
		free((char *) listPtr);
	    }
	    listPtr = nextPtr;
	}
	if (List_IsEmpty(&cltPtr->messages)) {
	    cltPtr->defaultSelBits &= ~FS_READ;
	}

	if (cltPtr->waitPtr != (Migd_WaitList *) NULL) {
	    List_Remove((List_Links *) cltPtr->waitPtr);
	    free((char *) cltPtr->waitPtr);
	    cltPtr->waitPtr = (Migd_WaitList *) NULL;
	}

	/*
	 * Manage statistics.  We only count processes that have ever
	 * requested hosts, and only when they close their pdev.
	 */
	if (migd_DoStats && cltPtr->numRequested > 0) {
	    archStatsPtr = &global_Stats.archStats
		[migInfoArray[cltPtr->host]->archType];
#ifndef min
#define min(a, b) (((a) < (b)) ? (a) : (b))
#endif
	    requests = min(cltPtr->maxRequests, MIG_MAX_HOSTS_DIST);
	    obtained = min(cltPtr->maxObtained, MIG_MAX_HOSTS_DIST);
	    if (global_Debug > 3) {
		PRINT_PID;
		fprintf(stderr,
			"Global_Done: process %x had a max of %d requests, %d obtained, %d stolen.\n",
			cltPtr->processID, requests, obtained,
			cltPtr->numStolen);
	    }
	    if (cltPtr->denied == 0) {
		archStatsPtr->gotAll++;
	    }
	    archStatsPtr->requestDist[requests]++;
	    archStatsPtr->obtainedDist[obtained]++;
	    archStatsPtr->counters.requested += cltPtr->numRequested;
	    archStatsPtr->squared.requested +=
		cltPtr->numRequested * cltPtr->numRequested;
	    archStatsPtr->counters.obtained += cltPtr->numObtained;
	    archStatsPtr->squared.obtained +=
		cltPtr->numObtained * cltPtr->numObtained;
	    archStatsPtr->counters.evicted += cltPtr->numEvicted;
	    archStatsPtr->squared.evicted +=
		cltPtr->numEvicted * cltPtr->numEvicted;
	    archStatsPtr->counters.reclaimed += cltPtr->numStolen;
	    archStatsPtr->squared.reclaimed +=
		cltPtr->numStolen * cltPtr->numStolen;
	}
    }
    
    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr, "Global_Done: returned %d hosts to pool for process %x\n",
	       numReturned, cltPtr->processID);
    }

    if (numReturned > 0) {
	cltPtr->stoleTime = 0;
    }
    cltPtr->numInUse -= numReturned;
    
    return(returnCode);
    
}


/*
 *----------------------------------------------------------------------
 *
 * InsertIdle --
 *
 *	Add a host to the appropriate list of idle hosts, based on
 *	the priorities with available capacity.  The host must not be
 *	on any such list when this procedure is called.  
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Adds host to list, and sets it state based on whether it has
 *	any capacity already used.
 *
 *----------------------------------------------------------------------
 */

static void
InsertIdle(migdPtr)
    Migd_Info *migdPtr;
{
    List_Links *listPtr;
    List_Links *hdrPtr;
    int priority;
    int archType = migdPtr->archType;
    Mig_Info *infoPtr;
    Migd_Info *compPtr;
    int noInput;
    int found;
    int origState;
    

    WakeupWaiters(archType);
    infoPtr = &migdPtr->info;
    noInput = infoPtr->loadVec.noInput;
    origState = infoPtr->state;
    List_InitElement((List_Links *) migdPtr);
    if (List_IsEmpty(&migdPtr->clientList)) {
	/*
	 * Fast path: host is totally idle, so put it in order
	 * of idle time on the idle list, and before any
	 * hosts with any low priority processes.
	 */
	hdrPtr = &idleHostsArray[MIG_LOW_PRIORITY][archType];
	found = 0;
	LIST_FORALL(hdrPtr, listPtr) {
	    compPtr = (Migd_Info *) listPtr;
	    if (global_Debug > 2) {
		PRINT_PID;
		fprintf(stderr, "InsertIdle: comparing %s (%d secs) to %s (%d secs).\n",
		       migdPtr->name, infoPtr->loadVec.noInput, 
		       compPtr->name, compPtr->info.loadVec.noInput);
	    }
	    if (compPtr->info.loadVec.noInput < noInput ||
		compPtr->info.foreign[MIG_LOW_PRIORITY] > 0) {
		found = 1;
		break;
	    }
	}
	if (found) {
	    if (global_Debug > 2) {
		PRINT_PID;
		fprintf(stderr, "Inserting %s before %s\n",
		       migdPtr->name, compPtr->name);
	    }
	    List_Insert((List_Links *) migdPtr, LIST_BEFORE(listPtr));
	} else {
	    if (global_Debug > 2) {
		PRINT_PID;
		fprintf(stderr, "Inserting %s at end of list\n",
		       migdPtr->name);
	    }
	    List_Insert((List_Links *) migdPtr, LIST_ATREAR(hdrPtr));
	}
	infoPtr->state = MIG_HOST_IDLE;
	goto done;
    } 

    /*
     * Look for the first priority that's filled up and put the process
     * on the queue for the next higher priority.  If none is filled up,
     * stick it on the end of the low-prio queue, after the hosts with
     * no foreign procs at all.
     */

    infoPtr->state = MIG_HOST_PART_USED;
    for (priority = MIG_HIGH_PRIORITY; priority >= MIG_LOW_PRIORITY;
	 priority--) {
	if (infoPtr->foreign[priority] == infoPtr->maxProcs) {
	    if (priority == MIG_HIGH_PRIORITY) {
		if (global_Debug > 1) {
		    PRINT_PID;
		    fprintf(stderr, "%s is actually all taken.\n",
			   migdPtr->name);
		}
		infoPtr->state = MIG_HOST_FULL;
		goto done;
	    } else {
		if (global_Debug > 2) {
		    PRINT_PID;
		    fprintf(stderr, "Inserting %s at end of priority %d\n",
			   migdPtr->name, priority + 1);
		}
		List_Insert((List_Links *) migdPtr,
			    LIST_ATREAR(&idleHostsArray
					[priority + 1][archType]));
		goto done;
	    }
	}
    }
    List_Insert((List_Links *) migdPtr,
		LIST_ATREAR(&idleHostsArray[MIG_LOW_PRIORITY][archType]));

    done:
    /*
     * If host changed state, modify counters accordingly.
     */
    if (origState != infoPtr->state) {
	hostCounts[archType][origState]--;
	hostCounts[archType][infoPtr->state]++;
    }
    
}


/*
 *----------------------------------------------------------------------
 *
 * WakeupWaiters --
 *
 *	Check to see if anyone is waiting for an idle host.  Wake up all
 *	processes waiting for available hosts of this type, if a host is
 *	available at the appropriate priority.  Then let them ask again for
 *	an idle host using normal ioctls.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Makes pdevs for clients readable so they know to contact us.
 *
 *----------------------------------------------------------------------
 */

static void
WakeupWaiters(archType)
    int archType;
{
    List_Links *waitingList;
    List_Links *listPtr;
    Migd_WaitList *waitPtr;
    Migd_OpenStreamInfo *cltPtr;
    MessageBlock *msgPtr;
    int curTime;
    int priority;
    int i;
    

    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr,
	       "WakeupWaiters(%s) called.\n", archTypesArray[archType]);
    }
    curTime = time(0);
    for (priority = MIG_HIGH_PRIORITY; priority >= MIG_LOW_PRIORITY;
	 priority--) {
	if (!List_IsEmpty(&idleHostsArray[priority][archType])) {
	    for (i = priority; i <= MIG_HIGH_PRIORITY; i++) {
		waitingList = &waitingLists[priority][archType];
		while (!List_IsEmpty(waitingList)) {
		    /*
		     * For each process waiting, get rid of the info saying it's
		     * waiting and instead add a message to the process.
		     */
		    listPtr = List_First(waitingList);
		    waitPtr = (Migd_WaitList *) listPtr;
		    List_Remove(listPtr);
		    cltPtr = waitPtr->cltPtr;
		    cltPtr->waitPtr = (Migd_WaitList *) NULL;
		    if (cltPtr->stoleTime > curTime - MIGD_STOLE_WINDOW) {
			if (global_Debug > 0) {
			    PRINT_PID;
			    fprintf(stderr,
				    "\t** not telling process %x about host available, since we stole from it recently.\n",
				    cltPtr->processID);
			}
		    } else {
			TellClient(cltPtr, 0);
			if (global_Debug > 0) {
			    PRINT_PID;
			    fprintf(stderr,
				    "\t** telling process %x about host available.\n",
				    cltPtr->processID);
			}
		    }
		    free((char *) waitPtr);
		}
	    }
	}
    }
}




/*
 *----------------------------------------------------------------------
 *
 * RecordEvictions --
 *
 *	A host is evicting foreign processes.  Make sure it's marked
 *	appropriately, and clean up any state from foreign processes.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Notifies clients of eviction [eventually].  Frees memory associated
 *	with outstanding requests for host.
 *
 *----------------------------------------------------------------------
 */

static void
RecordEvictions(migdPtr)
    Migd_Info *migdPtr;
{
    int down;
    List_Links *listPtr;
    RequestInfo *reqPtr;
    Migd_OpenStreamInfo *cltPtr;
    int priority;
    Mig_ArchStats *archPtr;	/* For statistics. */
    

    down = migdPtr->info.state == MIG_HOST_DOWN;
    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr, "RecordEvictions: %s %s.\n",
	       migdPtr->name, down ? "down" : "evicting foreigners");
    }

    if (migd_DoStats) {
	archPtr = &global_Stats.archStats[migdPtr->archType];
	if (!down) {
	    int noInput = (migdPtr->info.loadVec.noInput + 30) / 60 ;
	    archPtr->nonIdleTransitions++;
	    ADD_WITH_OVERFLOW(archPtr->counters.idleTimeWhenActive,
			      noInput);
	    ADD_WITH_OVERFLOW(archPtr->squared.idleTimeWhenActive,
			      noInput * noInput);
	}
    }


    while (!List_IsEmpty(&migdPtr->clientList)) {
	listPtr = List_First(&migdPtr->clientList);
	List_Remove(listPtr);
	reqPtr = NEXT_HOST_REQUEST_TO_INFO(listPtr);
	if (global_Debug > 1 && !down) {
	    PRINT_PID;
	    fprintf(stderr, "RecordEvictions: %s evicting client %x.\n",
		    migdPtr->name,
		   (reqPtr->flags & MIG_PROC_AGENT) ? 
		   reqPtr->client.processID : 
		   reqPtr->client.cltPtr->processID);
	}
	RevokePermission(reqPtr, REVOKE_EVICT);
	    
	if (migd_DoStats) {
	    int curTime = time(0);
	    int timeToEviction = curTime - reqPtr->timestamp;
	    archPtr->counters.timeToEviction += timeToEviction;
	    archPtr->squared.timeToEviction += timeToEviction * timeToEviction;
	    ADD_WITH_OVERFLOW(archPtr->counters.hostIdleEvicted,
			      reqPtr->idleTime);
	    ADD_WITH_OVERFLOW(archPtr->squared.hostIdleEvicted,
			      reqPtr->idleTime * reqPtr->idleTime);
	    
	}
	free((char *) reqPtr);
    }
    migdPtr->flags &= ~(MIGD_CHECK_COUNT | MIGD_WAS_EMPTY);
    for (priority = MIG_LOW_PRIORITY; priority <= MIG_HIGH_PRIORITY;
	 priority++) {
	migdPtr->info.foreign[priority] = 0;
    }
    /*
     * Clear out the last host assignment, since after a machine evicts or
     * crashes we don't care who last used it.
     */
    migdPtr->lastHostAssigned = -1;

    if (HostIsIdle(migdPtr)) {
	List_Remove((List_Links *) migdPtr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * TellClient --
 *
 *	Pass a number to a client.  It's either the ID of a host that
 *	it no longer has permission to use, or 0 to indicate a new host
 *	may be available.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Allocates a message buffer and adds it to the clients list.  Makes
 * 	the client pdev readable.
 *
 *----------------------------------------------------------------------
 */

static void
TellClient(cltPtr, msg)
    Migd_OpenStreamInfo *cltPtr; /* Info about client. */
    int msg;			 /* ID of host, or 0. */
{
    MessageBlock *msgPtr;

    if (global_Debug > 4) {
	PRINT_PID;
	fprintf(stderr, "TellClient: telling client %x value %d.\n",
	       cltPtr->processID, msg);
    }
	
    msgPtr = mnew(MessageBlock);
    List_InitElement(&msgPtr->links);
    msgPtr->msg = msg;
    if (List_IsEmpty(&cltPtr->messages)) {
	MigPdev_MakeReady(cltPtr->streamPtr, (ClientData) NULL);
    }
    List_Insert((List_Links *) msgPtr, LIST_ATREAR(&cltPtr->messages));
}


/*
 *----------------------------------------------------------------------
 *
 * RevokePermission  --
 *
 *	Notify a client that it has lost its permission to use a host.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sends a message.  Removes request from per-client list and per-machine
 *	list.
 *
 *----------------------------------------------------------------------
 */

static void
RevokePermission(reqPtr, action)
    RequestInfo *reqPtr;
    RevokeAction action;
{
    Migd_Info *migdPtr;
    Migd_OpenStreamInfo *cltPtr;

    migdPtr = reqPtr->migdPtr;
    migdPtr->info.foreign[reqPtr->priority] -= reqPtr->numProcessors;
    
    List_Remove(&reqPtr->nextInUse);
#ifdef DEBUG_LIST_REMOVE
    /*
     * Zap the element just to be sure.
     */
    List_InitElement(&reqPtr->nextInUse);
#endif /* DEBUG_LIST_REMOVE */
    
    if (!(reqPtr->flags & MIG_PROC_AGENT)) {
	List_Remove((List_Links *) reqPtr);
	cltPtr = reqPtr->client.cltPtr;
	TellClient(cltPtr, migdPtr->info.hostID);
	cltPtr->numInUse--;
	if (cltPtr->numInUse == 0) {
	    cltPtr->stoleTime = 0;
	}
	if (action == REVOKE_EVICT) {
	    cltPtr->numEvicted++;
	} else if (action == REVOKE_STOLEN) {
	    cltPtr->numStolen++;
	    cltPtr->stoleTime = time(0);
	}
    } else if (action == REVOKE_STOLEN) {
	/*
	 * Request was on behalf of another process, so we don't
	 * increment the client numStolen field and instead add it
	 * in to the totals independently.  (For regular clients we
	 * accumulate multiple reclaims/client and then the "squared"
	 * statistic weights clustered reclaimed hosts.)
	 */
	if (migd_DoStats) {
	    global_Stats.archStats[migdPtr->archType].counters.reclaimed++;
	    global_Stats.archStats[migdPtr->archType].squared.reclaimed++;
	}
    } else if (action == REVOKE_EVICT) {
	/*
	 * Request was on behalf of another process, so we don't
	 * increment the client numEvicted field and instead add it
	 * in to the totals independently.  (For regular clients we
	 * accumulate multiple evictions/client and then the "squared"
	 * statistic weights clustered evictions.)
	 */
	if (migd_DoStats) {
	    global_Stats.archStats[migdPtr->archType].counters.evicted++;
	    global_Stats.archStats[migdPtr->archType].squared.evicted++;
	}
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Global_GetUpdate --
 *
 *	Return an update message to a client.  This is an ioctl
 * 	invoked by the client when the client's stream becomes
 *	readable.
 *
 * Results:
 *	On error, a non-zero error status is returned, else 0.
 *
 * Side effects:
 *	The message is freed up.  If it's the last message in the
 *	client's queue, the client is changed to no longer be selectable.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_GetUpdate(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from, not used. */
    int inBufSize;		/* Size of the input buffer, not used. */
    char *outBuffer;		/* Buffer to place results. */
    int *outBufSizePtr;		/* Size of the output buffer. */
{
    int *intPtr;
    List_Links *listPtr;
    MessageBlock *msgPtr;
    
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "Global_GetUpdate called by process %x\n",
	       cltPtr->processID);
    }
    if (*outBufSizePtr < sizeof(int)) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_GetUpdate: bad output buffer size (%d) from process %x\n",
		   *outBufSizePtr, cltPtr->processID);
	}
	return(EINVAL);
    }
    if (List_IsEmpty(&cltPtr->messages)) {
	if (global_Debug > 1) {
	    PRINT_PID;
	    fprintf(stderr,
		   "Global_GetUpdate: no message for process %x\n",
		   cltPtr->processID);
	}
	return(EAGAIN);
    }
    listPtr = List_First(&cltPtr->messages);
    List_Remove(listPtr);
    if (List_IsEmpty(&cltPtr->messages)) {
	cltPtr->defaultSelBits &= ~FS_READ;
    }
    msgPtr = (MessageBlock *) listPtr;
    intPtr = (int *) outBuffer;
    *intPtr = msgPtr->msg;
    *outBufSizePtr = sizeof(int);
    free((char *) listPtr);
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr,
	       "Global_GetUpdate: message for process %x is %d\n",
	       cltPtr->processID, msgPtr->msg);
    }
    return(0);
}



/*
 *----------------------------------------------------------------------
 *
 * Global_RemoveHost --
 *
 *	Remove a host completely from the database.  This may be done
 *	when a host is removed from the system, for example.
 *
 * Results:
 *	On error, a non-zero error status is returned, else 0.
 *
 * Side effects:
 *	The host(s) are removed from our lists and a new checkpoint is
 *	written.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_RemoveHost(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results, not used. */
    int *outBufSizePtr;		/* Size of the output buffer, set to 0. */
{
    int *hostPtr;		/* Pointer into input buffer. */
    int i;			/* Counter. */
    int numArgs;		/* Number of args (host IDs). */
    int status;			/* Status from subroutine calls. */

    if ((inBufSize % sizeof(int)) != 0 || inBufSize <= 0) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_RemoveHost: bad input buffer size (%d) from process %x\n",
		   inBufSize, cltPtr->processID);
	}
	return(EINVAL);
    }
    numArgs = inBufSize / sizeof(int);
    hostPtr = (int *) inBuffer;
    for (i = 0; i < numArgs; i++) {
	status = RemoveHost(*hostPtr);
	if (status != 0) {
	    return(status);
	}
    }
    SaveCheckPoint((ClientData) NULL, time_ZeroSeconds);
    return(0);
}



/*
 *----------------------------------------------------------------------
 *
 * RemoveHost --
 *
 *	Remove a single host from our knowledge.
 *
 * Results:
 *	On error, a non-zero error status is returned, else 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
RemoveHost(hostID)
    int hostID;			/* Host to remove. */
{
    Migd_Info *migdPtr;

    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "RemoveHost(%d) -\n", hostID);
    }

    if (hostID > maxHosts || hostID <= 0) {
	if (global_Debug > 0) {
	    PRINT_PID;
	    fprintf(stderr, "RemoveHost: bad value of hostID: %d\n", hostID);
	}
	return(EINVAL);
    }
    migdPtr = migInfoArray[hostID];
    if (migdPtr != (Migd_Info *) NULL) {
	if (migdPtr->info.state != MIG_HOST_DOWN) {
	    if (global_Debug > 0) {
		PRINT_PID;
		fprintf(stderr, "RemoveHost: %s is up; not removing.\n",
			migdPtr->name);
	    }
	    return(EINVAL);
	}
	if (migdPtr->name != (char *) NULL) {
	    free(migdPtr->name);
	}
	free((char *) migdPtr);
	migInfoArray[hostID] = (Migd_Info *) NULL;
    } else if (global_Debug > 0) {
	PRINT_PID;
	fprintf(stderr, "RemoveHost(%d) - didn't know about host.\n", hostID);
    }
    return(0);
}

/*
 *----------------------------------------------------------------------
 *
 * Global_IsHostUp --
 *
 *	Return whether the specified host is up.
 *
 * Results:
 *	TRUE if the host exists and is up, FALSE otherwise.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Global_IsHostUp(hostID)
    int hostID;			/* Host to check on. */
{
    Migd_Info *migdPtr;

    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr, "Global_IsHostUp(%d) -\n", hostID);
    }

    if (hostID > maxHosts || hostID <= 0) {
	SYSLOG1(LOG_ERR, "Global_IsHostUp: bad value of hostID: %d\n", hostID);
	return(0);
    }
    migdPtr = migInfoArray[hostID];
    if ((migdPtr != (Migd_Info *) NULL) &&
	(migdPtr->info.state != MIG_HOST_DOWN)) {
	return(1);
    }
    return(0);
}

/*
 *----------------------------------------------------------------------
 *
 * Global_ChangeState --
 *
 *	A host has changed from accepting processes to not accepting them,
 *	or vice-versa.  If not accepting, then notify processes about
 *	any evictions that might have occurred.
 *
 * Results:
 *	On error, a non-zero error status is returned, else 0.
 *
 * Side effects:
 *	Message(s) sent to clients.  Host may be added to or removed from
 *	list of idle hosts.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_ChangeState(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results, not used. */
    int *outBufSizePtr;		/* Size of the output buffer, set to 0. */
{
    int *statePtr;
    int host;
    Migd_Info *migdPtr;

    if (cltPtr->type != MIGD_DAEMON) {
	if (global_Debug > 0) {
	    PRINT_PID;
	    fprintf(stderr,
		   "Global_ChangeState: non-daemon trying to invoke CHANGE ioctl; pid %x state %x\n",
		   cltPtr->processID, (int) cltPtr->type);
	}
	return(EPERM);
    }

    if (inBufSize != sizeof(int)) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_ChangeState: wrong size input (%d bytes) from process %x\n",
		   inBufSize, cltPtr->processID);
	}
	return(EINVAL);
    }

    host = cltPtr->host;
    statePtr = (int *) inBuffer;

    migdPtr = CltToMigd(cltPtr);
    if (migdPtr == (Migd_Info *) NULL) {
	SYSLOG0(LOG_ERR, "Global_ChangeState: never heard from this host?  Exiting.\n");
	exit(1);
    }

    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr, "Global_ChangeState called for %s => state %d.\n",
	       migdPtr->name,
	       *statePtr);
    }
    
    switch (*statePtr) {
	case MIG_HOST_ACTIVE:
	case MIG_HOST_REFUSES: {
	    /*
	     * Host is evicting any foreign processes.
	     */
	    if (global_Debug > 3) {
		PRINT_PID;
		fprintf(stderr, "Host is active or is refusing migration.\n");
	    }
	    RecordEvictions(migdPtr);
	    break;
	}
	case MIG_HOST_IDLE: {
	    /*
	     * Actually, this case should never be reached.
	     */
	    if (global_Debug > 0) {
		PRINT_PID;
		fprintf(stderr, "Changed state to idle??\n");
	    }
	    InsertIdle(migdPtr);
	    break;
	}
	default: {
	    PRINT_PID;
	    fprintf(stderr, "Invalid state!\n");
	    return(EINVAL);
	}
    }
    hostCounts[migdPtr->archType][migdPtr->info.state]--;
    migdPtr->info.state = *statePtr;
    hostCounts[migdPtr->archType][migdPtr->info.state]++;

    return(0);
}


/*
 *----------------------------------------------------------------------
 *
 * Global_HostUp --
 *
 *	Mark a host as being up, the first time its daemon talks to us.
 *
 * Results:
 *	0 for success, or an errno indicating the error.
 *	EPERM indicates that a non-root process tried to tell us it
 * 	was the daemon.
 *
 * Side effects:
 *	The host is added to the list of active hosts.  
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_HostUp(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results, not used. */
    int *outBufSizePtr;		/* Size of the output buffer, set to 0. */
{
    Mig_Info *infoPtr;
    Migd_Info *migdPtr;
    Host_Entry *hostPtr;
    Migd_OpenStreamInfo *oldCltPtr;  /* Information about any previous client
				        from this host. */
    int status;
    struct timeval time;
    
    if (global_Debug > 3) {
	PRINT_PID;
	fprintf(stderr, "Global_HostUp called.\n");
    }

    status = gettimeofday(&time, (struct timezone *) NULL);
    if (status == -1) {
	SYSLOG1(LOG_ERR, "Error in gettimeofday: %s", strerror(errno));
	exit(1);
    }

    if (inBufSize != sizeof(Mig_Info)) {
	if (global_Debug > 0) {
	    SYSLOG2(LOG_WARNING,
		   "Global_HostUp: bad input buffer size (%d) from process %x\n",
		   inBufSize, cltPtr->processID);
	}
	return(EINVAL);
    }

    /*
     * Flag a connection as belonging to a daemon, permitting
     * it to perform privileged operations.   Set the format
     * value in the info struct, so that writes can be swapped if
     * needed.
     */
#ifndef DEBUG
    if (global_Debug == 0 && cltPtr->user != PROC_SUPER_USER_ID) {
	return(EPERM);
    }
#endif /* DEBUG */


    infoPtr = (Mig_Info *) inBuffer;

    if (cltPtr->host > maxHosts) {
	/*
	 * XXX We should handle this obscure case by growing the array of
	 * hosts, but skip it for the time being.
	 */
	SYSLOG1(LOG_ERR,
	       "Never heard of host %d, and ID is too big.\n", cltPtr->host);
	return(EINVAL);
    }
    if (migInfoArray[cltPtr->host] == (Migd_Info *) NULL) {
	/*
	 * This can happen if a new host is added to the system
	 * after the global daemon starts up, or a host record has been
	 * removed. 
	 */
	hostPtr = Host_ByID(cltPtr->host);
	if (hostPtr == (Host_Entry *) NULL) {
	    SYSLOG1(LOG_ERR, "No entry in host database for host %d.\n",
		   cltPtr->host);
	    return(EINVAL);
	}
	CreateMigdRecord(hostPtr);
	Host_End();
    }
    migdPtr = migInfoArray[cltPtr->host];

    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr,
		"Global_HostUp - %s pid %x boot %d version %d maxProcs %d\n",
		migdPtr->name, cltPtr->processID,
		infoPtr->bootTime, infoPtr->migVersion,
	       infoPtr->maxProcs);
    }

    if (migdPtr->cltPtr != (Migd_OpenStreamInfo *) NULL) {
	oldCltPtr = migdPtr->cltPtr;
	/*
	 * Check once again whether the host is really down.  If a host reboots
	 * quickly, we might otherwise think it's still up and try to signal
	 * a process.  If we're really unlikely, the host rebooted and the
	 * process of the new daemon is the same as the daemon the last time
	 * the host was up, and we'll kill the new daemon (or some other
	 * random process).  We could solve this problem if each boot
	 * incremented some version number that got passed to the global
	 * daemon, but we don't have such a capability (yet).
	 *
	 * Another potential problem: the host migd gets a stale handle
	 * and closes and reopens the global pdev, and we have to recognize
	 * that and refrain from terminating it as if it were a leftover
	 * migd process.
	 */
	if (oldCltPtr->processID == cltPtr->processID) {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr,
			"Global_HostUp - %s reopening unclosed connection.\n",
			migdPtr->name);
	    }
	    (void) Global_HostDown(cltPtr->host, 0);
	} else if (migdPtr->info.loadVec.timestamp <
	    time.tv_sec - MIG_TIMEOUT) {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr,
			"Global_HostUp - marking %s down (curTime %d, updated %d).\n",
			migdPtr->name, time.tv_sec,
			migdPtr->info.loadVec.timestamp);
	    }
	    (void) Global_HostDown(cltPtr->host, 0);
	}
	if (migdPtr->info.state != MIG_HOST_DOWN) {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr,
			"Already talking to a daemon on %s.  Signalling pid %x.\n",
			migdPtr->name, oldCltPtr->processID);
	    }
	    status = Sig_Send(SIG_TERM, oldCltPtr->processID, 0);
	    if (status != SUCCESS) {
		if (global_Debug > 0) {
		    PRINT_PID;
		    fprintf(stderr, "SendSignal: error sending signal to process %x: %s\n",
			    oldCltPtr->processID, Stat_GetMsg(status));
		}
		Global_HostDown(cltPtr->host, 0);
	    } else {
		return(EBUSY);
	    }
	} else {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr,
		       "Received new open from host %s; closing prior stream.\n",
		       migdPtr->name);
	    }
	}
    } 

    /*
     * Check for migds that are a bit confused about their state.
     */
    if (infoPtr->state == MIG_HOST_IDLE && !infoPtr->loadVec.allowMigration) {
	if (global_Debug > 0) {
	    PRINT_PID;
	    fprintf(stderr,
		    "Host %s marked as idle but not allowing migration.\n",
		    migdPtr->name);
	}
	infoPtr->state = MIG_HOST_REFUSES;
    }
#ifdef USE_GLOBAL_CLOCK
    /*
     * Update the time of day to use the global daemon's idea of the time,
     * to keep things in sync and avoid calling a client down just because
     * of clock skew.  Check the bootstamp for clock skew too.
     */
    infoPtr->loadVec.timestamp = time.tv_sec;
    if (infoPtr->bootTime > time.tv_sec) {
	infoPtr->bootTime = time.tv_sec;
    }
#endif /* USE_GLOBAL_CLOCK */
    migdPtr->info = *infoPtr;
    migdPtr->cltPtr = cltPtr;
    cltPtr->type = MIGD_DAEMON;
    cltPtr->defaultSelBits = FS_WRITE;

    /*
     * Add the host to appropriate queue, and modify our counters.
     */
    hostCounts[migdPtr->archType][MIG_HOST_DOWN]--;
    hostCounts[migdPtr->archType][infoPtr->state]++;
    if (infoPtr->state == MIG_HOST_IDLE) {
	InsertIdle(migdPtr);
    }

    hostsUp++;
    
    return(0);
}



/*
 *----------------------------------------------------------------------
 *
 * EndCallBack --
 *
 *	Just a callback used by Fs_Dispatch when we want to quit
 *	but can't at the moment.  It takes (and ignores) the
 *	Fs_Dispatch callback arguments.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Global_End is called, and the process exits.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static void
EndCallBack(clientData, time)
    ClientData clientData;	/* Generic callback arg, not used.  */
    Time time;			/* Generic callback arg, not used.  */
{
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "EndCallBack -\n");
    }
    
    Global_End();
}

/*
 *----------------------------------------------------------------------
 *
 * Global_HostDown --
 *
 *	Mark a host as being down.  This may happen when a pdev for the
 *	daemon on that host is closed, or if it doesn't update its information
 *	in a timely fashion.  If it's really closed, we clean up state
 *	associated with it.
 *
 * Results:
 *	0 for success, or -1 and an errno for failure.
 *
 * Side effects:
 *	The host is removed from any lists of active hosts.  Any clients
 *	accessing the host are notified that it is down.
 *
 *----------------------------------------------------------------------
 */

int
Global_HostDown(hostID, closed)
    int hostID;
    int closed;			/* whether its stream was closed */
{
    Migd_Info *migdPtr;

    if (hostID > maxHosts || migInfoArray[hostID] == (Migd_Info *) NULL) {
	PRINT_PID;
	fprintf(stderr, "Global_HostDown: bad value of hostID: %d\n",
	       hostID);
	errno = EINVAL;
	return(-1);
    }
    migdPtr = migInfoArray[hostID];
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "Global_HostDown(host=%s(%d), closed=%d) called.\n",
	       migdPtr->name, hostID, closed);
    }

    if (closed) {
	/*
	 * We're no longer talking to the daemon with a pdev connection.
	 */
	migdPtr->cltPtr = (Migd_OpenStreamInfo *) NULL;
    } else {
	/*
	 * This connection is unusable... later closes should be ignored.
	 */
	migdPtr->cltPtr->type = MIGD_CLOSED;
    }
    if (HostIsIdle(migdPtr)) {
	List_Remove((List_Links *) migdPtr);
    }
    hostCounts[migdPtr->archType][migdPtr->info.state]--;
    migdPtr->info.state = MIG_HOST_DOWN;
    hostCounts[migdPtr->archType][MIG_HOST_DOWN]++;
    RecordEvictions(migdPtr);

    hostsUp--;
    if (hostsUp <= 0 && migd_Quit) {
	if (closed) {
	    /*
	     * Set up a callback to exit, rather than doing it right now,
	     * since we want to return from any Pdev_Close callback before
	     * trying to close the master.
	     */
	    Fs_TimeoutHandlerDestroy(migd_TimeoutToken);
	    (void) Fs_TimeoutHandlerCreate(time_ZeroSeconds, TRUE,
					   EndCallBack, (ClientData) NULL);
	} else {
	    Global_End();
	}
    }
    return(0);
}


/*
 *----------------------------------------------------------------------
 *
 * Global_UpdateLoad --
 *
 *	Update the load vector corresponding to a host.
 *
 * Results:
 *	0 for success, or -1 for failure, with errno indicating the error.
 *
 * Side effects:
 *	The host may be moved from one queue to another if its status
 *	changes.
 *
 *----------------------------------------------------------------------
 */

int
Global_UpdateLoad(cltPtr, vecPtr)
    Migd_OpenStreamInfo *cltPtr;
    Mig_LoadVector *vecPtr;
{
    int host;
    Migd_Info *migdPtr;
    Mig_LoadVector *oldVecPtr;
    int oldAllow;
    int oldForeign;
    int status;
    struct timeval time;

    host = cltPtr->host;
    
    migdPtr = CltToMigd(cltPtr);
    if (migdPtr == (Migd_Info *) NULL) {
	SYSLOG0(LOG_ERR, "Global_UpdateLoad: never heard from this host?  Exiting.\n");
	exit(1);
    }
    if (global_Debug > 3) {
	PRINT_PID;
	fprintf(stderr, "Global_UpdateLoad called for %s. time %d noInput %d foreign %d allowMigration %d utils %d %d %d lengths %.2f %.2f %.2f\n",
	       migdPtr->name,
	       vecPtr->timestamp,
	       vecPtr->noInput,
	       vecPtr->foreignProcs,
	       vecPtr->allowMigration,
	       vecPtr->utils[0], vecPtr->utils[1], vecPtr->utils[2],
	       vecPtr->lengths[0], vecPtr->lengths[1], vecPtr->lengths[2]);
    }
    if (migdPtr->info.state == MIG_HOST_DOWN) {
	if (global_Debug) {
	    PRINT_PID;
	    fprintf(stderr, "Global_UpdateLoad: Thought %s was down.\n",
		   migdPtr->name);
	}
	hostCounts[migdPtr->archType][MIG_HOST_DOWN]--;
	migdPtr->info.state = vecPtr->allowMigration ? MIG_HOST_ACTIVE :
	    MIG_HOST_REFUSES;
	hostCounts[migdPtr->archType][migdPtr->info.state]++;
	hostsUp++;
    }
    oldVecPtr = &migdPtr->info.loadVec; 
    if (global_Debug > 3) {
	PRINT_PID;
	fprintf(stderr, "Global_UpdateLoad: Old values: time %d noInput %d foreign %d allowMigration %d utils %d %d %d lengths %.2f %.2f %.2f\n",
	       oldVecPtr->timestamp,
	       oldVecPtr->noInput,
	       oldVecPtr->foreignProcs,
	       oldVecPtr->allowMigration,
	       oldVecPtr->utils[0],
	       oldVecPtr->utils[1],
	       oldVecPtr->utils[2],
	       oldVecPtr->lengths[0],
	       oldVecPtr->lengths[1],
	       oldVecPtr->lengths[2]);
    }
    oldAllow = oldVecPtr->allowMigration;
    oldForeign = oldVecPtr->foreignProcs;
    *oldVecPtr = *vecPtr;

#ifdef USE_GLOBAL_CLOCK
    /*
     * Update the time of day to use the global daemon's idea of the time,
     * to keep things in sync and avoid calling a client down just because
     * of clock skew.
     */
    status = gettimeofday(&time, (struct timezone *) NULL);
    if (status == -1) {
	SYSLOG1(LOG_ERR, "Error in gettimeofday: %s", strerror(errno));
	exit(1);
    }
    oldVecPtr->timestamp = time.tv_sec;
#endif /* USE_GLOBAL_CLOCK */
    if (oldAllow && !vecPtr->allowMigration) {
	/*
	 * Host is evicting any foreign processes.
	 */
	if (global_Debug > 2) {
	    PRINT_PID;
	    fprintf(stderr, "\t>>%s is no longer available<<\n", migdPtr->name);
	}
	if (HostIsIdle(migdPtr)) {
	    List_Remove((List_Links *) migdPtr);
	}
	hostCounts[migdPtr->archType][migdPtr->info.state]--;
	migdPtr->info.state = MIG_HOST_ACTIVE;
	hostCounts[migdPtr->archType][MIG_HOST_ACTIVE]++;
    } else if (!oldAllow && vecPtr->allowMigration) {
	if (global_Debug > 2) {
	    PRINT_PID;
	    fprintf(stderr, "\t<<%s is now idle>>\n", migdPtr->name);
	}
	if (HostIsIdle(migdPtr)) {
	    if (global_Debug > 0) {
		PRINT_PID;
		fprintf(stderr,
			"\t ** %s wasn't allowing migration but was idle\n",
			migdPtr->name);
	    }
	} else {
	    InsertIdle(migdPtr);
	}
    } else if (vecPtr->allowMigration && (migdPtr->flags & MIGD_CHECK_COUNT) &&
	       (migdPtr->info.state == MIG_HOST_FULL ||
		migdPtr->info.state == MIG_HOST_PART_USED)) {
	if (oldForeign > 0 && vecPtr->foreignProcs == 0) {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr, "\t--%s now has no foreigners--\n",
			migdPtr->name);
	    }
	    ForceHostIdle(migdPtr);
	} else if (vecPtr->foreignProcs > 0) {
	    /*
	     * If during a checkpoint we noticed that the host was empty, and
	     * now it isn't, we disable the MIGD_WAS_EMPTY flag.  This
	     * way a host will be marked as having no foreign processes
	     * and put back in the pool only if two checkpoints go by without
	     * the host ever having foreign processes at the points when the
	     * per-host migd sends us its data.
	     */
	    migdPtr->flags &= ~MIGD_WAS_EMPTY;
	}
    } else if (vecPtr->allowMigration &&
	       (migdPtr->info.state == MIG_HOST_ACTIVE) &&
	       vecPtr->foreignProcs == 0) {
	/*
	 * Host must have evicted without later making the transition
	 * from not allowing to allowing.  This can happen if eviction
	 * takes a long time or if eviction is done via an ioctl from
	 * a user rather than by detecting input.
	 */
	if (global_Debug > 1) {
	    PRINT_PID;
	    fprintf(stderr, "\t--%s available again--\n",
		   migdPtr->name);
	}
	ForceHostIdle(migdPtr);
    }
    return(0);
}


/*
 *----------------------------------------------------------------------
 *
 * RestoreCheckPoint --
 *
 *	Restore the last known state of the world from a checkpoint
 *	file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The state of each host in the checkpoint, and the statistics structure,
 *	are initialized.
 *
 *----------------------------------------------------------------------
 */

static void
RestoreCheckPoint()
{

    FILE *checkPoint;
    int hostID;
    int timestamp;
    Migd_Info *migdPtr;
    int numScanned;
    int hostsRead = 0;
    char buffer[BUFSIZ];
    
    if (global_Debug > 2) {
	PRINT_PID;
	fprintf(stderr, "RestoreCheckPoint -\n");
    }
    checkPoint = fopen(MIGD_CHECKPOINT_FILE, "r");
    if (checkPoint == (FILE *) NULL) {
	PRINT_PID;
	fprintf(stderr, "RestoreCheckPoint - error reading checkpoint: %s.\n",
	       strerror(errno));
	return;
    }
    if (ReadStats(checkPoint) < 0) {
	PRINT_PID;
	SYSLOG1(stderr,
		"warning: RestoreCheckPoint - error reading statistics from checkpoint: %s.\n",
	       strerror(errno));
	/*
	 * Reinitialize statistics buffer.
	 */
	InitStats();
    }
    while (1) {
	if (fgets(buffer, sizeof(buffer), checkPoint) == (char *) NULL) {
	    break;
	}
	if (buffer[0] == '%') {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr, "RestoreCheckPoint: %s",
		       buffer);
	    }
	    continue;
	}
	numScanned = sscanf(buffer, "%d %d", &hostID, &timestamp);
	if (numScanned < 0) {
	    break;
	}
	if (numScanned != 2) {
	    PRINT_PID;
	    fprintf(stderr,
		   "RestoreCheckPoint - scanned %d items from checkpoint file.\n",
		   numScanned);
	    break;
	}
	if (hostID <= 0 || hostID > maxKnownHost) {
	    PRINT_PID;
	    fprintf(stderr,
		   "RestoreCheckPoint - invalid hostID %d.\n", hostID);
	    break;
	}
	migdPtr = migInfoArray[hostID];
	if (migdPtr == (Migd_Info *) NULL ||
	    migdPtr->info.loadVec.timestamp != 0) {
	    PRINT_PID;
	    fprintf(stderr,
		   "RestoreCheckPoint - host %d NULL or non-zero.\n", hostID);
	    break;
	}
	if (global_Debug > 2) {
	    fprintf(stderr,
		   "<%d,%d> ",
		   hostID, timestamp);
	}
	migdPtr->info.loadVec.timestamp = timestamp;
	if (migdPtr->info.state != MIG_HOST_DOWN) {
	    panic("RestoreCheckPoint: host was not in DOWN state before.\n");
	}
	hostsRead++;
    }
    if (global_Debug > 1) {
	fprintf(stderr, "\n%x: RestoreCheckPoint - read %d hosts.\n",
		migd_Pid, hostsRead);
    }
    (void) fclose(checkPoint);
}


/*
 *----------------------------------------------------------------------
 *
 * SaveCheckPoint --
 *
 *	Save the last known state of the world to a checkpoint
 *	file.  While we're at it, check for hosts we haven't heard from
 *	in a while, and check to make sure we're still controlling the
 * 	real master pdev (someone else could have mistakenly removed it
 * 	and started a second master, and new contacts would reach it instead
 * 	of this process).
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Hosts may be marked as DOWN.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static void
SaveCheckPoint(clientData, timeArg)
    ClientData clientData;	/* Generic callback arg, not used.  */
    Time timeArg;		/* Generic callback arg, not used.  */
{
    FILE *checkPoint;
    Migd_Info *migdPtr;
    int i;
    struct timeval tv;
    int status;
    struct stat nameAtts;
    
    if (global_Debug > 1) {
	int t = time(0);
	PRINT_PID;
	fprintf(stderr, "Global daemon checkpoint: running on %s at %s",
		migd_HostName, ctime(&t));
    }

#ifdef 0
    if (lstat(migd_GlobalPdevName, &nameAtts) < 0) {
	SYSLOG2(LOG_ERR, "Exiting: unable to stat %s: %s.\n",
	       migd_GlobalPdevName, strerror(errno));
	exit(1);
    }
#endif
    if (lstat(MIGD_LOCK_FILE, &nameAtts) < 0) {
	SYSLOG2(LOG_ERR, "Exiting: unable to stat %s: %s.\n",
	       MIGD_LOCK_FILE, strerror(errno));
	exit(1);
    }
    if (nameAtts.st_ino != descAtts.st_ino ||
	nameAtts.st_version != descAtts.st_version) {
	SYSLOG3(LOG_WARNING,
	       "Exiting: mismatch statting files: name inode <%d,%d>, version %d.\n",
	       nameAtts.st_ino, nameAtts.st_devServerID, nameAtts.st_version);
	SYSLOG3(LOG_WARNING,
	       "\tdescriptor inode <%d,%d> version %d (another migd running, or server changed file version).\n",
	       descAtts.st_ino, descAtts.st_devServerID, descAtts.st_version);
	exit(1);
    }

    if (global_Debug > 4) {
	PRINT_PID;
	fprintf(stderr, "Descriptors matched okay.\n");
    }
	

    checkPoint = fopen(MIGD_CHECKPOINT_FILE, "w+");
    if (checkPoint == (FILE *) NULL) {
	SYSLOG1(LOG_WARNING, "SaveCheckPoint - error writing checkpoint: %s.\n",
	       strerror(errno));
	return;
    }
    status = gettimeofday(&tv, (struct timezone *) NULL);
    if (status == -1) {
	SYSLOG1(LOG_ERR, "Error in gettimeofday: %s", strerror(errno));
	exit(1);
    }

    if (migd_DoStats) {
	DumpStats(checkPoint);
    }

    for (i = 1; i <= maxKnownHost; i++) {
	migdPtr = migInfoArray[i];
	if (migdPtr == (Migd_Info *) NULL ||
	    migdPtr->info.loadVec.timestamp == 0) {
	    continue;
	}
	if (fprintf(checkPoint, "%d %d\n", migdPtr->info.hostID,
		    migdPtr->info.loadVec.timestamp) < 0) {
	    SYSLOG1(LOG_WARNING,
		   "SaveCheckPoint - error writing record to checkpoint file: %s.\n",
		   strerror(errno));
	    if (global_Debug == 0) {
		(void) unlink(MIGD_CHECKPOINT_FILE);
	    }
	}
	if (migdPtr->info.state != MIG_HOST_DOWN &&
	    migdPtr->info.loadVec.timestamp < tv.tv_sec - MIG_TIMEOUT) {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr,
		       "SaveCheckPoint - marking %s down (curTime %d, updated %d).\n",
		       migdPtr->name, tv.tv_sec,
		       migdPtr->info.loadVec.timestamp);
	    }
	    (void) Global_HostDown(i, 0);
	} else if (migdPtr->info.loadVec.foreignProcs == 0 &&
		   migdPtr->info.loadVec.allowMigration &&
		   (migdPtr->info.state == MIG_HOST_PART_USED ||
		    migdPtr->info.state == MIG_HOST_FULL)) {
	    if (global_Debug > 1) {
		PRINT_PID;
		fprintf(stderr,
		       "SaveCheckPoint - checking %s foreign count.\n",
		       migdPtr->name);
	    }
	    
	    if (migdPtr->flags & MIGD_WAS_EMPTY) {
		ForceHostIdle(migdPtr);
	    } else {
		migdPtr->flags |= MIGD_WAS_EMPTY;
	    }
	} 
    }
    fflush(checkPoint);
    if (fsync(fileno(checkPoint)) < 0) {
	SYSLOG0(LOG_WARNING, "Error syncing checkpoint file to disk.\n");
    }
    (void) fclose(checkPoint);
}
	

/*
 *----------------------------------------------------------------------
 *
 * DumpStats --
 *
 *	Write statistics out in ASCII to the checkpoint file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static void
DumpStats(file)
    FILE *file;
{
    int i, j;

    global_Stats.intervals++;
    
    fprintf(file, "%% Version %u HasStats %u Interval %u last run on %s.\n",
	    migd_Version, migd_DoStats, global_CheckpointInterval,
	    migd_HostName);
    fprintf(file, "%% firstRun %u\n", global_Stats.firstRun);
    fprintf(file, "%% restarts %u\n", global_Stats.restarts);
    fprintf(file, "%% intervals %u\n", global_Stats.intervals);
    fprintf(file, "%% maxArchs %u\n", global_Stats.maxArchs);
    fprintf(file, "%% getLoadRequests %u\n", global_Stats.getLoadRequests);
    fprintf(file, "%% totalRequests %u\n", global_Stats.totalRequests);
    fprintf(file, "%% totalObtained %u\n", global_Stats.totalObtained);
    fprintf(file, "%% numRepeatRequests %u\n", global_Stats.numRepeatRequests);
    fprintf(file, "%% numRepeatAssignments %u\n",
	    global_Stats.numRepeatAssignments);
    fprintf(file, "%% numFirstAssignments %u\n",
	    global_Stats.numFirstAssignments);
    for (i = 0; i < global_Stats.maxArchs; i++) {
	fprintf(file, "%% arch %u %s\n",
		i, global_Stats.archStats[i].arch);
	fprintf(file, "%% \tnumClients %u\n",
		global_Stats.archStats[i].numClients);
	fprintf(file, "%% \tgotAll %u\n",
		global_Stats.archStats[i].gotAll);
	fprintf(file, "%% \tnonIdleTransitions %u\n",
		global_Stats.archStats[i].nonIdleTransitions);

	fprintf(file, "%% \trequested %u %u\n",
		global_Stats.archStats[i].counters.requested,
		global_Stats.archStats[i].squared.requested);
	fprintf(file, "%% \tobtained %u %u\n",
		global_Stats.archStats[i].counters.obtained,
		global_Stats.archStats[i].squared.obtained);
	fprintf(file, "%% \tevicted %u %u\n",
		global_Stats.archStats[i].counters.evicted,
		global_Stats.archStats[i].squared.evicted);
	fprintf(file, "%% \treclaimed %u %u\n",
		global_Stats.archStats[i].counters.reclaimed,
		global_Stats.archStats[i].squared.reclaimed);
	fprintf(file, "%% \ttimeUsed %u %u\n",
		global_Stats.archStats[i].counters.timeUsed,
		global_Stats.archStats[i].squared.timeUsed);
	fprintf(file, "%% \ttimeToEviction %u %u\n",
		global_Stats.archStats[i].counters.timeToEviction,
		global_Stats.archStats[i].squared.timeToEviction);
	fprintf(file, "%% \thostIdleObtained %u %u %u %u\n",
		global_Stats.archStats[i].counters.hostIdleObtained[MIG_COUNTER_HIGH],
		global_Stats.archStats[i].counters.hostIdleObtained[MIG_COUNTER_LOW],
		global_Stats.archStats[i].squared.hostIdleObtained[MIG_COUNTER_HIGH],
		global_Stats.archStats[i].squared.hostIdleObtained[MIG_COUNTER_LOW]);
	fprintf(file, "%% \thostIdleEvicted %u %u %u %u\n",
		global_Stats.archStats[i].counters.hostIdleEvicted[MIG_COUNTER_HIGH],
		global_Stats.archStats[i].counters.hostIdleEvicted[MIG_COUNTER_LOW],
		global_Stats.archStats[i].squared.hostIdleEvicted[MIG_COUNTER_HIGH],
		global_Stats.archStats[i].squared.hostIdleEvicted[MIG_COUNTER_LOW]);
	fprintf(file, "%% \tidleTimeWhenActive %u %u %u %u\n",
		global_Stats.archStats[i].counters.idleTimeWhenActive[MIG_COUNTER_HIGH],
		global_Stats.archStats[i].counters.idleTimeWhenActive[MIG_COUNTER_LOW],
		global_Stats.archStats[i].squared.idleTimeWhenActive[MIG_COUNTER_HIGH],
		global_Stats.archStats[i].squared.idleTimeWhenActive[MIG_COUNTER_LOW]);

	fprintf(file, "%% \trequestDist ");
	for (j = 0; j <= MIG_MAX_HOSTS_DIST; j++) {
	    fprintf(file, "%u ",
		    global_Stats.archStats[i].requestDist[j]);
	}
	fprintf(file, "\n");

	fprintf(file, "%% \tobtainedDist ");
	for (j = 0; j <= MIG_MAX_HOSTS_DIST; j++) {
	    fprintf(file, "%u ",
		    global_Stats.archStats[i].obtainedDist[j]);
	}
	fprintf(file, "\n");

	fprintf(file, "%% \thostCounts ");
	for (j = 0; j < MIG_NUM_STATES; j++) {
	    global_Stats.archStats[i].counters.hostCounts[j] +=
		hostCounts[i][j];
	    global_Stats.archStats[i].squared.hostCounts[j] +=
		hostCounts[i][j] * hostCounts[i][j];
	    fprintf(file, "%u %u ",
		    global_Stats.archStats[i].counters.hostCounts[j],
		    global_Stats.archStats[i].squared.hostCounts[j]);
	}
	fprintf(file, "\n");
    }
    fprintf(file, "%% End of Statistics\n");
} 

/*
 *----------------------------------------------------------------------
 *
 * ReadStats --
 *
 *	Read statistics in ASCII format from the checkpoint file.  
 *
 * Results:
 *	If we are the wrong version, or we hit some error, we return -1.
 *	That indicates that the caller should just ignore any subsequent
 *	lines with "%" in them.  0 indicates success.
 *
 * Side effects:
 *	Statistics are reset from last checkpoint.
 *
 *----------------------------------------------------------------------
 */

static int
ReadStats(file)
    FILE *file;
{
    int i, j;
    int numScanned;
    char buffer[BUFSIZ];
    int lineNum = 0;
    int lastVersion;		/* Version of migd that wrote checkpoint. */
    int lastInterval;		/* Checkpoint interval  of migd that wrote
				   checkpoint. */
    int didStats;		/* Whether that migd wrote the statistics. */
    int maxArchs;		/* Value of global_Stats.maxArchs for that
				   migd. */
    int archNum;		/* Index into archTypes array. */
    char archName[BUFSIZ];	/* Temporary buffer for name, to compare
				   against our index. */
    
/*
 * Common action to take on bad input.
 */
#define BAD_INPUT \
    if (global_Debug > 1) { \
	PRINT_PID; \
	fprintf(stderr, \
		"ReadStats: bad input on line %d: %s", \
		lineNum, buffer); \
    } \
    return(-1); 



/*
 * For each line, read the whole thing into a buffer (1) to make sure
 * it starts with "%" and (2) to be able to print it out entirely
 * if there's a problem.
 */
#define READLINE \
    if (fgets(buffer, sizeof(buffer), file) == (char *) NULL) { \
	if (global_Debug > 1) { \
	    PRINT_PID; \
	    fprintf(stderr, \
		    "ReadStats: end of file at line %d.\n", lineNum); \
	} \
	return(-1); \
    } \
    lineNum++; \
    if (buffer[0] != '%') { \
        BAD_INPUT; \
    }

    READLINE;
    
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "ReadStats: %s", buffer);
    }
    
    numScanned = sscanf(buffer, "%% Version %u HasStats %u Interval %u",
	    &lastVersion, &didStats, &lastInterval);
    if (numScanned != 3) {
	BAD_INPUT;
    }
    if (lastVersion != migd_Version ||
	lastInterval != global_CheckpointInterval) {
	if (global_Debug > 1) { 
	    PRINT_PID; 
	    fprintf(stderr, 
		    "ReadStats: mismatch in migd versions or intervals: ours is %u, %u but last writer\n\tof checkpoint was %u, %u.\n",
		    migd_Version, global_CheckpointInterval,
		    lastVersion, lastInterval); 
	} 
	return(-1); 
    }
    if (didStats != 1) {
	if (global_Debug > 1) { 
	    PRINT_PID; 
	    fprintf(stderr, 
		    "ReadStats: last writer of checkpoint didn't do stats.\n"); 
	} 
	return(0);
    }


    READLINE;
    numScanned = sscanf(buffer, "%% firstRun %u", &global_Stats.firstRun);
    if (numScanned != 1) {
	BAD_INPUT;
    }
    
    READLINE;
    numScanned = sscanf(buffer, "%% restarts %u", &global_Stats.restarts);
    if (numScanned != 1) {
	BAD_INPUT;
    }
    global_Stats.restarts++;

    READLINE;
    numScanned = sscanf(buffer, "%% intervals %u", &global_Stats.intervals);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    READLINE;
    numScanned = sscanf(buffer, "%% maxArchs %u", &maxArchs);
    if (numScanned != 1) {
	BAD_INPUT;
    }
    if (maxArchs != global_Stats.maxArchs) {
	if (global_Debug > 1) { 
	    PRINT_PID; 
	    fprintf(stderr, 
		    "ReadStats: discrepancy in number of architectures managed by\n\tmigd; resetting statistics.\n"); 
	} 
	return(-1);
    }
    
    READLINE;
    numScanned = sscanf(buffer, "%% getLoadRequests %u",
			&global_Stats.getLoadRequests);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    READLINE;
    numScanned = sscanf(buffer, "%% totalRequests %u",
			&global_Stats.totalRequests);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    READLINE;
    numScanned = sscanf(buffer, "%% totalObtained %u",
			&global_Stats.totalObtained);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    READLINE;
    numScanned = sscanf(buffer, "%% numRepeatRequests %u",
			&global_Stats.numRepeatRequests);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    READLINE;
    numScanned = sscanf(buffer, "%% numRepeatAssignments %u",
			&global_Stats.numRepeatAssignments);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    READLINE;
    numScanned = sscanf(buffer, "%% numFirstAssignments %u",
			&global_Stats.numFirstAssignments);
    if (numScanned != 1) {
	BAD_INPUT;
    }

    for (i = 0; i < maxArchs; i++) {
	READLINE;
	numScanned = sscanf(buffer, "%% arch %u %s", &archNum, archName);
	if (numScanned != 2) {
	    BAD_INPUT;
	}
	if (archNum != i || strcmp(archName, global_Stats.archStats[i].arch)) {
	    BAD_INPUT;
	}

	READLINE;
	numScanned = sscanf(buffer, "%% \tnumClients %u",
		&global_Stats.archStats[i].numClients);
	if (numScanned != 1) {
	    BAD_INPUT;
	}
	
	READLINE;
	numScanned = sscanf(buffer, "%% \tgotAll %u",
		&global_Stats.archStats[i].gotAll);
	if (numScanned != 1) {
	    BAD_INPUT;
	}
	
	READLINE;
	numScanned = sscanf(buffer, "%% \tnonIdleTransitions %u",
		&global_Stats.archStats[i].nonIdleTransitions);
	if (numScanned != 1) {
	    BAD_INPUT;
	}

	READLINE;
	numScanned = sscanf(buffer, "%% \trequested %u %u",
		&global_Stats.archStats[i].counters.requested,
		&global_Stats.archStats[i].squared.requested);
	if (numScanned != 2) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \tobtained %u %u",
		&global_Stats.archStats[i].counters.obtained,
		&global_Stats.archStats[i].squared.obtained);
	if (numScanned != 2) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \tevicted %u %u",
		&global_Stats.archStats[i].counters.evicted,
		&global_Stats.archStats[i].squared.evicted);
	if (numScanned != 2) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \treclaimed %u %u",
		&global_Stats.archStats[i].counters.reclaimed,
		&global_Stats.archStats[i].squared.reclaimed);

	if (numScanned != 2) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \ttimeUsed %u %u",
		&global_Stats.archStats[i].counters.timeUsed,
		&global_Stats.archStats[i].squared.timeUsed);

	if (numScanned != 2) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \ttimeToEviction %u %u",
		&global_Stats.archStats[i].counters.timeToEviction,
		&global_Stats.archStats[i].squared.timeToEviction);

	if (numScanned != 2) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \thostIdleObtained %u %u %u %u",
		&global_Stats.archStats[i].counters.hostIdleObtained[MIG_COUNTER_HIGH],
		&global_Stats.archStats[i].counters.hostIdleObtained[MIG_COUNTER_LOW],
		&global_Stats.archStats[i].squared.hostIdleObtained[MIG_COUNTER_HIGH],
		&global_Stats.archStats[i].squared.hostIdleObtained[MIG_COUNTER_LOW]);

	if (numScanned != 4) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \thostIdleEvicted %u %u %u %u",
		&global_Stats.archStats[i].counters.hostIdleEvicted[MIG_COUNTER_HIGH],
		&global_Stats.archStats[i].counters.hostIdleEvicted[MIG_COUNTER_LOW],
		&global_Stats.archStats[i].squared.hostIdleEvicted[MIG_COUNTER_HIGH],
		&global_Stats.archStats[i].squared.hostIdleEvicted[MIG_COUNTER_LOW]);

	if (numScanned != 4) {
	    BAD_INPUT;
	}
	READLINE;
	numScanned = sscanf(buffer, "%% \tidleTimeWhenActive %u %u %u %u",
		&global_Stats.archStats[i].counters.idleTimeWhenActive[MIG_COUNTER_HIGH],
		&global_Stats.archStats[i].counters.idleTimeWhenActive[MIG_COUNTER_LOW],
		&global_Stats.archStats[i].squared.idleTimeWhenActive[MIG_COUNTER_HIGH],
		&global_Stats.archStats[i].squared.idleTimeWhenActive[MIG_COUNTER_LOW]);
	if (numScanned != 4) {
	    BAD_INPUT;
	}
	/*
	 * No easy way to use sscanf to go through a string piece
	 * by piece, so use fscanf.  First for distribution of requests,
	 * then count of hosts.
	 */
	numScanned = fscanf(file, "%% \trequestDist");
	if (numScanned < 0) {
	    if (global_Debug > 1) { 
		PRINT_PID; 
		fprintf(stderr, 
			"ReadStats: error scanning requestDist line (%d).\n",
			lineNum); 
	    } 
	    return(-1);
	}	    
	for (j = 0; j <= MIG_MAX_HOSTS_DIST; j++) {
	    numScanned = fscanf(file, "%u ",
				&global_Stats.archStats[i].requestDist[j]);
	    if (numScanned != 1) {
		if (global_Debug > 1) { 
		    PRINT_PID; 
		    fprintf(stderr, 
			    "ReadStats: error scanning requestDist line (%d).\n",
			    lineNum); 
		} 
		return(-1);
	    }	    
	}
	numScanned = fscanf(file, "\n");
	if (numScanned < 0) {
	    if (global_Debug > 1) { 
		PRINT_PID; 
		fprintf(stderr, 
			"ReadStats: error scanning requestDist line (%d).\n",
			lineNum); 
	    } 
	    return(-1);
	}	    
	lineNum++;

	numScanned = fscanf(file, "%% \tobtainedDist");
	if (numScanned < 0) {
	    if (global_Debug > 1) { 
		PRINT_PID; 
		fprintf(stderr, 
			"ReadStats: error scanning obtainedDist line (%d).\n",
			lineNum); 
	    } 
	    return(-1);
	}	    
	for (j = 0; j <= MIG_MAX_HOSTS_DIST; j++) {
	    numScanned = fscanf(file, "%u ",
				&global_Stats.archStats[i].obtainedDist[j]);
	    if (numScanned != 1) {
		if (global_Debug > 1) { 
		    PRINT_PID; 
		    fprintf(stderr, 
			    "ReadStats: error scanning obtainedDist line (%d).\n",
			    lineNum); 
		} 
		return(-1);
	    }	    
	}
	numScanned = fscanf(file, "\n");
	if (numScanned < 0) {
	    if (global_Debug > 1) { 
		PRINT_PID; 
		fprintf(stderr, 
			"ReadStats: error scanning obtainedDist line (%d).\n",
			lineNum); 
	    } 
	    return(-1);
	}	    
	lineNum++;

	numScanned = fscanf(file, "%% \thostCounts");
	if (numScanned < 0) {
	    if (global_Debug > 1) { 
		PRINT_PID; 
		fprintf(stderr, 
			"ReadStats: error scanning hostCounts line (%d).\n",
			lineNum); 
	    } 
	    return(-1);
	}	    
	for (j = 0; j < MIG_NUM_STATES; j++) {
	    numScanned = fscanf(file, "%u %u ",
				&global_Stats.archStats[i].counters.hostCounts[j],
				&global_Stats.archStats[i].squared.hostCounts[j]);
	    if (numScanned != 2) {
		if (global_Debug > 1) { 
		    PRINT_PID; 
		    fprintf(stderr, 
			    "ReadStats: error scanning hostCounts line (%d).\n",
			    lineNum); 
		} 
		return(-1);
	    }	    
	}
	numScanned = fscanf(file, "\n");
	if (numScanned < 0) {
	    if (global_Debug > 1) { 
		PRINT_PID; 
		fprintf(stderr, 
			"ReadStats: error scanning hostCounts line (%d).\n",
			lineNum); 
	    } 
	    return(-1);
	}	    
	lineNum++;
    }
    return(0);
} 



/*
 *----------------------------------------------------------------------
 *
 * CltToMigd --
 *
 *	Return the migd pointer corresponding to a host, given its
 *	cltPtr.  This combines all the sanity checks in a single
 *	location.
 *
 * Results:
 *	A pointer to the internal Migd_Info record for the host is returned,
 *	or NULL if the entry doesn't exist.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static Migd_Info *
CltToMigd(cltPtr)
    Migd_OpenStreamInfo *cltPtr;
{
    Migd_Info *migdPtr;

    migdPtr = migInfoArray[cltPtr->host];
    if (migdPtr == (Migd_Info *) NULL) {
	return((Migd_Info *) NULL);
    }
    if (migdPtr->cltPtr != cltPtr) {
	SYSLOG1(LOG_WARNING,
	       "Mismatch in client information for host %d.\n", cltPtr->host);
	return((Migd_Info *) NULL);
    }
    return(migdPtr);
}



/*
 *----------------------------------------------------------------------
 *
 * HostIsIdle --
 *
 *	Return whether a host is considered to be at least partly
 *	idle.  This should be changed to a macro once debugging is over.
 *
 * Results:
 *	A non-zero value is returned if the host referenced by migdPtr
 *	is considered to be idle, else 0.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

static int
HostIsIdle(migdPtr)
    Migd_Info *migdPtr;
{
    if (global_Debug > 3) {
	PRINT_PID;
	fprintf(stderr, "HostIsIdle: %s was ", migdPtr->name);
    }
    if (migdPtr->info.state == MIG_HOST_IDLE ||
	migdPtr->info.state == MIG_HOST_PART_USED) {
	if (global_Debug > 3) {
	    PRINT_PID;
	    fprintf(stderr, "idle\n");
	}
	return(1);
    }
    if (global_Debug > 3) {
	PRINT_PID;
	fprintf(stderr, "not idle\n");
    }
    return(0);
}


/*
 *----------------------------------------------------------------------
 *
 * ForceHostIdle --
 *
 *	Restore sanity to a host by removing all indications of clients
 *	associated with the host and then putting it back on the idle list.
 *	This is necessary if, for example, someone requests a host
 *	and then doesn't release it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Host is put on idle list.
 *
 *----------------------------------------------------------------------
 */

static void
ForceHostIdle(migdPtr)
    Migd_Info *migdPtr;
{
    List_Links *listPtr;
    RequestInfo *reqPtr;

    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "ForceHostIdle: %s now believed to be idle.\n",
	       migdPtr->name);
    }
    while (!List_IsEmpty(&migdPtr->clientList)) {
	listPtr = List_First(&migdPtr->clientList);
	List_Remove(listPtr);
	reqPtr = NEXT_HOST_REQUEST_TO_INFO(listPtr);
	if (global_Debug > 1) {
	    PRINT_PID;
	    fprintf(stderr, "Removing request from process %x.\n",
		   (reqPtr->flags & MIG_PROC_AGENT) ? 
		   reqPtr->client.processID : 
		   reqPtr->client.cltPtr->processID);
	}
	/*
	 * This is like an eviction, except no processes are getting evicted
	 * -- we don't even think there are any there.  Instead, we notify
	 * the client that we've revoked its permission to use this host
	 * later on.
	 */
	RevokePermission(reqPtr, REVOKE_IDLE);
	free((char *) reqPtr);
    }
    migdPtr->flags &= ~(MIGD_CHECK_COUNT | MIGD_WAS_EMPTY);
    if (HostIsIdle(migdPtr)) {
	List_Remove((List_Links *) migdPtr);
    } 
    InsertIdle(migdPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Global_GetStats --
 *
 *	Return the statistics structure to a user process.
 *
 * Results:
 *	0 for success, or an errno indicating the error.
 *	The statistics, and the size of the buffer, are returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_GetStats(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Not used. */
    int inBufSize;		/* Not used. */
    char *outBuffer;		/* Buffer to place results. */
    int *outBufSizePtr;		/* Size of the output buffer. */
{
    if (global_Debug > 3) {
	PRINT_PID;
	fprintf(stderr, "Global_GetStats called.\n");
    }

    if (*outBufSizePtr != sizeof(Mig_Stats)) {
	if (global_Debug > 0) {
	    SYSLOG3(LOG_WARNING,
		   "Global_GetStats: bad output buffer size (%d, not %d) from process %x\n",
		   *outBufSizePtr, sizeof(Mig_Stats), cltPtr->processID);
	}
	return(EINVAL);
    }
    bcopy((char *) &global_Stats, outBuffer, sizeof(Mig_Stats));
    return(0);
}

/*
 *----------------------------------------------------------------------
 *
 * Global_ResetStats --
 *
 *	Reinitialize the statistics structure to a user process.  This
 *	is just a stub that takes standard ioctl arguments and calls the
 *	internal routine.
 *
 * Results:
 *	0 indicates success.
 *
 * Side effects:
 *	Statistics are zeroed.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_ResetStats(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Not used. */
    int inBufSize;		/* Not used. */
    char *outBuffer;		/* Buffer to place results. */
    int *outBufSizePtr;		/* Size of the output buffer. */
{
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "Global_ResetStats called.\n");
    }

    InitStats();

    return(0);
}



/*
 *----------------------------------------------------------------------
 *
 * Global_SetParms --
 *
 *	Set the parameters used by all migration daemons.
 *
 * Results:
 *	0 for success, or an errno indicating the error.
 *
 * Side effects:
 *	Host-specific daemon connections are made readable so
 *	they know to perform an ioctl to get the info.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
int
Global_SetParms(cltPtr, command, inBuffer, inBufSize, outBuffer,
	       outBufSizePtr)
    Migd_OpenStreamInfo *cltPtr;/* Information about the client making
				   the request. */
    int command;		/* Ignored. */
    char *inBuffer;		/* Buffer to get arguments from. */
    int inBufSize;		/* Size of the input buffer. */
    char *outBuffer;		/* Buffer to place results, not used. */
    int *outBufSizePtr;		/* Size of the output buffer, set to 0. */
{
    if (global_Debug > 1) {
	PRINT_PID;
	fprintf(stderr, "Global_SetParms called.\n");
    }

    /*
     * XXX need to add this here.
     */
    return(EINVAL);
}
    

/*
 *----------------------------------------------------------------------
 *
 * Global_DumpIdleList --
 *
 *	For debugging purposes: print an idle list, given the header.
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
Global_DumpIdleList(hdrPtr)
    List_Links *hdrPtr;
{
    List_Links *itemPtr;
    Migd_Info  *migdPtr;

    LIST_FORALL(hdrPtr, itemPtr) {
	migdPtr = (Migd_Info *) itemPtr;
	fprintf(stderr, "\t%s idle %d seconds\n", migdPtr->name,
		migdPtr->info.loadVec.noInput);
    }
    fflush(stderr);
}


