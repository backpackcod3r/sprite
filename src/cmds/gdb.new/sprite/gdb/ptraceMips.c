/*
 * ptrace.c --
 *
 *	Routines for creating a Unix like debugger interface to Sprite.
 *
 * Copyright 1991 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that this copyright
 * notice appears in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * $Header: /sprite/src/kernel/mach/spur.md/RCS/machConfig.c,v 1.2 88/11/11 15:3
6:43 mendel Exp $ SPRITE (Berkeley)
 */

#include <stdio.h> 
#include "sprite.h"
#include "status.h"
#include <sys/types.h>
#include "sys/ptrace.h"
#include <errno.h>
#include <proc.h>
#include <signal.h>
#include <sys/wait.h>

static int mapStatusToErrno();

/* 
 *----------------------------------------------------------------------
 *
 * ptrace --
 *
 *	Emulate Unix ptrace system call.
 *
 * Results:
 *	An integer.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */

int
ptrace (request,pid,addr,data, addr2)
     int request;	/* Data
     int pid;			/* Process id of debugee. */
     char *addr;		
     int data;
     char *addr2;
{
     extern int	errno;
     Proc_DebugState process_state;
     int	returnData;
     ReturnStatus	status;


#if 0
     printf("ptrace: request %d, id 0x%x, addr 0x%x, data 0x%x\n", 
	 request, pid, addr);
#endif
     errno = 0;
     if ((int) addr < 0) {
	 printf("Negative addr\n");
     }
     switch (request) {
        case PT_TRACE_ME: { 	 /* 0, by tracee to begin tracing */
	     extern int	_execDebug;
	     /*
	      * This is a hack to emulate the ptrace(0) request.  ptrace(0) 
	      * seems to cause the exec() to stop after the first instruction.
	      * Setting _execDebug to true causes the Proc_Exec call to
	      * enter the debug queue after the first instruction.
	      */
	      _execDebug = 1;
	      return 0;
	}
        case PT_READ_I:    /* 1, read word from text segment */
        case PT_READ_D:  { /* 2, read word from data segment */
	    status = Proc_Debug(pid,PROC_READ, sizeof(int),addr,
			    &returnData);
	    errno = mapStatusToErrno(status);
	    return errno ? -1 : returnData; 
        }
        case PT_WRITE_I:    /* 4, write word into text segment */
        case PT_WRITE_D: {  /* 5, write word into data segment */
	    status = Proc_Debug(pid,PROC_WRITE, sizeof(int),&data,addr);
	    if (status != 0) {
		fprintf(stderr,
		    "Proc_Debug failed status = %x, pid = %x, addr = %x\n",
		    status, pid, addr);
	    }

	    errno = mapStatusToErrno(status);
	    return errno ? -1 : data; 
        }
        case PT_CONTINUE: {       /* 7, continue process */
	    if (data != 0 && data != 1) {
		if (kill(pid,data) != 0) {
			perror("kill");
			return -1;
		}
	    }
	    status = Proc_Debug(pid,PROC_CONTINUE,0,0,0);
	    errno = mapStatusToErrno(status);
	    return errno ? -1 : 0; 
        }
        case PT_KILL: {      /* 8, terminate process */
	    if (kill(pid,SIGKILL) != 0) {
		perror("kill");
		return -1;
	    }
	    status= Proc_Debug(pid,PROC_DETACH_DEBUGGER,0,0,0);
	    (void) kill(pid,SIGKILL); /* Sometimes Sprite takes more than
				       * one kill.
				       */
	    (void) kill(pid,SIGCONT); 
	    status= Proc_Debug(pid,PROC_DETACH_DEBUGGER,0,0,0);
	    (void) kill(pid,SIGKILL); /* Sometimes Sprite takes more than
				       * one kill.
				       */
	    (void) kill(pid,SIGCONT); 
	    (void) kill(pid,SIGKILL); /* Sometimes Sprite takes more than
				       * one kill.
				       */
	    status= Proc_Debug(pid,PROC_DETACH_DEBUGGER,0,0,0);
	    if (status == PROC_INVALID_PID) {
		status = SUCCESS;
	    }
	    errno = mapStatusToErrno(status);
	    return errno ? -1 : 0; 
	}
        case PT_STEP:{ /* 9, single step process */
	    status = Proc_Debug(pid,PROC_SINGLE_STEP,0,0,0);
	    errno = mapStatusToErrno(status);
	    return errno ? -1 : 0; 
        }
        case PT_ATTACH: {  /* 10, attach to an existing process */
	    status = Proc_Debug(pid,PROC_GET_THIS_DEBUG,0,0,0);
	    errno = mapStatusToErrno(status);
	    return errno ? -1 : 0; 
	}
        case PT_DETACH: {  /* 11, detach from a process */
	    if (data != 0 && data != 1) {
		if (kill(pid,data) != 0) {
			perror("kill");
			return -1;
		}
	    }
	    status = Proc_Debug(pid,PROC_DETACH_DEBUGGER,0,0,0);
	    errno = mapStatusToErrno(status);
	    return errno ? -1 : 0; 
	}
       case PT_READ_U: {   /* 3, read word from user struct */
	    Mach_RegState	*regPtr;
	    int			reg;
	    reg = (int) addr;
	    if ((reg >= 0) && (reg <  NPTRC_REGS)) {
		status = Proc_Debug(pid,PROC_GET_DBG_STATE,0,0,&process_state);
		errno = mapStatusToErrno(status);
		if (errno) {
		    return -1;
		}
		regPtr = &process_state.regState;
		if (reg < FPR_BASE) {
		    returnData = regPtr->regs[reg];
		} else if (reg < FPR_BASE + NFP_REGS) {
		    returnData = regPtr->fpRegs[reg - FPR_BASE];
		} else if (reg == PC) {
		    returnData = (int) regPtr->pc;
		} else if (reg == MMHI) {
		    returnData = regPtr->mfhi;
		} else if (reg == MMLO) {
		    returnData = regPtr->mflo;
		} else if (reg == FPC_CSR) {
		    returnData = regPtr->fpStatusReg;
		} else {
		    returnData = -1;
		}
		return returnData;
	    } else {
		return -1;
	    }
	}
       case PT_WRITE_U: {   /* 6, write word into user struct */
	    Mach_RegState	*regPtr;
	    int			reg;
	    reg = (int) addr;
	    if ((reg >= 0) && (reg <  NPTRC_REGS)) {
		status = Proc_Debug(pid,PROC_GET_DBG_STATE,0,0,&process_state);
		errno = mapStatusToErrno(status);
		if (errno) {
		    return -1;
		}
		regPtr = &process_state.regState;
		if (reg < FPR_BASE) {
		    regPtr->regs[reg] = data;
		} else if (reg < FPR_BASE + NFP_REGS) {
		    regPtr->fpRegs[reg - FPR_BASE] = data;
		} else if (reg == PC) {
		    regPtr->pc = (Address) data;
		} else if (reg == MMHI) {
		    regPtr->mfhi = data;
		} else if (reg == MMLO) {
		    regPtr->mflo = data;
		} else if (reg == FPC_CSR) {
		    regPtr->fpStatusReg = data;
		} else {
		    return -1;
		}
		status = Proc_Debug(pid,PROC_SET_DBG_STATE,0,&process_state,0);
	    } else {
		return -1;
	    }
	}
       default: {
		return -1;
	}
    }
}

/* 
 *----------------------------------------------------------------------
 *
 * wait --
 *
 *	Emulate the Unix wait system call when used to wait for a 
 *	process being ptraced.
 *
 * Results:
 *	An integer.
 *
 * Side effects:
 *
 *----------------------------------------------------------------------
 */
int
wait (statusPtr)
    union wait *statusPtr;
{
    ReturnStatus	status;
    Proc_DebugState process_state;
    extern int inferior_pid;

    if (!inferior_pid) {
	error("Trying to wait for nonexistant process");
    }
    /*
     * Wait for this process to enter the debug list.
     */
    status = Proc_Debug(inferior_pid,PROC_GET_THIS_DEBUG,0,0,0);
    if (status == SUCCESS) {
	if (statusPtr == (union wait *) 0) {
	    goto reapChild;
	}
	status = Proc_Debug(inferior_pid,PROC_GET_DBG_STATE,0,0,&process_state);
	if (status == SUCCESS) {
	    int unixSignal;
            statusPtr->w_status = 0;
            statusPtr->w_stopval = WSTOPPED;
            if (process_state.termReason == PROC_TERM_SUSPENDED) {
                (void)Compat_SpriteSignalToUnix(process_state.termStatus,
					       &unixSignal);
                statusPtr->w_stopsig = unixSignal;
		if (process_state.termStatus == SIG_DEBUG ||
		    process_state.termStatus == SIG_TRACE_TRAP ||
		    process_state.termStatus == SIG_BREAKPOINT) {
		    statusPtr->w_stopsig = SIGTRAP;
		}
            } else if (process_state.termReason == PROC_TERM_SIGNALED ||
                       process_state.termReason == PROC_TERM_RESUMED) {
                (void)Compat_SpriteSignalToUnix(process_state.termStatus, 
						&unixSignal);
                statusPtr->w_stopsig = unixSignal;
		if (process_state.termStatus == SIG_DEBUG ||
		    process_state.termStatus == SIG_TRACE_TRAP ||
		    process_state.termStatus == SIG_BREAKPOINT) {
		    statusPtr->w_stopsig = SIGTRAP;
		}
            } else {
		statusPtr->w_status = 0;
                statusPtr->w_retcode = process_state.termStatus;
		(void) ptrace(PT_DETACH,inferior_pid,0,SIGKILL,0);
            }
	    return inferior_pid;
	}
    }
reapChild:
    { 
        Proc_PID	pid;
	int		reason, childStatus, subStatus;
	status = Proc_Wait(1, (int *) &inferior_pid, PROC_WAIT_BLOCK, 
		&pid, &reason,  &childStatus, &subStatus, 
		(Proc_ResUsage *) NULL);
        if (statusPtr != NULL)  {
	    int		unixSignal;
            statusPtr->w_status = 0;
            if (reason == PROC_TERM_SUSPENDED) {
                (void)Compat_SpriteSignalToUnix(childStatus, &unixSignal);
                statusPtr->w_stopval = WSTOPPED;
                statusPtr->w_stopsig = unixSignal;
            } else if (reason == PROC_TERM_SIGNALED ||
                       reason == PROC_TERM_RESUMED) {
                (void)Compat_SpriteSignalToUnix(childStatus, &unixSignal);
                statusPtr->w_stopval = WSTOPPED;
                statusPtr->w_termsig = unixSignal;
            } else {
                statusPtr->w_retcode = childStatus;
		(void) ptrace(PT_DETACH,inferior_pid,0,SIGKILL,0);
            }
        }
    }
    return inferior_pid;
}




/* 
 *----------------------------------------------------------------------
 *
 * mapStatusToErrno --
 *
 *	Map a Sprite status return code to a Unix errno. This is intended 
 *	to work only on the status codes returns from Proc_Debug.
 *
 * Results:
 *	An integer.
 *
 * Side effects:
 *      None
 *
 *----------------------------------------------------------------------
 */

static int
mapStatusToErrno(status)
    ReturnStatus	status;
{
    int	returnValue;

    if (status == SUCCESS) {
	return 0;
    }

    switch (status) {

    case PROC_INVALID_PID:
	returnValue = ESRCH;
	break;

    case SYS_INVALID_ARG:
	returnValue = EINVAL;
	break;

    case SYS_ARG_NOACCESS:
	returnValue = EINVAL;
	break;

    case GEN_ABORTED_BY_SIGNAL:
	returnValue = EINTR;
	break;

    default:
	returnValue = EIO;
	break;
    }
    return returnValue;
}

