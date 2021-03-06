/* 
 * MigHostCache.c --
 *
 *	Internal routine to manage the cache of available hosts.
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
static char rcsid[] = "$Header: /sprite/src/lib/c/mig/RCS/MigHostCache.c,v 2.1 90/09/24 14:46:46 douglis Exp $ SPRITE (Berkeley)";
#endif /* not lint */

#include <sprite.h>
#include <hash.h>
#include <stdlib.h>
#include <mig.h>
#include "migInt.h"

static Hash_Table table;


/*
 *----------------------------------------------------------------------
 *
 * MigHostCache --
 *
 *	Add, remove, or verify an entry in the host cache.
 *
 * Results:
 *	TRUE if the host is in the cache at the start of the operation,
 *	FALSE otherwise.  
 *
 * Side effects:
 *	Hash table is updated.
 *
 *----------------------------------------------------------------------
 */

int
MigHostCache(hostID, op, callback)
    int hostID;			/* Host to operate on. */
    MigCacheOp op;		/* Operation to perform. */
    int callback;		/* Whether to invoke callback function */
{
    static int init = 0;
    Hash_Entry *entryPtr;	/* Entry in hash table. */
    int found;

    if (!init) {
	if (op == MIG_CACHE_REMOVE_ALL) {
	    return(0);
	}
	Hash_InitTable(&table, 0, HASH_ONE_WORD_KEYS);
	init = 1;
    }

    switch (op) {
	case MIG_CACHE_ADD: {
	    /*
	     * Create the entry if it doesn't exist.  Sets third arg
	     * to TRUE if it creates it, meaning it wasn't found, so
	     * we have to negate it.
	     */
	    entryPtr = Hash_CreateEntry(&table, (Address) hostID, &found);
	    found = !found;
	    Hash_SetValue(entryPtr, (ClientData) hostID);
	    break;
	}
	case MIG_CACHE_VERIFY: 
	case MIG_CACHE_REMOVE: {
	    entryPtr = Hash_FindEntry(&table, (Address) hostID);
	    found = (entryPtr != (Hash_Entry *) NULL);
	    if (op == MIG_CACHE_REMOVE && found) {
		Hash_DeleteEntry(&table, entryPtr);
	    }
	    if (callback && (migCallBackPtr != NULL)) {
		(*migCallBackPtr)(hostID);
	    }
		
	    break;
	}
	case MIG_CACHE_REMOVE_ALL: {
	    if (callback && (migCallBackPtr != NULL)) {
		Hash_Search search;
		for (entryPtr = Hash_EnumFirst(&table, &search);
		     entryPtr != NULL; entryPtr = Hash_EnumNext(&search)) {
		    hostID = (int) Hash_GetValue(entryPtr);
		    (*migCallBackPtr)(hostID);
		}
	    }
	    Hash_DeleteTable(&table);
	    init = 0;
	    return(0);
	}
	default: {
	    return(0);
	}
    }
    return(found);
}

