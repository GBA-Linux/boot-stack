/*
 * GBA Linux Loader - GBA Side - Host operations
 *
 * Copyright (C) 2025 Techflash
 */

#ifndef _HOST_H
#define _HOST_H

#include <gba_types.h>

extern void H_ReadMemBuf(void *buf, u32 addr, int len);
extern void H_WriteMemBuf(void *buf, u32 addr, int len);

#endif /* _HOST_H */
