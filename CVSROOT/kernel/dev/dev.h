/*
 * dev.h --
 *
 *     Types, constants, and macros exported by the device module.
 *
 * Copyright 1985, 1988 Regents of the University of California
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies.  The University of California
 * makes no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without
 * express or implied warranty.
 *
 *
 * $Header$ SPRITE (Berkeley)
 */

#ifndef _DEV
#define _DEV

#include "status.h"
#ifdef KERNEL
#include "devSyslog.h"
#include "devDependent.h"
#else
#include <kernel/devSyslog.h>
#include <kernel/devDependent.h>
#endif


/*
 * The filesystem and the device module cooperate to translate from
 * filesystem block numbers to disk addresses.  Hence, this simple
 * type and the bytes per sector are exported.
 */
typedef struct Dev_DiskAddr {
    int cylinder;
    int head;
    int sector;
} Dev_DiskAddr;
/*
 *	DEV_BYTES_PER_SECTOR the common size for disk sectors.
 */
#define DEV_BYTES_PER_SECTOR	512

extern void	Dev_Init();
extern void	Dev_Config();
extern void	Dev_GatherDiskStats();
extern int	Dev_GetDiskStats();
extern void	Dev_InvokeConsoleCmd();
extern void	Dev_RegisterConsoleCmd();

#endif /* _DEV */
