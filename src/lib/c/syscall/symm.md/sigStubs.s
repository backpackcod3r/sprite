/*
 *
 * sigStubs.s --
 *
 *     Stubs for the Sig_ system calls.
 *
 * Copyright 1986, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 * rcs = $Header: /crg2/bruces6/sprite/src/lib/c/syscall/sym.md/RCS/sigStubs.s,v 1.1 90/01/19 10:19:24 fubar Exp $ SPRITE (Berkeley)
 *
 */
#include "userSysCallInt.h"

SYS_CALL(1,	Sig_RawPause, 		SYS_SIG_PAUSE)
SYS_CALL(3,	Sig_Send, 		SYS_SIG_SEND)
SYS_CALL(3,	Sig_SetAction, 		SYS_SIG_SETACTION)
SYS_CALL(2,	Sig_SetHoldMask, 	SYS_SIG_SETHOLDMASK)
