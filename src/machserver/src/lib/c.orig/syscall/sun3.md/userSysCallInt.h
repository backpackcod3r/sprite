|*
|* userSysCallInt.h --
|*
|*     Contains macro for stubs for user-level system calls.
|*
|* Copyright 1985, 1988 Regents of the University of California
|* Permission to use, copy, modify, and distribute this
|* software and its documentation for any purpose and without
|* fee is hereby granted, provided that the above copyright
|* notice appear in all copies.  The University of California
|* makes no representations about the suitability of this
|* software for any purpose.  It is provided "as is" without
|* express or implied warranty.
|*
|* rcs = $Header: userSysCallInt.h,v 1.1 88/06/19 14:30:08 ouster Exp $ SPRITE (Berkeley)
|*

#include "kernel/sysSysCall.h"
#ifndef _USERSYSCALLINT
#define _USERSYSCALLINT
/*
 * ----------------------------------------------------------------------------
 *
 * SYS_CALL --
 *
 *      Define a user-level system call.  The call sets up a trap into a 
 *	system-level routine with the appropriate constant passed as
 * 	an argument to specify the type of system call.
 * ----------------------------------------------------------------------------
 */

#define SYS_CALL(name, constant) \
	.globl _/**/name; _/**/name: \
	movl #constant, sp@- ; \
	movl #constant, d0; \
	trap #1; \
	addql #4, sp ; \
	1: rts;

#endif _USERSYSCALLINT
