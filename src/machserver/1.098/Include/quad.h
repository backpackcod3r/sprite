/*
 * quad.h --
 *
 *	Operations on 64-bit integers.
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
 * $Header: /sprite/src/lib/include/RCS/quad.h,v 1.1 91/03/17 22:38:11 kupfer Exp $ SPRITE (Berkeley)
 */

#ifndef _QUAD
#define _QUAD

#include <stdio.h>
#include <sys/types.h>

/* 
 * For the sake of compatibility, use the BSD definition of the 
 * types (i.e., use an array, rather than longs named "hi" and "lo"), 
 * and get them out of the standard place (types.h).
 * 
 * For the sake of simplicity, always treat word 0 as the least 
 * significant of the pair (though it might be better to pay attention 
 * to the byte order of the machine).
 */

#define QUAD_MOST_SIG	1
#define QUAD_LEAST_SIG	0


/*
 *----------------------------------------------------------------------
 *
 * Quad_EQ --
 *
 *	Tell whether two quads (or unsigned quads) are equal.
 *
 * Results:
 *	Returns non-zero if the given values are the same.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

#define Quad_EQ(q1, q2) \
    ((q1).val[QUAD_LEAST_SIG] == (q2).val[QUAD_LEAST_SIG] \
     && (q1).val[QUAD_MOST_SIG] == (q2).val[QUAD_MOST_SIG])



extern void	Quad_AddUns _ARGS_ ((u_quad uq1, u_quad uq2, 
				     u_quad *resultPtr));
extern void	Quad_AddUnsLong _ARGS_ ((u_quad quadVal, u_long longVal,
					 u_quad *resultPtr));
extern int	Quad_CompareUns _ARGS_ ((_CONST u_quad *uq1,
					 _CONST u_quad *uq2));
extern void	Quad_PutUns _ARGS_ ((FILE *s, u_quad q));
extern double	Quad_UnsToDouble _ARGS_  ((u_quad));

#endif /* _QUAD */
