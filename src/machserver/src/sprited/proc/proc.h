/*
 * proc.h --
 *
 *	External declarations for process management.
 *
 * Copyright 1986, 1988, 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /user5/kupfer/spriteserver/src/sprited/proc/RCS/proc.h,v 1.14 92/06/02 15:00:43 kupfer Exp $ SPRITE (Berkeley)
 */

#ifndef _PROC
#define _PROC

#include <mach.h>

#ifdef SPRITED
#include <procTypes.h>
#else
#include <sprited/procTypes.h>
#endif

/*
 *  proc_PCBTable is the array of all valid PCB's in the system.
 *  It is initialized by Proc_Init at boot time to the appropriate size,
 *  which depends on the workstation configuration.
 */

extern Proc_ControlBlock **proc_PCBTable;


/*
 *   Keep track of the maximum number of processes at any given time.
 */

extern int proc_MaxNumProcesses;

/*
 *  Macros to manipulate process IDs.
 */

#define Proc_ComparePIDs(p1, p2) ((p1) == (p2))

#define Proc_GetPCB(pid) (proc_PCBTable[(pid) & PROC_INDEX_MASK])

#define Proc_ValidatePID(pid) \
    ((((pid) & PROC_INDEX_MASK) < proc_MaxNumProcesses) && \
     (((pid) == proc_PCBTable[(pid) & PROC_INDEX_MASK]->processID)))

#define PROC_GET_VALID_PCB(pid, procPtr) \
    if (((pid) & PROC_INDEX_MASK) >= proc_MaxNumProcesses) { \
	procPtr = (Proc_ControlBlock *) NIL; \
    } else { \
	procPtr = proc_PCBTable[(pid) & PROC_INDEX_MASK]; \
	if ((pid) != (procPtr)->processID) { \
	    procPtr = (Proc_ControlBlock *) NIL; \
	} \
    }

#define	Proc_GetHostID(pid) (((pid) & PROC_ID_NUM_MASK) >> PROC_ID_NUM_SHIFT)

/* Alias for compatibility with old code */
#define	Proc_GetActualProc() Proc_GetCurrentProc()

/*
 * Various routines use Proc_UseRpcBuffer to decide whether to copy 
 * to/from user address space or kernel address space.  A common 
 * example of using a kernel (RPC) buffer is when a migrated process 
 * invokes a system call that is forwarded home.
 */

#ifndef SPRITED_MIGRATION
#define	Proc_UseRpcBuffer() (FALSE)
#else
#define Proc_UseRpcBuffer() \
    (Proc_GetCurrentProc()->rpcClientProcess != \
		((Proc_ControlBlock *) NIL))
#endif

/*
 * Used to get the lock at the top of the lock stack without popping it off.
 */
#define Proc_GetCurrentLock(pcbPtr, typePtr, lockPtrPtr) \
    { \
	if ((pcbPtr)->lockStackSize <= 0) { \
	    *(typePtr) = -1; \
	    *(lockPtrPtr) = (Address) NIL; \
	} else { \
	    *(typePtr) = (pcbPtr)->lockStack[(pcbPtr)->lockStackSize-1].type; \
	    *(lockPtrPtr) = \
		(pcbPtr)->lockStack[(pcbPtr)->lockStackSize-1].lockPtr; \
	} \
    }

/* 
 * Set the process's state, with an optional debugging check.
 */
#ifdef CLEAN
#define Proc_SetState(procPtr, newState)	(procPtr)->state = (newState)
#else
extern void		Proc_SetState _ARGS_((Proc_ControlBlock *procPtr,
					     Proc_State newState));
#endif


/*
 *----------------------------------------------------------------------
 *
 * Proc_AssertLocked --
 *
 *	Assert that the given handle points to a locked PCB.  It is 
 *	expected that the programmer will verify by inspection that the 
 *	given pcb is in fact locked.  If we get really paranoid we could 
 *	add a test to verify that the entry is in fact locked.
 *
 * Results:
 *	Returns the given handle, cast appropriately.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */
    
#define Proc_AssertLocked(procPtr)	((Proc_LockedPCB *)(procPtr))


/* 
 * External procedures.
 */

extern ReturnStatus 	Proc_ByteCopy _ARGS_((Boolean copyIn, int numBytes,
				Address sourcePtr, Address destPtr));
extern void		Proc_CallFunc _ARGS_((void (*func)(
						   ClientData clientData, 
						   Proc_CallInfo *callInfoPtr),
				ClientData clientData, Time interval));
extern ClientData 	Proc_CallFuncAbsTime _ARGS_((void (*func)(
						   ClientData clientData, 
						   Proc_CallInfo *callInfoPtr),
				ClientData clientData, Timer_Ticks time));
extern void 		Proc_CancelCallFunc _ARGS_((ClientData token));
extern void		Proc_ContextSwitch _ARGS_((Proc_State newState));
extern	ReturnStatus	Proc_DoForEveryProc _ARGS_((Boolean (*booleanFuncPtr)
						(Proc_ControlBlock *pcbPtr),
				ReturnStatus (*actionFuncPtr)(Proc_PID pid), 
				Boolean ignoreStatus, int *numMatchedPtr));
extern	ReturnStatus	Proc_EvictForeignProcs _ARGS_((void));
extern Proc_ControlBlock *Proc_ExceptionToPCB _ARGS_((mach_port_t port));
extern	int		Proc_Exec _ARGS_((char *fileName, 
				char **argsPtrArray, char **envPtrArray,
				Boolean debugMe, int host));
extern int 		Proc_ExecEnv _ARGS_((char *fileName, 
				char **argPtrArray, char **envPtrArray, 
				Boolean debugMe));
extern void 		Proc_Exit _ARGS_((int status));
extern void 		Proc_ExitInt _ARGS_((int reason, int status, int code));
extern	int		Proc_Fork _ARGS_((Boolean shareHeap, Proc_PID *pidPtr));
extern Proc_ControlBlock *Proc_GetCurrentProc _ARGS_((void));
#ifdef SPRITED_MIGRATION
extern	Proc_ControlBlock *Proc_GetEffectiveProc _ARGS_((void));
#else
#define Proc_GetEffectiveProc()	Proc_GetCurrentProc()
#endif
extern	ReturnStatus	Proc_GetFamilyID _ARGS_((Proc_PID pid,
				Proc_PID *familyIDPtr));
extern	ReturnStatus	Proc_GetGroupIDs _ARGS_((int numGIDs, int *gidArrayPtr,
				int *trueNumGIDsPtr));
extern	ReturnStatus	Proc_GetHostIDs _ARGS_((int *virtualHostPtr, 
				int *physicalHostPtr));
extern	ReturnStatus	Proc_GetIDs _ARGS_((Proc_PID *procIDPtr,
				Proc_PID *parentIDPtr, int *userIDPtr,
				int *effUserIDPtr));
extern	ReturnStatus	Proc_GetIntervalTimer _ARGS_((int timerType,
				Proc_TimerInterval *userTimerPtr));
extern	Boolean		Proc_HasPermission _ARGS_((int userID));
extern void 		Proc_InformParent _ARGS_((register 
				Proc_LockedPCB *procPtr, int childStatus));
extern void		Proc_Init _ARGS_((void));
extern	void		Proc_InitMainEnviron _ARGS_((
				Proc_ControlBlock *procPtr));
extern void 		Proc_InitMainProc _ARGS_((void));
extern ReturnStatus	Proc_KernExec _ARGS_((Proc_ControlBlock *procPtr,
				char *execPath, char **argPtrArray));
extern void		Proc_Kill _ARGS_((Proc_LockedPCB *procPtr,
				int reason, int status));
extern	int		Proc_KillAllProcesses _ARGS_((Boolean userProcsOnly));
extern void		Proc_Lock _ARGS_((Proc_ControlBlock *procPtr));
extern ReturnStatus 	Proc_LockFamily _ARGS_((int familyID, 
				List_Links **familyListPtr, int *userIDPtr));
extern Proc_LockedPCB	*Proc_LockPID _ARGS_((Proc_PID pid));
extern void		Proc_MakeReady _ARGS_((Proc_LockedPCB *procPtr));
extern ReturnStatus 	Proc_MakeStringAccessible _ARGS_((int maxLength,
				char **stringPtrPtr, int *accessLengthPtr,
				int *newLengthPtr));
extern void 		Proc_MakeUnaccessible _ARGS_((Address addr, 
				int numBytes));
extern	ReturnStatus	Proc_MigGetStats _ARGS_((Address addr));
#ifdef SPRITED_MIGRATION
extern ReturnStatus	Proc_MigUpdateInfo _ARGS_((Proc_ControlBlock *procPtr));
#else
#define			Proc_MigUpdateInfo(p)	(FAILURE)
#endif
extern ReturnStatus	Proc_NewProc _ARGS_((Address startPC,
				Address stateAddr, int procType,
				Boolean shareHeap, Proc_PID *pidPtr,
				char *procName));
extern	void		Proc_PushLockStack _ARGS_((Proc_ControlBlock *pcbPtr,
				int type, Address lockPtr));
extern void 		Proc_Reaper _ARGS_((void));
extern	void		Proc_RemoveFromLockStack _ARGS_((
				Proc_ControlBlock *pcbPtr, Address lockPtr));
extern void		Proc_ResumeProcess _ARGS_((Proc_LockedPCB *procPtr,
				Boolean killingProc));
#ifdef SPRITED_MIGRATION
extern	ReturnStatus	Proc_RpcGetPCB _ARGS_((ClientData srvToken,
				int clientID, int command,
				Rpc_Storage *storagePtr));
extern	ReturnStatus	Proc_RpcMigCommand _ARGS_((ClientData srvToken,
				int hostID, int command, 
				Rpc_Storage *storagePtr));
extern	ReturnStatus	Proc_RpcRemoteCall _ARGS_((ClientData srvToken,
				int clientID, int command, 
				Rpc_Storage *storagePtr));
extern ReturnStatus 	Proc_RpcRemoteWait _ARGS_((ClientData srvToken,
				int clientID, int command, 
				Rpc_Storage *storagePtr));
#else /* SPRITED_MIGRATION */
#define			Proc_RpcGetPCB		Rpc_NotImplemented
#define			Proc_RpcMigCommand	Rpc_NotImplemented
#define			Proc_RpcRemoteCall	Rpc_NotImplemented
#define			Proc_RpcRemoteWait	Rpc_NotImplemented
#endif /* SPRITED_MIGRATION */
extern void 		Proc_ServerInit _ARGS_((void));
extern void 		Proc_ServerProc _ARGS_((void));
extern void		Proc_ServerProcTimes _ARGS_((void));
extern void		Proc_SetCurrentProc 
				_ARGS_((Proc_ControlBlock *procPtr));
extern	ReturnStatus	Proc_SetFamilyID _ARGS_((Proc_PID pid, 
				Proc_PID familyID));
extern	ReturnStatus	Proc_SetGroupIDs _ARGS_((int numGIDs, 
				int *gidArrayPtr));
extern	ReturnStatus	Proc_SetIDs _ARGS_((int userID, int effUserID));
extern	ReturnStatus	Proc_SetIntervalTimer _ARGS_((int timerType,
				Proc_TimerInterval *newTimerPtr,
				Proc_TimerInterval *oldTimerPtr));
extern	void		Proc_SetupEnviron _ARGS_((Proc_ControlBlock *procPtr));
extern ReturnStatus 	Proc_Start _ARGS_((Proc_ControlBlock *procPtr));
extern char 		*Proc_StateName _ARGS_((Proc_State state));
extern	ReturnStatus	Proc_StringNCopy _ARGS_((int numBytes, char *srcStr,
				char *destStr, int *strLengthPtr));
extern	void		Proc_SuspendProcess _ARGS_((
				Proc_LockedPCB *procPtr, Boolean debug,
				int termReason, int termStatus, 
				int termCode));
extern Proc_ControlBlock *Proc_SyscallToPCB _ARGS_((mach_port_t port));
extern void 		Proc_Unlock _ARGS_((Proc_LockedPCB *procPtr));
extern void		Proc_UnlockAndSwitch _ARGS_((Proc_LockedPCB *procPtr,
				Proc_State state));
extern void 		Proc_UnlockFamily _ARGS_((int familyID));
extern	ReturnStatus	Proc_Wait _ARGS_((int numPids, Proc_PID pidArray[],
				int flags, Proc_PID *procIDPtr, 
				int *reasonPtr, int *statusPtr, 
				int *subStatusPtr, Proc_ResUsage *usagePtr));
extern	ReturnStatus	Proc_WaitForHost _ARGS_((int hostID));
extern	void		Proc_WakeupAllProcesses _ARGS_((void));
extern	void		Proc_ZeroServerProcTimes _ARGS_((void));
extern	unsigned int	proc_NumServers;

#endif /* _PROC */
