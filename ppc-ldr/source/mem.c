/*
 * GBA Linux Loader - GCN/Wii host side - Memory
 *
 * Copyright (C) 2025 Techflash
 */
#include <gccore.h>
#include "mem.h"
void *M_GuestToHost(u32 addr) {
	if (addr < M_State.blocks[0].size)
		return M_State.blocks[0].ptr.w8 + addr;
	else if (M_State.blocks[1].size)
		return M_State.blocks[1].ptr.w8 + (addr - M_State.blocks[0].size);
	else
		return NULL;
	/* TODO: virtual ramdisk? */
}
