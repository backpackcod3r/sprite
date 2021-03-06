/* 
 * on_exit.c --
 *
 *	This file contains the source code for the "on_exit" library
 *	procedure.  "on_exit" is sort of just like "at_exit" except
 *      you can pass an argument.
 *
 * Copyright 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/src/lib/c/stdlib/RCS/on_exit.c,v 1.1 92/03/27 13:40:40 rab Exp Locker: rab $ SPRITE (Berkeley)";
#endif /* not lint */

#include <stdlib.h>

/*
 * Variables shared with exit.c:
 */

extern void (*_exitHandlers[])();	/* Function table. */
extern int _exitNumHandlers;		/* Number of functions currently
					 * registered in table. */
extern long _exitHandlerArgs[];		/* Arguments to pass to functions. */
extern int _exitTableSize;		/* Number of entries in table. */

/*
 *----------------------------------------------------------------------
 *
 * on_exit --
 *
 *	Register a function ("func") to be called as part of process
 *	exiting.  One argment can be passed.
 *
 * Results:
 *	The return value is 0 if the registration was successful,
 *	and -1 if registration failed because the table was full.
 *
 * Side effects:
 *	Information will be remembered so that when the process exits
 *	(by calling the "exit" procedure), func will be called.  Func
 *	takes no arguments and returns no result.  If the process
 *	terminates in some way other than by calling exit, then func
 *	will not be invoked.
 *
 *----------------------------------------------------------------------
 */

int
on_exit(func, arg)
    void (*func)();			/* Function to call during exit. */
    long arg;
{
    if (_exitNumHandlers >= _exitTableSize) {
	return -1;
    }
    _exitHandlers[_exitNumHandlers] = func;
    _exitHandlerArgs[_exitNumHandlers] = arg;
    _exitNumHandlers += 1;
    return 0;
}
