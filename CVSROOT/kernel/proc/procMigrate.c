/* 
 * procMigrate.c --
 *
 *	Routines for process migration.  These provide the system
 *	call interface to initiate migration and routines to transfer
 *	data from the host on which the process is currently executing
 *	to the host to which it is migrating.  The routines that accept
 *	this data are in procRemote.c.  
 *
 * Copyright 1986, 1988, 1989 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header$ SPRITE (Berkeley)";
#endif /* not lint */


#include "sprite.h"
#include "mach.h"
#include "proc.h"
#include "procInt.h"
#include "procMigrate.h"
#include "migrate.h"
#include "fs.h"
#include "stdlib.h"
#include "sig.h"
#include "spriteTime.h"
#include "trace.h"
#include "list.h"
#include "byte.h"
#include "vm.h"
#include "sys.h"
#include "dbg.h"
#include "rpc.h"
#include "prof.h"
#include "sched.h"
#include "sync.h"
#include "sysSysCall.h"

static	Sync_Condition	migrateCondition;
static	Sync_Lock	migrateLock = Sync_LockInitStatic("Proc:migrateLock");
#define	LOCKPTR &migrateLock

int proc_MigDebugLevel = 0;

Trace_Header proc_TraceHeader;
Trace_Header *proc_TraceHdrPtr = (Trace_Header *)NIL;
Boolean proc_DoTrace = FALSE;
Boolean proc_DoCallTrace = FALSE;

/*
 * True if we should convert a SIG_DEBUG into a SIG_KILL for migrated
 * processes.
 */
Boolean proc_KillMigratedDebugs = TRUE;

int proc_AllowMigrationState = PROC_MIG_ALLOW_DEFAULT;

/*
 * Set the migration version number.  Machines can only migrate to other
 * machines of the same architecture and version number.
 */
#ifndef PROC_MIGRATE_VERSION
#define PROC_MIGRATE_VERSION 4
#endif /* PROC_MIGRATE_VERSION */

int proc_MigrationVersion = PROC_MIGRATE_VERSION;

/*
 * Procedures internal to this file
 */

static ReturnStatus GetEncapSize();
static ReturnStatus EncapProcState();
static ReturnStatus DeencapProcState();
static ReturnStatus UpdateState();

static ReturnStatus ResumeExecution();
static ReturnStatus KillRemoteCopy();
static ReturnStatus InitiateMigration();
static void	    LockAndSwitch();
static ENTRY void   WakeupCallers();

#ifdef DEBUG
int proc_MemDebug = 0;
#endif /* DEBUG */
/*
 * Define the structure for keeping track of callbacks for migrating
 * a process.  (This is done after procedure declarations since
 * some things are static and we need the forward reference.)
 *
 * See the comments in Proc_MigrateTrap for further explanation.
 */
typedef struct {
    ReturnStatus (*preFunc)();	   /* function to call when initiating
				      migration (returning size); should not
				      have side-effects requiring further
				      callback */
    ReturnStatus (*encapFunc)();   /* function to call to encapsulate data */
    ReturnStatus (*deencapFunc)(); /* function to call to deencapsulate
					data on other end */
    ReturnStatus (*postFunc)();	   /* function to call after migration
				      completes or fails */
    Proc_EncapToken token;	   /* identifier to match encap and deencap
				      functions between two hosts */
} EncapCallback;

/*
 * Set up the functions to be called.
 */
static EncapCallback encapCallbacks[] = {
    { GetEncapSize, EncapProcState, DeencapProcState, NULL,
	  PROC_MIG_ENCAP_PROC},
#ifdef notdef
    { Vm_InitiateMigration, Vm_EncapState, Vm_DeencapState, Vm_FinishMigration,
	  PROC_MIG_ENCAP_VM},
#endif
    { Vm_InitiateMigration, Vm_EncapState, Vm_DeencapState, NULL,
	  PROC_MIG_ENCAP_VM},
    { Fs_InitiateMigration, Fs_EncapFileState, Fs_DeencapFileState, NULL,
	  PROC_MIG_ENCAP_FS},
    { Mach_GetEncapSize, Mach_EncapState, Mach_DeencapState, NULL,
	  PROC_MIG_ENCAP_MACH},
    { Prof_GetEncapSize, Prof_EncapState, Prof_DeencapState, NULL,
	  PROC_MIG_ENCAP_PROF},
    { Sig_GetEncapSize, Sig_EncapState, Sig_DeencapState, NULL,
	  PROC_MIG_ENCAP_SIG}
};

#ifdef BREAKS_KDBX
static struct {
    char *preFunc;	   /* name of function to call when initiating
				      migration */
    char *encapFunc;	/* name of function to call to encapsulate */
    char *deencapFunc;	/* name of function to call to deencapsulate */
    char *postFunc;	/* name of function to call when done */
} callbackNames[] = {
    { "GetEncapSize", "EncapProcState", "DeencapProcState", NULL},
    { "Vm_InitiateMigration", "Vm_EncapState", "Vm_DeencapState",
	  "Vm_FinishMigration"},
    { "Fs_InitiateMigration", "Fs_EncapFileState", "Fs_DeencapFileState",
	  "Fs_MigDone"},
    { "Mach_InitiateMigration", "Mach_EncapState", "Mach_DeencapState", NULL},
    { "Prof_InitiateMigration", "Prof_EncapState", "Prof_DeencapState", NULL},
    { "Sig_InitiateMigration", "Sig_EncapState", "Sig_DeencapState", NULL}
};
#endif


/*
 *----------------------------------------------------------------------
 *
 * Proc_Migrate --
 *
 *	Migrates a process to another workstation.  The process may be
 *    	PROC_MY_PID, PROC_ALL_PROCESSES, or a process ID.
 *    	PROC_ALL_PROCESSES implies evicting all foreign processes, in
 *    	which case the hostID is ignored.  The workstation may be
 *    	PROC_MIG_ANY or a particular workstation.  (For now, the
 *    	workstation argument must be a specific workstation.)
 *
 * 	This procedure implements the system call by the same name.
 *
 * Results:
 *	PROC_INVALID_PID -	the pid argument was illegal.
 *	PROC_INVALID_NODE_ID -	the host argument was illegal.
 *	GEN_NO_PERMISSION -	the user or process is not permitted to
 *				migrate.
 *	Other results may be returned from the VM and RPC modules.
 *
 * Side effects:
 *	The specified process is migrated to another workstation.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Proc_Migrate(pid, hostID)
    Proc_PID pid;
    int	     hostID;
{
    register Proc_ControlBlock *procPtr;
    ReturnStatus status;
    Proc_TraceRecord record;
    Boolean migrateSelf = FALSE;
    int permMask;

    /*
     * It is possible for a process to try to migrate onto the machine
     * on which it is currently executing.
     */

    if (hostID == rpc_SpriteID) {
	return(SUCCESS);
    }
    if (hostID <= 0 || hostID > NET_NUM_SPRITE_HOSTS) {
	return(GEN_INVALID_ARG);
    }
    
    if (Proc_ComparePIDs(pid, PROC_ALL_PROCESSES)) {
	status = Proc_EvictForeignProcs();
	return(status);
    }
    
    if (Proc_ComparePIDs(pid, PROC_MY_PID)) {
	migrateSelf = TRUE;
	procPtr = Proc_GetActualProc();
	if (procPtr == (Proc_ControlBlock *) NIL) {
	    panic("Proc_Migrate: procPtr == NIL\n");
	}
	Proc_Lock(procPtr);
	pid = procPtr->processID;
    } else {
	procPtr = Proc_LockPID(pid);
	if (procPtr == (Proc_ControlBlock *) NIL) {
	    return (PROC_INVALID_PID);
	}
    }


    if (proc_MigDebugLevel > 3) {
	printf("Proc_Migrate: migrate process %x to host %d.\n",
		   procPtr->processID, hostID);
    }

    /*
     * Do some sanity checking.  
     */
    if (procPtr->state == PROC_DEAD || procPtr->state == PROC_EXITING) {
	if (proc_MigDebugLevel > 3) {
	    printf("Proc_Migrate: process %x has exited.\n",
		       pid);
	}
	Proc_Unlock(procPtr);
	return(PROC_INVALID_PID);
    }
    if (procPtr->state == PROC_MIGRATED) {
	if (proc_MigDebugLevel > 1) {
	    printf("Proc_Migrate: process %x has already migrated.\n",
		       pid);
	}
	Proc_Unlock(procPtr);
	return(PROC_INVALID_PID);
    }
	
    if (procPtr->genFlags & PROC_FOREIGN) {
	if (proc_MigDebugLevel > 0) {
	    printf("Proc_Migrate: process %x is foreign... can't migrate yet.\n",
		       procPtr->processID);
	}
	Proc_Unlock(procPtr);
	return(PROC_INVALID_PID);
    }
    if (procPtr->genFlags & PROC_DONT_MIGRATE) {
	if (proc_MigDebugLevel > 0) {
	    printf("Proc_Migrate: process %x is not allowed to migrate.\n",
		       pid);
	}
	Proc_Unlock(procPtr);
	return(GEN_NO_PERMISSION);
    }
    
    if (procPtr->userID == PROC_SUPER_USER_ID) {
	permMask = PROC_MIG_EXPORT_ROOT;
    } else {
	permMask = PROC_MIG_EXPORT_ALL;
    }
 
    if ((proc_AllowMigrationState & permMask) != permMask) {
	if (proc_MigDebugLevel > 0) {
	    printf("Proc_Migrate: user does not have permission to migrate.\n");
	}
	Proc_Unlock(procPtr);
	return(GEN_NO_PERMISSION);
    }
	
#ifndef CLEAN
    if (proc_DoTrace && proc_MigDebugLevel > 0) {
	record.processID = procPtr->processID;
	record.flags = PROC_MIGTRACE_START | PROC_MIGTRACE_HOME;
	record.info.filler = NIL;
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_BEGIN_MIG,
		     (ClientData) &record);
    }
#endif /* CLEAN */   
   
    /*
     * Contact the remote workstation to establish communication and
     * verify that migration is permissible.
     */
    
    status = InitiateMigration(procPtr, hostID);


    if (status != SUCCESS) {
	Proc_Unlock(procPtr);
	return(status);
    }

    Proc_FlagMigration(procPtr, hostID);

    Proc_Unlock(procPtr);

    /*
     * If migrating another process, wait for it to migrate.
     */
    if (!migrateSelf) {
	status = Proc_WaitForMigration(pid);
    } else {
	status = SUCCESS;
    }

    /*
     * Note: the "end migration" trace record is inserted by the MigrateTrap
     * routine to get around the issue of waiting for oneself to migrate.
     * (Otherwise, since we can't wait here for the current process to
     * migrate -- only a different process -- we wouldn't be able to insert
     * the trace record at the right time.)
     */
     
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_MigrateTrap --
 *
 *	Transfer the state of a process once it has reached a state with no
 *	relevant information on the kernel stack.  This is done following a
 *	kernel call, when the process can migrate and immediately return
 *	to user mode and there is no relevant information on the kernel
 *	stack.  
 *
 * Results:
 *	No value is returned.
 *
 * Side effects:
 *	The process state is transferred by performing callbacks
 *	to each module with state to transfer.
 *
 *----------------------------------------------------------------------
 */

void
Proc_MigrateTrap(procPtr)
    register Proc_ControlBlock 	*procPtr; /* The process being migrated */
{
    int hostID;			/* host to which it migrates */
    ReturnStatus status;
    Proc_TraceRecord record;
    Boolean foreign = FALSE;
    Address buffer;
    Address bufPtr;
    int bufSize;
    register int i;
    Proc_EncapInfo info[PROC_MIG_NUM_CALLBACKS];
    Proc_EncapInfo *infoPtr;
    ProcMigCmd cmd;
    Proc_MigBuffer inBuf;
    int failure;

#ifdef MEM_TRASH
    int lastMemDebug;
    lastMemDebug = proc_MemDebug;
    proc_MemDebug = 1;
#endif /* MEM_TRASH */

    Proc_Lock(procPtr);

    if (procPtr->genFlags & PROC_FOREIGN) {
	foreign = TRUE;
    }

#ifndef CLEAN
    if (proc_DoTrace && proc_MigDebugLevel > 1) {
	record.processID = procPtr->processID;
	record.flags = PROC_MIGTRACE_START;
	if (!foreign) {
	    record.flags |= PROC_MIGTRACE_HOME;
	}
	record.info.filler = NIL;
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_MIGTRAP,
		     (ClientData) &record);
    }
#endif /* CLEAN */   
   
    procPtr->genFlags = (procPtr->genFlags & ~PROC_MIG_PENDING) |
	PROC_MIGRATING;
    
    hostID = procPtr->peerHostID;
    bufSize = 0;

    /*
     * Go through the list of callbacks to generate the size of the buffer
     * we'll need.  In unusual circumstances, a caller may return a status
     * other than SUCCESS. In this case, the process should be continuable!
     */
    for (i = 0; i < PROC_MIG_NUM_CALLBACKS; i++) {
	infoPtr = &info[i];
	infoPtr->token = encapCallbacks[i].token;
	infoPtr->processed = 0;
	infoPtr->special = 0;
	if (proc_MigDebugLevel > 5) {
	    printf("Calling preFunc %d\n", i);
	}
	status = (*encapCallbacks[i].preFunc)(procPtr, hostID, infoPtr);
	if (status == SUCCESS && infoPtr->special) {
	    /*
	     * This is where we'd like to handle special cases like shared
	     * memory processes.  For now, bail out.
	     */
	    status = FAILURE;
	}
	if (status != SUCCESS) {
	    printf("Warning: Proc_MigrateTrap: error returned by encapsulation procedure %s:\n\t%s.\n",
#ifdef BREAKS_KDBX
		   callbackNames[i].preFunc,
#else /* BREAKS_KDBX */
		   "(can't get name)", 
#endif /* BREAKS_KDBX */
		   Stat_GetMsg(status));
	    procPtr->genFlags &= ~PROC_MIGRATING;
	    Proc_Unlock(procPtr);
	    WakeupCallers();
#ifdef MEM_TRASH
    proc_MemDebug = lastMemDebug;
#endif /* MEM_TRASH */
	    return;
	}
	bufSize += infoPtr->size + sizeof(Proc_EncapInfo);
    }
    if (proc_MigDebugLevel > 5) {
	printf("Buffer size is %d\n", bufSize);
    }
    buffer = malloc(bufSize);
    if (proc_MigDebugLevel > 5) {
	printf("past malloc\n", bufSize);
    }
    bufPtr = buffer;
    failure = 0;

    /*
     * This time, go through the list of callbacks to fill in the
     * encapsulated data.  From this point on, failure indicates
     * that the process will be killed.
     */
    for (i = 0; i < PROC_MIG_NUM_CALLBACKS; i++) {
	infoPtr = &info[i];
	bcopy((Address) infoPtr, bufPtr, sizeof(Proc_EncapInfo));
	bufPtr += sizeof(Proc_EncapInfo);
	if (proc_MigDebugLevel > 5) {
	    printf("Calling encapFunc %d\n", i);
	}
	status = (*encapCallbacks[i].encapFunc)(procPtr, hostID, infoPtr,
						bufPtr);
#ifdef lint
	status = EncapProcState(procPtr, hostID, infoPtr, bufPtr);
	status = Vm_EncapState(procPtr, hostID, infoPtr, bufPtr);
	status = Fs_EncapFileState(procPtr, hostID, infoPtr, bufPtr);
	status = Mach_EncapState(procPtr, hostID, infoPtr, bufPtr);
	status = Prof_EncapState(procPtr, hostID, infoPtr, bufPtr);
	status = Sig_EncapState(procPtr, hostID, infoPtr, bufPtr);
#endif /* lint */
	if (status != SUCCESS) {
	    printf("Warning: Proc_MigrateTrap: error returned by encapsulation procedure %s:\n\t%s.\n",
#ifdef BREAKS_KDBX
		   callbackNames[i].encapFunc,
#else /* BREAKS_KDBX */
		   "(can't get name)", 
#endif /* BREAKS_KDBX */
		   Stat_GetMsg(status));
	    failure = 1;
	    break;
	}
	bufPtr += infoPtr->size;
	infoPtr->processed = 1;
    }

    Proc_Unlock(procPtr);
    /*
     * Send the encapsulated data in the buffer to the other host.  
     */
    if (!failure) {
	/*
	 * Set up for the RPC.
	 */
	cmd.command = PROC_MIGRATE_CMD_ENTIRE;
	cmd.remotePid = procPtr->peerProcessID;
	inBuf.size = bufSize;
	inBuf.ptr = buffer;

	if (proc_MigDebugLevel > 5) {
	    printf("Sending encapsulated state.\n");
	}
	status = ProcMigCommand(procPtr->peerHostID, &cmd, &inBuf,
				(Proc_MigBuffer *) NIL);

	if (status != SUCCESS) {
	    printf("Warning: Proc_MigrateTrap: error encountered sending encapsulated state:\n\t%s.\n",
		   Stat_GetMsg(status));
	    failure = 1;
	}
    }
    /*
     * Finally, go through the list of callbacks to clean up.  Only call
     * routines that were processed last time (in the case of failure
     * partway through).  Note that the process pointer is UNLOCKED
     * during these callbacks (as well as the RPC to transfer state).
     * This is primarily because Vm_FinishMigration is the only callback
     * so far and it needs it unlocked, and we have to unlock for the rpc
     * anyway so that we don't deadlock on the process once the migrated
     * version resumes (a side effect of the RPC).
     */
    bufPtr = buffer;
    for (i = 0; i < PROC_MIG_NUM_CALLBACKS; i++) {
	infoPtr = &info[i];
	if (infoPtr->processed != 1) {
	    break;
	}
	bufPtr += sizeof(Proc_EncapInfo);
	if (encapCallbacks[i].postFunc != NULL) {
	    if (proc_MigDebugLevel > 5) {
		printf("Calling postFunc %d\n", i);
	    }
	    status = (*encapCallbacks[i].postFunc)(procPtr, hostID, infoPtr,
						   bufPtr, failure);
#ifdef lint
	    status = Vm_FinishMigration(procPtr, hostID, infoPtr, bufPtr,
					failure);
#endif /* lint */
	}
	if (status != SUCCESS) {
	    failure = 1;
	}
	bufPtr += infoPtr->size;
    }
    free(buffer);

    if (failure) {
	goto failure;
    }

    Proc_Lock(procPtr);

    procPtr->genFlags = (procPtr->genFlags & ~PROC_MIGRATING) |
	PROC_MIGRATION_DONE;
    Proc_Unlock(procPtr);


    /*
     * Tell the other host to resume the process.
     */
    cmd.command = PROC_MIGRATE_CMD_RESUME;
    cmd.remotePid = procPtr->peerProcessID;

    if (proc_MigDebugLevel > 5) {
	printf("Issuing resume command.\n");
    }
    status = ProcMigCommand(procPtr->peerHostID, &cmd, (Proc_MigBuffer *) NIL,
			    (Proc_MigBuffer *) NIL);

    if (status != SUCCESS) {
	printf("Warning: Proc_MigrateTrap: error encountered resuming process:\n\t%s.\n",
	       Stat_GetMsg(status));
	goto failure;
    }

    /*
     * If not migrating back home, note the dependency on the other host.
     */
    if (!foreign) {
	Proc_AddMigDependency(procPtr->processID, hostID);
    }
    
    WakeupCallers();
    if (proc_DoTrace && proc_MigDebugLevel > 1) {
	record.flags = (foreign ? 0 : PROC_MIGTRACE_HOME);
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_MIGTRAP,
		     (ClientData) &record);
    }

#ifndef CLEAN
    if (proc_DoTrace && proc_MigDebugLevel > 0 && !foreign) {
	record.processID = procPtr->processID;
	record.flags = PROC_MIGTRACE_HOME;
	record.info.filler = NIL;
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_END_MIG,
		     (ClientData) &record);
    }
#endif /* CLEAN */   
#ifdef MEM_TRASH
    proc_MemDebug = lastMemDebug;
#endif /* MEM_TRASH */
   
    if (foreign) {
	ProcExitProcess(procPtr, -1, -1, -1, TRUE);
    } else {
	Sched_ContextSwitch(PROC_MIGRATED);
    }
    panic("Proc_MigrateTrap: returned from context switch.\n");
    return;

 failure:
    procPtr->genFlags &= ~PROC_MIGRATING;
    KillRemoteCopy(procPtr, hostID);
    WakeupCallers();
    if (proc_DoTrace && proc_MigDebugLevel > 0 && !foreign) {
	record.processID = procPtr->processID;
	record.flags = PROC_MIGTRACE_HOME;
	record.info.filler = NIL;
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_END_MIG,
		     (ClientData) &record);
    }
    Sig_SendProc(procPtr, SIG_KILL, (int) status);
#ifdef MEM_TRASH
    proc_MemDebug = lastMemDebug;
#endif /* MEM_TRASH */

}

/*
 *----------------------------------------------------------------------
 *
 * ProcMigReceiveProcess --
 *
 *	Receive the encapsulated state of a process from another host
 *	and deencapsulate it by calling the appropriate callback routines.
 *	If deencapsulation succeeds, resume the migrated process.
 *
 * Results:
 *	A ReturnStatus indicates whether deencapsulation succeeds.  If
 * 	a module returns an error, the first error is returned and the
 *	rest of the state is not deencapsulated.
 *
 * Side effects:
 *	The process is resumed on this host.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
ReturnStatus
ProcMigReceiveProcess(cmdPtr, procPtr, inBufPtr, outBufPtr)
    ProcMigCmd *cmdPtr;/* contains ID of process on this host */
    Proc_ControlBlock *procPtr; /* ptr to process control block */
    Proc_MigBuffer *inBufPtr;	/* input buffer */
    Proc_MigBuffer *outBufPtr;	/* output buffer (stays NIL) */
{
    ReturnStatus status;
    Address bufPtr;
    register int i;
    Proc_EncapInfo *infoPtr;

    Proc_Lock(procPtr);

    /*
     * Go through the list of callbacks to generate the size of the buffer
     * we'll need.  In unusual circumstances, a caller may return a status
     * other than SUCCESS. In this case, the process should be continuable!
     */
    bufPtr = inBufPtr->ptr;
    for (i = 0; i < PROC_MIG_NUM_CALLBACKS; i++) {
	infoPtr = (Proc_EncapInfo *) bufPtr;
	bufPtr += sizeof(Proc_EncapInfo);
	if (infoPtr->token != encapCallbacks[i].token) {
	    if (proc_MigDebugLevel > 0) {
		panic("ProcMigReceiveProcess: mismatch deencapsulating process (continuable -- but call Fred)");
	    }
	    Proc_Unlock(procPtr);
	    return(PROC_MIGRATION_REFUSED);
	}
	if (infoPtr->special) {
	    /*
	     * This is where we'd like to handle special cases like shared
	     * memory processes.  For now, this should never happen.
	     */
	    if (proc_MigDebugLevel > 0) {
		panic("ProcMigReceiveProcess: special flag set?! (continuable -- but call Fred)");
	    }
	    Proc_Unlock(procPtr);
	    return(PROC_MIGRATION_REFUSED);
	}
	if (proc_MigDebugLevel > 5) {
	    printf("Calling deencapFunc %d\n", i);
	}
	status = (*encapCallbacks[i].deencapFunc)(procPtr, infoPtr, bufPtr);
#ifdef lint
	status = DeencapProcState(procPtr, infoPtr, bufPtr);
	status = Vm_DeencapState(procPtr, infoPtr, bufPtr);
	status = Fs_DeencapFileState(procPtr, infoPtr, bufPtr);
	status = Mach_DeencapState(procPtr, infoPtr, bufPtr);
	status = Prof_DeencapState(procPtr, infoPtr, bufPtr);
	status = Sig_DeencapState(procPtr, infoPtr, bufPtr);
#endif /* lint */
	if (status != SUCCESS) {
	    printf("Warning: ProcMigReceiveProcess: error returned by deencapsulation procedure %s:\n\t%s.\n",
#ifdef BREAKS_KDBX
		   callbackNames[i].deencapFunc,
#else /* BREAKS_KDBX */
		   "(can't get name)", 
#endif /* BREAKS_KDBX */
		   Stat_GetMsg(status));
	    Proc_Unlock(procPtr);
	    return(status);
	}
	bufPtr += infoPtr->size;
    }

    Proc_Unlock(procPtr);

    return(status);
}



/*
 * Define the process state that is sent during migration.
 * See proc.h for explanations of these fields.
 * This structure is followed by a variable-length string containing
 * procPtr->argString, padded to an integer boundary.
 */
typedef struct {
    Proc_PID		parentID;
    int			familyID;
    int			userID;
    int			effectiveUserID;
    int			genFlags;
    int			syncFlags;
    int			schedFlags;
    int			exitFlags;
    int 		billingRate;
    unsigned int 	recentUsage;
    unsigned int 	weightedUsage;
    unsigned int 	unweightedUsage;
    Proc_Time		kernelCpuUsage;
    Proc_Time		userCpuUsage;
    Proc_Time		childKernelCpuUsage;
    Proc_Time		childUserCpuUsage;
    int 		numQuantumEnds;
    int			numWaitEvents;
    unsigned int 	schedQuantumTicks;
    int			argStringLength;
} EncapState;

#define COPY_STATE(from, to, field) to->field = from->field

/*
 * A process is allowed to update its userID, effectiveUserID,
 * billingRate, or familyID.  If any of these fields is modified, all
 * of them are transferred to the remote host.
 */

typedef struct {
    int			familyID;
    int			userID;
    int			effectiveUserID;
    int 		billingRate;
} UpdateEncapState;

/*
 *----------------------------------------------------------------------
 *
 * GetEncapSize --
 *
 *	Determine the size of the encapsulated process state.
 *
 * Results:
 *	SUCCESS is returned directly; the size of the encapsulated state
 *	is returned in infoPtr->size.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static ReturnStatus
GetEncapSize(procPtr, hostID, infoPtr)
    Proc_ControlBlock *procPtr;			/* process being migrated */
    int hostID;					/* host to which it migrates */
    Proc_EncapInfo *infoPtr;			/* area w/ information about
						 * encapsulated state */
{
    infoPtr->size = sizeof(EncapState) +
	Byte_AlignAddr(strlen(procPtr->argString) + 1);
    /*
     * The clientData part of the EncapInfo struct is used to indicate
     * that the process is migrating home.
     */
    if (procPtr->genFlags & PROC_FOREIGN) {
	infoPtr->data = (ClientData) 1;
	if (proc_MigDebugLevel > 4) {
	    printf("Encapsulating foreign process %x.\n", procPtr->processID);
	}
    } else {
	infoPtr->data = (ClientData) 0;
	if (proc_MigDebugLevel > 4) {
	    printf("Encapsulating local process %x.\n", procPtr->processID);
	}
    }

    return(SUCCESS);	
}


/*
 *----------------------------------------------------------------------
 *
 * EncapProcState --
 *
 *	Encapsulate the state of a process from the Proc_ControlBlock
 *	and return it in the buffer provided.
 *
 * Results:
 *	SUCCESS.  The buffer is filled.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static ReturnStatus
EncapProcState(procPtr, hostID, infoPtr, bufPtr)
    register Proc_ControlBlock 	*procPtr; /* The process being migrated */
    int hostID;				  /* host to which it migrates */
    Proc_EncapInfo *infoPtr;
    Address bufPtr;
{
    int argStringLength;
    EncapState *encapPtr = (EncapState *) bufPtr;

    COPY_STATE(procPtr, encapPtr, parentID);
    COPY_STATE(procPtr, encapPtr, familyID);
    COPY_STATE(procPtr, encapPtr, userID);
    COPY_STATE(procPtr, encapPtr, effectiveUserID);
    COPY_STATE(procPtr, encapPtr, genFlags);
    COPY_STATE(procPtr, encapPtr, syncFlags);
    COPY_STATE(procPtr, encapPtr, schedFlags);
    COPY_STATE(procPtr, encapPtr, exitFlags);
    COPY_STATE(procPtr, encapPtr, billingRate);
    COPY_STATE(procPtr, encapPtr, recentUsage);
    COPY_STATE(procPtr, encapPtr, weightedUsage);
    COPY_STATE(procPtr, encapPtr, unweightedUsage);
    COPY_STATE(procPtr, encapPtr, kernelCpuUsage);
    COPY_STATE(procPtr, encapPtr, userCpuUsage);
    COPY_STATE(procPtr, encapPtr, childKernelCpuUsage);
    COPY_STATE(procPtr, encapPtr, childUserCpuUsage);
    COPY_STATE(procPtr, encapPtr, numQuantumEnds);
    COPY_STATE(procPtr, encapPtr, numWaitEvents);
    COPY_STATE(procPtr, encapPtr, schedQuantumTicks);

    bufPtr += sizeof(EncapState);
    argStringLength = Byte_AlignAddr(strlen(procPtr->argString) + 1);
    encapPtr->argStringLength = argStringLength;
    strncpy(bufPtr, procPtr->argString, argStringLength);

    return(SUCCESS);
}


/*
 *----------------------------------------------------------------------
 *
 * DeencapProcState --
 *
 *	Get information from a Proc_ControlBlock from another host.
 *	The information is contained in the parameter ``buffer''.
 *	The process control block should be locked on entry and exit.
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	None.
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
static ReturnStatus
DeencapProcState(procPtr, infoPtr, bufPtr)
    register Proc_ControlBlock 	*procPtr; /* The process being migrated */
    Proc_EncapInfo *infoPtr;		  /* information about the buffer */
    Address bufPtr;			  /* buffer containing data */
{
    Boolean 		home;
    EncapState *encapPtr = (EncapState *) bufPtr;

    if (infoPtr->data == 0) {
	if (proc_MigDebugLevel > 4) {
	    printf("Deencapsulating foreign process %x.\n", procPtr->processID);
	}
	home = FALSE;
    } else {
	if (proc_MigDebugLevel > 4) {
	    printf("Deencapsulating local process %x.\n", procPtr->processID);
	}
	home = TRUE;
    }


    COPY_STATE(encapPtr, procPtr, parentID);
    COPY_STATE(encapPtr, procPtr, familyID);
    COPY_STATE(encapPtr, procPtr, userID);
    COPY_STATE(encapPtr, procPtr, effectiveUserID);
    COPY_STATE(encapPtr, procPtr, genFlags);
    COPY_STATE(encapPtr, procPtr, syncFlags);
    COPY_STATE(encapPtr, procPtr, schedFlags);
    COPY_STATE(encapPtr, procPtr, exitFlags);
    COPY_STATE(encapPtr, procPtr, billingRate);
    COPY_STATE(encapPtr, procPtr, recentUsage);
    COPY_STATE(encapPtr, procPtr, weightedUsage);
    COPY_STATE(encapPtr, procPtr, unweightedUsage);
    COPY_STATE(encapPtr, procPtr, kernelCpuUsage);
    COPY_STATE(encapPtr, procPtr, userCpuUsage);
    COPY_STATE(encapPtr, procPtr, childKernelCpuUsage);
    COPY_STATE(encapPtr, procPtr, childUserCpuUsage);
    COPY_STATE(encapPtr, procPtr, numQuantumEnds);
    COPY_STATE(encapPtr, procPtr, numWaitEvents);
    COPY_STATE(encapPtr, procPtr, schedQuantumTicks);

    bufPtr += sizeof(*encapPtr);
    if (procPtr->argString != (char *) NIL) {
	free(procPtr->argString);
    }
    procPtr->argString = (char *) malloc(encapPtr->argStringLength);
    bcopy(bufPtr, (Address) procPtr->argString, encapPtr->argStringLength);

    procPtr->genFlags |= PROC_NO_VM;
    if (home) {
	procPtr->genFlags &= (~PROC_FOREIGN);
	procPtr->kcallTable = mach_NormalHandlers;
    } else {
	procPtr->genFlags |= PROC_FOREIGN;
	procPtr->kcallTable = mach_MigratedHandlers;
    }
    procPtr->genFlags &= ~(PROC_MIG_PENDING | PROC_MIGRATING);
    procPtr->schedFlags &=
	~(SCHED_STACK_IN_USE | SCHED_CONTEXT_SWITCH_PENDING);

    /*
     * Initialize some of the fields as if for a new process.  If migrating
     * home, these are already set up.   Fix up dependencies.
     */
    procPtr->state 		= PROC_NEW;
    Vm_ProcInit(procPtr);
    procPtr->event			= NIL;

    if (!home) {
	/*
	 *  Initialize our child list to remove any old links.
	 */
	List_Init((List_Links *) procPtr->childList);

	/*
	 * Remember the dependency on the other host.
	 */
	Proc_AddMigDependency(procPtr->processID, procPtr->peerHostID);
    } else {
	/*
	 * Forget the dependency on the other host; we're running
	 * locally now.
	 */
	Proc_RemoveMigDependency(procPtr->processID);
    }


    if (proc_MigDebugLevel > 4) {
	printf("Received process state for process %x.\n", procPtr->processID);
    }

    return(SUCCESS);
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_MigUpdateInfo --
 *
 *	Send the updateable portions of the state of a process to the
 *	host on which it is currently executing.  This requires
 *	packaging the relevant information from the Proc_ControlBlock
 *	and sending it via an RPC.
 *
 * 	The process is assumed to be locked.
 *
 * Results:
 *	A ReturnStatus is returned to indicate the status of the RPC.
 *
 * Side effects:
 *	A remote procedure call is performed and the process state
 *	is transferred.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Proc_MigUpdateInfo(procPtr)
    Proc_ControlBlock 	*procPtr; /* The migrated process */
{
    ReturnStatus status;
    ProcMigCmd cmd;
    UpdateEncapState state;
    UpdateEncapState *statePtr = &state;
    Proc_MigBuffer inBuf;

    COPY_STATE(procPtr, statePtr, familyID);
    COPY_STATE(procPtr, statePtr, userID);
    COPY_STATE(procPtr, statePtr, effectiveUserID);
    COPY_STATE(procPtr, statePtr, billingRate);

    /*
     * Set up for the RPC.
     */
    cmd.command = PROC_MIGRATE_CMD_UPDATE;
    cmd.remotePid = procPtr->peerProcessID;
    inBuf.size = sizeof(UpdateEncapState);
    inBuf.ptr = (Address) statePtr;

    status = ProcMigCommand(procPtr->peerHostID, &cmd, &inBuf,
			    (Proc_MigBuffer *) NIL);

    if (status != SUCCESS && proc_MigDebugLevel > 0) {
	printf("Warning: Proc_MigUpdateInfo: error returned updating information on host %d:\n\t%s.\n",
		procPtr->peerHostID,Stat_GetMsg(status));
    }
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * ProcMigGetUpdate --
 *
 *	Receive the updateable portions of the state of a process from its
 *	home node.
 *
 * Results:
 *	SUCCESS.
 *
 * Side effects:
 *	The process's control block is locked and then updated.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
ReturnStatus
ProcMigGetUpdate(cmdPtr, procPtr, inBufPtr, outBufPtr)
    ProcMigCmd *cmdPtr;/* contains ID of process on this host */
    Proc_ControlBlock *procPtr; /* ptr to process control block */
    Proc_MigBuffer *inBufPtr;	/* input buffer */
    Proc_MigBuffer *outBufPtr;	/* output buffer (stays NIL) */
{
    UpdateEncapState *statePtr = (UpdateEncapState *) inBufPtr->ptr;

    Proc_Lock(procPtr);
    COPY_STATE(statePtr, procPtr, familyID);
    COPY_STATE(statePtr, procPtr, userID);
    COPY_STATE(statePtr, procPtr, effectiveUserID);
    COPY_STATE(statePtr, procPtr, billingRate);
    Proc_Unlock(procPtr);
    return(SUCCESS);

}


/*
 *----------------------------------------------------------------------
 *
 * ProcMigEncapCallback --
 *
 *	Handle a callback on behalf of a module requesting more data.
 *	Not yet implemented.
 *
 * Results:
 *	A ReturnStatus, dependenet on the module doing the callback.
 *
 * Side effects:
 *	Variable.
 *
 *----------------------------------------------------------------------
 */

/* ARGSUSED */
ReturnStatus
ProcMigEncapCallback(cmdPtr, procPtr, inBufPtr, outBufPtr)
    ProcMigCmd *cmdPtr;/* contains ID of process on this host */
    Proc_ControlBlock *procPtr; /* ptr to process control block */
    Proc_MigBuffer *inBufPtr;	/* input buffer */
    Proc_MigBuffer *outBufPtr;	/* output buffer (stays NIL) */
{
    return(FAILURE);
}


/*
 *----------------------------------------------------------------------
 *
 * KillRemoteCopy --
 *
 *	Inform the remote host that a failure occurred during migration,
 * 	so the incomplete process on the remote host will kill the process.
 *	This just sets up and issues a MigCommand.
 *
 * Results:
 *	A ReturnStatus is returned, from the RPC.
 *
 * Side effects:
 *	A remote procedure call is performed and the migrated process
 *	is killed.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
KillRemoteCopy(procPtr, hostID)
    register Proc_ControlBlock 	*procPtr;    /* The process being migrated */
    int hostID;				     /* Host to which it migrates */
{
    ReturnStatus status;
    ProcMigCmd cmd;

    /*
     * Set up for the RPC.
     */
    cmd.command = PROC_MIGRATE_CMD_DESTROY;
    cmd.remotePid = procPtr->peerProcessID;

    status = ProcMigCommand(hostID, &cmd, (Proc_MigBuffer *) NIL,
			    (Proc_MigBuffer *) NIL);

    if (status != SUCCESS && proc_MigDebugLevel > 2) {
	printf("Warning: KillRemoteCopy: error returned by Rpc_Call: %s.\n",
		Stat_GetMsg(status));
    }
}



/*
 *----------------------------------------------------------------------
 *
 * Proc_FlagMigration --
 *
 *	Mark a process as waiting to migrate.  This will cause the process
 *	to trap to the Proc_MigrateTrap routine after the next time it
 *	traps into the kernel. 
 *
 *	The calling routine is assumed to hold the lock for the process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The process is flagged for migration.  If the process is suspended,
 *	it is resumed.
 *
 *----------------------------------------------------------------------
 */

void
Proc_FlagMigration(procPtr, hostID)
    Proc_ControlBlock 	*procPtr;	/* The process being migrated */
    int hostID;				/* Host to which it migrates */
{

    procPtr->genFlags |= PROC_MIG_PENDING;
    procPtr->peerHostID = hostID;
    if (procPtr->state == PROC_SUSPENDED) {
	Sig_SendProc(procPtr, SIG_RESUME, 0);
    }
    Sig_SendProc(procPtr, SIG_MIGRATE_TRAP, 0);

}


/*
 *----------------------------------------------------------------------
 *
 * InitiateMigration --
 *	
 *	Send a message to a specific workstation requesting permission to
 *	migrate a process.
 *
 * Results:
 *	SUCCESS is returned if permission is granted.
 *	PROC_MIGRATION_REFUSED is returned if the host is not accepting
 *		migrated processes or it is not at the right migration
 *		level.
 *	GEN_INVALID_ID if the user doesn't have permission to migrate
 *		from this host or to the other host.
 *	Other errors may be returned by the rpc module.
 *
 * Side effects:
 *	A message is sent to the remote workstation.
 *
 *----------------------------------------------------------------------
 */

static ReturnStatus
InitiateMigration(procPtr, hostID)
    register Proc_ControlBlock *procPtr;    	/* process to migrate */
    int hostID;			      		/* host to which to migrate */
{
    ReturnStatus status;
    ProcMigInitiateCmd init;
    ProcMigCmd cmd;
    Proc_MigBuffer inBuf;
    Proc_MigBuffer outBuf;
    Proc_PID pid;
    int foreign;
    

    init.processID = procPtr->processID;
    init.version = proc_MigrationVersion;
    init.userID = procPtr->userID;
    init.clientID = rpc_SpriteID;
    if (procPtr->genFlags & PROC_FOREIGN) {
	foreign = 1;
	cmd.remotePid = procPtr->peerProcessID;
    } else {
	foreign = 0;
	cmd.remotePid = (Proc_PID) NIL;
    }
    cmd.command = PROC_MIGRATE_CMD_INIT;
    
    inBuf.ptr = (Address) &init;
    inBuf.size = sizeof(ProcMigInitiateCmd);

    outBuf.ptr = (Address) &pid;
    outBuf.size = sizeof(Proc_PID);

    status = ProcMigCommand(hostID, &cmd, &inBuf, &outBuf);

    if (status != SUCCESS) {
	if (proc_MigDebugLevel > 0) {
	    printf(
		   "%s InitiateMigration: Error returned by host %d:\n\t%s\n",
		   "Warning:", hostID, Stat_GetMsg(status));
	}
    } else if (!foreign) {
	procPtr->peerProcessID = pid;
    }
    return(status);
}



/*
 *----------------------------------------------------------------------
 *
 * ProcMigCommand --
 *
 *	Send a process migration-related command to another host.
 *	This sets up and performs the RPC itself.
 *
 * RPC: Input parameters:
 *		process ID
 *		command to perform
 *		data buffer
 *	Return parameters:
 *		ReturnStatus
 *		data buffer
 *
 * Results:
 *	A ReturnStatus is returned to indicate the status of the RPC.
 *	The data buffer is filled by the RPC if a result is returned by
 *	the other host.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
ProcMigCommand(host, cmdPtr, inPtr, outPtr)
    int host;			 /* host to send command to */
    ProcMigCmd *cmdPtr; /* command to send */
    Proc_MigBuffer *inPtr;	 /* pair of <size, ptr> for input */
    Proc_MigBuffer *outPtr;	 /* pair of <size, ptr> for output */
{
    ReturnStatus status;
    Rpc_Storage storage;
    Proc_TraceRecord record;

#ifndef CLEAN
    if (proc_DoTrace && proc_MigDebugLevel > 2) {
	record.processID = cmdPtr->remotePid;
	record.flags = PROC_MIGTRACE_START;
	record.info.command.type = cmdPtr->command;
	record.info.command.data = (ClientData) NIL;
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_COMMAND,
		     (ClientData) &record);
    }
#endif /* CLEAN */   

    /*
     * Set up for the RPC.
     */
    storage.requestParamPtr = (Address) cmdPtr;
    storage.requestParamSize = sizeof(ProcMigCmd);

    if (inPtr == (Proc_MigBuffer *) NIL) {
	storage.requestDataPtr = (Address) NIL;
	storage.requestDataSize = 0;
    } else {
	storage.requestDataPtr = inPtr->ptr;
	storage.requestDataSize = inPtr->size;
    }

    storage.replyParamPtr = (Address) NIL;
    storage.replyParamSize = 0;

    if (outPtr == (Proc_MigBuffer *) NIL) {
	storage.replyDataPtr = (Address) NIL;
	storage.replyDataSize = 0;
    } else {
	storage.replyDataPtr = outPtr->ptr;
	storage.replyDataSize = outPtr->size;
    }


    status = Rpc_Call(host, RPC_PROC_MIG_COMMAND, &storage);

#ifndef CLEAN
    if (proc_DoTrace && proc_MigDebugLevel > 2) {
	record.flags = 0;
	Trace_Insert(proc_TraceHdrPtr, PROC_MIGTRACE_COMMAND,
		     (ClientData) &record);
    }
#endif /* CLEAN */   

    if (status != SUCCESS) {
	if (proc_MigDebugLevel > 6) {
	    printf("%s ProcMigCommand: error %x returned by Rpc_Call.\n",
		"Warning:", status);
	}
    }
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_WaitForMigration --
 *
 *	Monitored procedure to wait for a process to migrate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ENTRY ReturnStatus
Proc_WaitForMigration(processID)
    Proc_PID processID;
{
    Proc_ControlBlock *procPtr;
    ReturnStatus status;

    LOCK_MONITOR;

    Sync_LockRegister(LOCKPTR);

    procPtr = Proc_LockPID(processID);
    if (procPtr == NULL) {
	return(PROC_INVALID_PID);
    }
    while (procPtr->genFlags & (PROC_MIG_PENDING | PROC_MIGRATING)) {
	Proc_Unlock(procPtr);
	if (Sync_Wait(&migrateCondition, TRUE)) {
	    UNLOCK_MONITOR;
	    return(GEN_ABORTED_BY_SIGNAL);
	}
	Proc_Lock(procPtr);
    }
    if (procPtr->processID != processID) {
	status = PROC_INVALID_PID;
    } else if (procPtr->genFlags & PROC_MIGRATION_DONE) {
	status = SUCCESS;
    } else {
	status = FAILURE;
    }
    Proc_Unlock(procPtr);
    
    UNLOCK_MONITOR;
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * WakeupCallers --
 *
 *	Monitored procedure to signal any processes that may have waited for
 *	a process to migrate.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
static ENTRY void
WakeupCallers()
{

    LOCK_MONITOR;

    Sync_Broadcast(&migrateCondition);

    UNLOCK_MONITOR;
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_MigrateStartTracing --
 *
 *	Initialize the tracing variables for process migration.
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
Proc_MigrateStartTracing()
{
    static Boolean init = FALSE;

    if (!init) {
	init = TRUE;
	proc_TraceHdrPtr = &proc_TraceHeader;
	Trace_Init(proc_TraceHdrPtr, PROC_NUM_TRACE_RECS,
		   sizeof(Proc_TraceRecord), 0);
    }
    proc_DoTrace = TRUE;
}



/*
 *----------------------------------------------------------------------
 *
 * Proc_DestroyMigratedProc --
 *
 *	Kill a process, presumably when its peer host (the home host
 *	of a foreign process, or the remote host of a migrated process)
 *	is down.  It may also be done if the process is
 *	unsuccessfully killed with a signal, even if the remote host
 *	hasn't been down long enough to be sure it has crashed.
 *
 *	This procedure is distinct from Proc_KillRemoteCopy, which issues
 * 	a command to do a similar thing on the host to which the process
 * 	is migrating.  In this case, we're killing our own copy of it.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The process is killed.
 *
 *----------------------------------------------------------------------
 */

void 
Proc_DestroyMigratedProc(pidData) 
    ClientData pidData;		/* the process ID, as a ClientData */
{
    Proc_ControlBlock 		*procPtr; /* Process to kill. */
    Proc_PID pid = (Proc_PID) pidData;

    procPtr = Proc_LockPID(pid);
    if (procPtr == (Proc_ControlBlock *) NIL) {
	if (proc_MigDebugLevel > 0) {
	    printf("Warning: Proc_DestroyMigratedProc: process %x not found.\n",
		      (int) pid);
	}
	/*
	 * Make sure the dependency on this process goes away.
	 */
	Proc_RemoveMigDependency(pid);
	return;
    }
    if ((procPtr->state != PROC_MIGRATED) &&
	!(procPtr->genFlags & PROC_FOREIGN)) {
	if (proc_MigDebugLevel > 0) {
	    printf("%s Proc_DestroyMigratedProc: process %x not migrated.\n",
		      "Warning:", (int) pid);
	}
	Proc_Unlock(procPtr);
	/*
	 * Make sure the dependency on this process goes away.
	 */
	Proc_RemoveMigDependency(pid);
	return;
    }

    if (procPtr->state == PROC_MIGRATED ||
	(procPtr->genFlags & PROC_MIGRATING) ||
	procPtr->state == PROC_NEW) {
	/*
	 * Perform an exit on behalf of the process -- it's not
	 * in a state where we can signal it.  Unlock the
	 * process again, since ProcExitProcess locks it.  [Is there
	 * any race condition?  e.g., ProcExitProcess must be careful
	 * about the process it's passed.]
	 */
	Proc_Unlock(procPtr);
	ProcExitProcess(procPtr, PROC_TERM_DESTROYED, (int) PROC_NO_PEER, 0,
			FALSE);
	Proc_RemoveMigDependency(pid);
    } else {
	/*
	 * Let it get killed the normal way, and let the exit routines
	 * handle cleaning up dependencies.
	 */
	Sig_SendProc(procPtr, SIG_KILL, (int) PROC_NO_PEER);
	Proc_Unlock(procPtr);
    }

}


/*
 *----------------------------------------------------------------------
 *
 * Proc_EvictForeignProcs --
 *
 *	Evict all foreign processes from this machine.  To do this, send
 *	each foreign process the SIG_MIGRATE_HOME signal.
 *
 * Results:
 *	If sending any signals returns a non-SUCCESS status, that status
 *	is returned.  Otherwise, SUCCESS is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Proc_EvictForeignProcs()
{
    ReturnStatus status;
    
    status = Proc_DoForEveryProc(Proc_IsMigratedProc, Proc_EvictProc, TRUE);
    return(status);
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_IsMigratedProc --
 *
 *	Return whether the specified process is foreign.  (This routine is
 * 	a callback procedure that may be passed as a parameter to routines
 *	requiring an arbitrary Boolean procedure operating on a PCB.)
 *
 * Results:
 *	Boolean result: TRUE if foreign, FALSE if not.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

Boolean
Proc_IsMigratedProc(procPtr)
    Proc_ControlBlock *procPtr;
{
    if (procPtr->genFlags & PROC_FOREIGN) {
	return(TRUE);
    } else {
	return(FALSE);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_EvictProc --
 *
 *	Send a process the SIG_MIGRATE_HOME signal.  Note that if
 *	the process is not foreign, then the signal will be ignored.
 *	(This routine is a callback procedure that may be passed as a
 *    	parameter to routines requiring an arbitrary procedure
 *    	operating on a Proc_PID and returning a ReturnStatus.)
 *
 * Results:
 *	The value from Sig_Send is returned.
 *
 * Side effects:
 *	The specified process is signalled.
 *
 *----------------------------------------------------------------------
 */

ReturnStatus
Proc_EvictProc(pid)
    Proc_PID pid;
{
    ReturnStatus status;
    
    status = Sig_Send(SIG_MIGRATE_HOME, 0, pid, FALSE);
    return(status); 
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_NeverMigrate --
 *
 *	Flag a process so it will never be migrated.  This may be
 * 	used to keep the master of a pseudo-device from migrating, or
 * 	a process with kernel addresses mapped into user space from
 *	migrating.  The process is flagged as unmigrateable for the rest of
 * 	the lifetime of the process.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	The process's genFlags field is modified.
 *
 *----------------------------------------------------------------------
 */

void
Proc_NeverMigrate(procPtr)
    Proc_ControlBlock *procPtr;
{

    Proc_Lock(procPtr);
    procPtr->genFlags |= PROC_DONT_MIGRATE;
    Proc_Unlock(procPtr);
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_GetEffectiveProc --
 *
 *	Get a pointer to the Proc_ControlBlock for the process that is
 *	*effectively* running on the current processor.  Thus, for an
 *	RPC server performing a system call on behalf of a migrated process,
 *	the "effective" process will be the process that invoked the system
 *	call.  In all other cases, the "effective" process will be the
 *	same as the "actual" process.
 *
 * Results:
 *	A pointer to the process is returned.  If no process is active,
 *	NIL is returned.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
Proc_ControlBlock *
Proc_GetEffectiveProc()
{
    Proc_ControlBlock *procPtr;

    procPtr = proc_RunningProcesses[Mach_GetProcessorNumber()];
    if (procPtr == (Proc_ControlBlock *) NIL ||
	    procPtr->rpcClientProcess ==  (Proc_ControlBlock *) NIL) {
	return(procPtr);
    }
    return(procPtr->rpcClientProcess);
}


/*
 *----------------------------------------------------------------------
 *
 * Proc_SetEffectiveProc --
 *
 *	Set the "effective" process on the current processor.  If the
 *	process is (Proc_ControlBlock) NIL, the effective process is
 *	the same as the real process.
 *
 * Results:
 *	None.  
 *
 * Side effects:
 *	The "rpcClientProcess" field of the current process's
 *	Proc_ControlBlock is set to hold the effective process.
 *
 *----------------------------------------------------------------------
 */
void
Proc_SetEffectiveProc(procPtr)
    Proc_ControlBlock *procPtr;
{
    Proc_ControlBlock *actualProcPtr;

    actualProcPtr = Proc_GetActualProc();
    if (actualProcPtr == (Proc_ControlBlock *) NIL) {
	panic("Proc_SetEffectiveProcess: current process is NIL.\n");
    } else {
	actualProcPtr->rpcClientProcess = procPtr;
    }
}

