/* 
 * WordTest.c --
 *
 *	Routine to test a bitfield one word at a time.
 *
 * Copyright 1990 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 */

#ifndef lint
static char rcsid[] = "$Header: /sprite/lib/forms/RCS/proto.c,v 1.3 90/01/12 12:03:36 douglis Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <bf.h>


/*
 *----------------------------------------------------------------------
 *
 * WordTest --
 *
 * 	Wrapper for Bf_Wordest.
 *
 * Results:
 *	Result from Bf_Wordest.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
WordTest(ptr, offset, size, value)
    unsigned int	*ptr;
    int			offset;
    int			size;
    int			value;
{
    return Bf_WordTest(ptr, offset, size, value);
}

