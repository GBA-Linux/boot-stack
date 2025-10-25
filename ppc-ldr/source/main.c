/*
 * GBA Linux Loader - GCN/Wii host side - Main bootstrap
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <gccore.h>

#ifdef HW_RVL
	#include <wiiuse/wpad.h>
#endif

#include "mem.h"
#include "console.h"
#include "comms.h"

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

/* our buffer in MEM1 */
u8 mem1_buf[MEM1_BUF_SZ];

struct _memState M_State;

int main(int argc, char **argv) {
	int mem1_blkSz;
	RVL_ONLY(int i; int mem2_blkSz);
	union memRegion mem1_blk;
	RVL_ONLY(union memRegion mem2_blk);

	VIDEO_Init();

	PAD_Init();
	RVL_ONLY(WPAD_Init());

	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb,20,20,rmode->fbWidth-20,rmode->xfbHeight-20,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
	/*SYS_STDIO_Report(true);*/
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	VIDEO_SetBlack(false);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	puts("Setting up memory...");


#ifdef HW_RVL
	/*
	 * try to allocated progressively smaller chunks
	 * until it fits in MEM2, tries in 256KB chunks,
	 * starting at 64MB.
	 */
	for (i = 256; i > 0; i--) {
		mem2_blk.w8 = malloc(i * 256 * 1024);
		if (!mem2_blk.w8) {
			/* failed, no good */
			continue;
		}
		if (mem2_blk.w8 <= (u8 *)0x90000000) {
			/* in MEM1, no good */
			free(mem2_blk.w8);
			continue;
		}
		break;
	}
	mem2_blkSz = i * 256 * 1024;
	printf("mem2_blk: %p, size=%dKB\n", mem2_blk.w8, mem2_blkSz / 1024);
	if (mem2_blk.w8 <= (u8 *)0x90000000 || !mem2_blk.w8) {
		puts("FATAL: Failed to get a valid piece of memory in in MEM2...\n");
		sleep(5);
		exit(1);
	}
	M_State.blocks[1].ptr = mem2_blk;
	M_State.blocks[1].size = mem2_blkSz;
#endif

	mem1_blkSz = MEM1_BUF_SZ;
	mem1_blk.w8 = mem1_buf;
	printf("mem1_blk: %p, size=%dKB\n", mem1_blk.w8, mem1_blkSz / 1024);
	if (mem1_blk.w8 >= (u8 *)0x90000000 || !mem1_blk.w8) {
		puts("FATAL: Failed to get a valid piece of memory in in MEM1...\n");
		sleep(5);
		exit(1);
	}
	M_State.blocks[0].ptr = mem1_blk;
	M_State.blocks[0].size = mem1_blkSz;

	printf("Cleaing memory... ");
	memset(mem1_blk.w8, 0, mem1_blkSz);
	RVL_ONLY(memset(mem2_blk.w8, 0, mem2_blkSz));
	puts("done");

	printf("Waiting for GBA connection on port %d...\nHOME (WiiMote)/Start (GCN Controller on port 1) to exit.\n", GBA_CHAN + 1);

	while(1) {
		u32 pressedG;
		RVL_ONLY(u32 pressedW);

		PAD_ScanPads();
#ifdef HW_RVL
		WPAD_ScanPads();
		pressedW = WPAD_ButtonsDown(0);
#endif
		pressedG = PAD_ButtonsDown(0);
		if (RVL_ONLY(pressedW & WPAD_BUTTON_HOME ||) pressedG & PAD_BUTTON_START) {
			puts("Bye!");
			sleep(5);
			exit(0);
		}

		C_Process();
	}

	return 0;
}
