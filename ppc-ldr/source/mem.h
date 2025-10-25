/*
 * GBA Linux Loader - GCN/Wii host side - Memory
 *
 * Copyright (C) 2025 Techflash
 */
#ifndef _MEM_H
#define _MEM_H

/* same pointer as different sizes */
union memRegion {
	u32 *w32;
	u16 *w16;
	u8  *w8;
};

struct _memState {
	struct {
		union memRegion ptr;
		int size;
	} blocks[2];
};

extern struct _memState M_State;
extern void *M_GuestToHost(u32 addr);

/* this seems to be as high as we can go before stuff starts to break :( */
#define MEM1_BUF_SZ (21 * 1024 * 1024)


#endif /* _MEM_H */
