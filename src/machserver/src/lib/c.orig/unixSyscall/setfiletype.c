/* 
 * setfiletype.c --
 *
 *	Procedure to map from a Unix-like setfiletype call to the Sprite
 *	Fs_SetAttributes system call.
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
static char rcsid[] = "$Header: /sprite/src/lib/c/unixSyscall/RCS/setfiletype.c,v 1.1 88/11/17 13:30:37 ouster Exp $ SPRITE (Berkeley)";
#endif not lint

#include "sprite.h"
#include "fs.h"

#include "compatInt.h"
#include <errno.h>


/*
 *----------------------------------------------------------------------
 *
 * setfiletype --
 *
 *	Procedure to map from Unix setfiletype system call to Sprite 
 *	Fs_SetAttributes system call.
 *
 * Results:
 *      UNIX_SUCCESS    - the call was successful.
 *      UNIX_ERROR      - the call was not successful.
 *                        The actual error code stored in errno.
 *
 * Side effects:
 *	The file type of the specified file is modified.
 *
 *----------------------------------------------------------------------
 */

int
setfiletype(path, type)
    char *path;
    int type;
{
    ReturnStatus status;	/* result returned by Sprite system calls */
    Fs_Attributes attributes;	/* struct containing all file attributes,
				 * only file type looked at. */

    attributes.userType = type;
    status = Fs_SetAttr(path,  FS_ATTRIB_FILE, &attributes, FS_SET_FILE_TYPE);
    if (status != SUCCESS) {
	errno = Compat_MapCode(status);
	return(UNIX_ERROR);
    } else {
	return(UNIX_SUCCESS);
    }
}
