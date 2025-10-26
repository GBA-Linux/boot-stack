/*
 * GBA Linux Loader - GCN/Wii host side - GBA Communications
 *
 * Copyright (C) 2025 Techflash
 *
 * Multiboot code derived from FIX94's gba-link-cable-rom-sender:
 *   Copyright (C) 2018 FIX94
 *   This software may be modified and distributed under the terms
 *   of the MIT license.  See the LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>
#include <fat.h>
#include "mem.h"
#include "comms.h"


#define LDR_PATH  "/apps/gba-linux-loader/linux-loader.gba"
#define KERN_PATH "/apps/gba-linux-loader/linux.elf"

/* stupid libogc not exporting functions.... */
extern u64 gettime(void);
extern u32 diff_msec(u64 start,u64 end);

static enum {
	STATE_READ_LINUX_LOADER, /* reading Linux loader */
	STATE_WAIT_GBA,          /* waiting for GBA to be connected */
	STATE_MULTIBOOT_SETUP,   /* setting up multiboot */
	STATE_MULTIBOOT,         /* doing multiboot */
	STATE_HANDSHAKE_EMU,     /* handshaking with emulator on GBA */
	STATE_READ_KERNEL,       /* reading the kernel */
	STATE_LOAD_KERNEL,       /* uploading the kernel */
	STATE_READY              /* ready to speak real protocol */
} curState = STATE_READ_LINUX_LOADER;

static bool multibootInitialized = false;
static struct stat statBuf;
static u8 *resbuf, *cmdbuf;
static vu32 transval, resval;
static void (*cmdCallbacks[7])(u32 rx);

#define SI_TRANS_DELAY 50
static void transcb(s32 chan, u32 ret) {
	transval = 1;
}

static u32 docrc(u32 crc, u32 val) {
	int i;
	for (i = 0; i < 0x20; i++) {
		if ((crc ^ val) & 1) {
			crc >>= 1;
			crc ^= 0xa1c1;
		}
		else
			crc >>= 1;
		val >>= 1;
	}
	return crc;
}


static inline void waitTransfer() {
	/* 350 is REALLY pushing it already, cant go further */
	do {
		usleep(350);
	} while(transval == 0);
}

static u32 calckey(u32 size) {
	u32 ret = 0;
	int res1, res2, res3;

	size = (size - 0x200) >> 3;
	res1 = (size & 0x3F80) << 1;
	res1 |= (size & 0x4000) << 2;
	res1 |= (size & 0x7F);
	res1 |= 0x380000;
	res2 = res1;
	res1 = res2 >> 0x10;
	res3 = res2 >> 8;
	res3 += res1;
	res3 += res2;
	res3 <<= 24;
	res3 |= res2;
	res3 |= 0x80808080;

	if ((res3 & 0x200) == 0) {
		ret |= (((res3) & 0xFF) ^ 0x4B) << 24;
		ret |= (((res3 >> 8) & 0xFF) ^ 0x61) << 16;
		ret |= (((res3 >> 16) & 0xFF) ^ 0x77) << 8;
		ret |= (((res3 >> 24) & 0xFF) ^ 0x61);
	}
	else {
		ret |= (((res3) & 0xFF) ^ 0x73) << 24;
		ret |= (((res3 >> 8) & 0xFF) ^ 0x65) << 16;
		ret |= (((res3 >> 16) & 0xFF) ^ 0x64) << 8;
		ret |= (((res3 >> 24) & 0xFF) ^ 0x6F);
	}
	return ret;
}

static void doreset() {
	cmdbuf[0] = 0xFF; /* reset */
	transval = 0;
	SI_Transfer(GBA_CHAN, cmdbuf, 1, resbuf, 3, transcb, SI_TRANS_DELAY);
	while (transval == 0);
}

static void getstatus() {
	cmdbuf[0] = 0; /* status */
	transval = 0;
	SI_Transfer(GBA_CHAN, cmdbuf, 1, resbuf, 3, transcb, SI_TRANS_DELAY);
	while (transval == 0);
}

static u32 recv() {
	memset(resbuf, 0, 32);
	cmdbuf[0] = 0x14; /* read */
	transval = 0;
	SI_Transfer(GBA_CHAN, cmdbuf, 1, resbuf, 5, transcb, SI_TRANS_DELAY);
	while (transval == 0);
	return *(vu32 *)resbuf;
}

#define srecv() __builtin_bswap32(recv())

static void send(u32 msg) { 
	u64 ticks, ticksNew;
	cmdbuf[0] = 0x15;
	cmdbuf[1] = (msg >> 0) & 0xFF;
	cmdbuf[2] = (msg >> 8) & 0xFF;
	cmdbuf[3] = (msg >> 16) & 0xFF;
	cmdbuf[4] = (msg >> 24) & 0xFF;

	transval = 0;
	resbuf[0] = 0;
	SI_Transfer(GBA_CHAN, cmdbuf, 5, resbuf, 1, transcb, SI_TRANS_DELAY);
	ticks = gettime();
	while (transval == 0) {
		ticksNew = gettime();
		if (diff_msec(ticks, ticksNew) > 60)
			break; /* give up */
	}
}


#define ssend(x)  send(__builtin_bswap32(x))
#if 0
#define psend(x)  send(parity(x))
#define spsend(x) ssend(parity(x))
#endif
#define csend(x)  send(crc(x))

static void readLinuxLoader(void) {
	FILE *fp;
	
	if (!fatInitDefault()) {
		puts("fatInitDefault() failed, can't read linux-loader.gba!");
		sleep(5);
		exit(1);
	}

	if (stat(LDR_PATH, &statBuf)) {
		perror("stat() on " LDR_PATH " failed");
		sleep(5);
		exit(1);
	}

	if (statBuf.st_size >= 256 * 1024) {
		puts(LDR_PATH " is larger than 256KB, something is wrong!");
		sleep(5);
		exit(1);
	}

	fp = fopen(LDR_PATH, "rb");
	if (!fp) {
		puts("Failed to open " LDR_PATH "!");
		sleep(5);
		exit(1);
	}

	if (fread(M_State.blocks[0].ptr.w8, statBuf.st_size, 1, fp) != 1) {
		fclose(fp);
		puts("Failed to read " LDR_PATH "!");
		sleep(5);
		exit(1);
	}

	fclose(fp);

	printf("Successfully read GBA Linux loader ROM (%llu bytes)\n", statBuf.st_size);
	curState = STATE_WAIT_GBA;
}

static void checkGBA(void) {
	u32 type;

	type = SI_GetType(GBA_CHAN);
	if (type & SI_GBA) {
		puts("Found a GBA!  Doing multiboot...");
		curState = STATE_MULTIBOOT_SETUP;
	}
	return;
}

static void doMultibootSetup(void) {
	u8 *gbaBuf = M_State.blocks[0].ptr.w8;
	if (!multibootInitialized) {
		cmdbuf = memalign(32,32);
		resbuf = memalign(32,32);
		multibootInitialized = true;
	}

	if (gbaBuf[0xB2] != 0x96) {
		printf("GBA header value incorrect (0x%02X)! Fixing value (0x96)\n", gbaBuf[0xB2]);
		gbaBuf[0xB2] = 0x96;
	}

	if(*(u32 *)(gbaBuf + 0xE4 ) == 0x0010A0E3 && *(u32 *)(gbaBuf + 0xEC ) == 0xC010A0E3 &&
	   *(u32 *)(gbaBuf + 0x100) == 0xFCFFFF1A && *(u32 *)(gbaBuf + 0x118) == 0x040050E3 &&
	   *(u32 *)(gbaBuf + 0x11C) == 0xFBFFFF1A && *(u32 *)(gbaBuf + 0x12C) == 0x020050E3 &&
	   *(u32 *)(gbaBuf + 0x130) == 0xFBFFFF1A && *(u32 *)(gbaBuf + 0x140) == 0xFEFFFF1A
	) {
		printf("Gamecube multiboot rom, patching entry point\n");
		/* jump over joyboot handshake */
		*(u32 *)(gbaBuf + 0xE0) = 0x170000EA;
	}

	curState = STATE_MULTIBOOT;
	return;
}

static void doMultiboot(void) {
	u32 sendsize, ourkey, fcrc, sessionkeyraw, sessionkey, enc;
	int i;
	u8 *gbaBuf = M_State.blocks[0].ptr.w8;
	size_t gbaSize = statBuf.st_size;

	puts("GBA Found! Waiting for BIOS...");
	resbuf[2]=0;

	while (!(resbuf[2] & 0x10)) {
		doreset();
		getstatus();
	}

	puts("GBA Ready, sending Linux Loader...");
	sendsize = (((gbaSize) + 7) & ~7);
	ourkey = calckey(sendsize);
	printf("Our Key: %08x\n", ourkey);

	/* get current sessionkey */
	sessionkeyraw = recv();
	sessionkey = __builtin_bswap32(sessionkeyraw ^ 0x7365646F);

	/* send over our own key */
	ssend(ourkey);

	fcrc = 0x15a0;

	/* send over gba header */
	for (i = 0; i < 0xC0; i+=4)
		ssend(*(vu32*)(gbaBuf + i));

	puts("Header done! Sending ROM...");
	for (i = 0xC0; i < sendsize; i+=4) {
		enc = (
			(gbaBuf[i + 3] << 24) | 
			(gbaBuf[i + 2] << 16) | 
			(gbaBuf[i + 1] << 8)  |
			(gbaBuf[i])
		);

		fcrc = docrc(fcrc, enc);
		sessionkey = (sessionkey * 0x6177614B) + 1;
		enc ^= sessionkey;
		enc ^= ((~(i + (0x20 << 20))) + 1);
		enc ^= 0x20796220;
		send(enc);
	}
	fcrc |= (sendsize << 16);
	printf("ROM done! CRC: %08x\n", fcrc);

	/* send over CRC */
	sessionkey = (sessionkey * 0x6177614B) + 1;
	fcrc ^= sessionkey;
	fcrc ^= ((~(i + (0x20 << 20))) + 1);
	fcrc ^= 0x20796220;
	send(fcrc);

	/* get crc back (unused) */
	recv();
	puts("GBA booted!  Waiting for handshake...");

	curState = STATE_HANDSHAKE_EMU;

	return;
}

static void doHandshake(void) {
	u32 rx;

	/* try to send a ping message */
	csend(CLASS_SYS | SYS_PING | 0 /* id */ | (0x4849 << DATA_SHIFT));

	/* try to get ACK response */
	rx = srecv();

	/* nonsense or corrupt */
	if (!crcValid(rx))
		return;

	/* check for ACK */
	if ((rx & PKT_CLASS)  != CLASS_SYS ||
	    (rx & PKT_SUBCMD) != SYS_ACK   ||
	    (rx & PKT_CMD_ID) != 0         ||
	    (rx & PKT_DATA)   != 0)
		return;

getReply:
	/* get new message for ping reply */
	rx = srecv();
	if (!crcValid(rx))
		goto getReply;

	/* didn't return our ping as-is */
	if ((rx & PKT_CLASS)  != CLASS_SYS      ||
	    (rx & PKT_SUBCMD) != SYS_PING_REPLY ||
	    (rx & PKT_CMD_ID) != 0              ||
	    (rx & PKT_DATA)   != (0x4849 << DATA_SHIFT))
		goto getReply;

	/* ACK the ping reply */
	csend(CLASS_SYS | SYS_ACK | 0 /* id */ | 0 /* data */);

	/* valid ping, start transferring kernel */
	puts("Got ping back from GBA!  Loading kernel...");
	curState = STATE_READ_KERNEL;

	return;
}

static void readKernel(void) {
	FILE *fp;

	if (stat(KERN_PATH, &statBuf)) {
		perror("stat() on " KERN_PATH " failed");
		sleep(5);
		exit(1);
	}

	if (statBuf.st_size >= 16 * 1024 * 1024) {
		puts(KERN_PATH " is larger than 16MB, something is wrong!");
		sleep(5);
		exit(1);
	}

	fp = fopen(KERN_PATH, "rb");
	if (!fp) {
		puts("Failed to open " KERN_PATH "!");
		sleep(5);
		exit(1);
	}

	if (fread(M_State.blocks[0].ptr.w8, statBuf.st_size, 1, fp) != 1) {
		fclose(fp);
		puts("Failed to read " KERN_PATH "!");
		sleep(5);
		exit(1);
	}

	fclose(fp);

	printf("Successfully read GBA Linux Kernel (%llu bytes)\n", statBuf.st_size);

	curState = STATE_LOAD_KERNEL;
	return;
}

static void loadKernel(void) {
	u32 rx;

	puts("sending...");
	csend(CLASS_SYS | SYS_KERNEL_LOAD | 0 /* id */ | 0);
	puts("receiving...");
	rx = srecv();

	/* nonsense or corrupt */
	if (!crcValid(rx)) {
		puts("invalid crc");
		return;
	}

	/* check for ACK */
	if ((rx & PKT_CLASS)  != CLASS_SYS ||
	    (rx & PKT_SUBCMD) != SYS_ACK   ||
	    (rx & PKT_CMD_ID) != 0         ||
	    (rx & PKT_DATA)   != 0) {
		printf("BS packet: 0x%08X\n", rx);
		return;
	}

	/* valid ACK */
	puts("GBA is now preparing to boot the kernel, entering main communications loop...");

	curState = STATE_READY;

	return;
}

static void memRead(void) {
	u32 rx, addr, length, tmp[2];
	u16 crcVal, crcValCalc;
	int i;

	csend(CLASS_SYS | SYS_ACK | 0 /* id */ | 0 /* data */);

	while (1) {
		rx = srecv();
		if ((rx & PKT_CLASS) == CLASS_MEM &&
		    (rx & PKT_SUBCMD) == MEM_READ)
			continue; /* HACK: fix weird issue where it reads the MEM_READ command multiple times */
		break;
	}

	addr = tmp[0] = rx;
	length = tmp[1] = srecv();
	printf("Got MEM_READ with addr=0x%08x, length=%u\n", addr, length);

	rx = srecv();
	if ((rx & PKT_CLASS)  != CLASS_SYS      ||
	    (rx & PKT_SUBCMD) != SYS_MW_TX_DONE ||
	    (rx & PKT_CMD_ID) != 0) {
		printf("Invalid data (0x%08x) for MW_TX_DONE 1\n", rx);
		return;
	}

	crcVal = (rx & PKT_DATA) >> DATA_SHIFT;
	crcValCalc = calc_crc16((u8 *)tmp, 2 * sizeof(u32));
	if (crcVal != crcValCalc) {
		printf("Invalid CRC (0x%04x != 0x%04x) for addr+len\n", crcVal, crcValCalc);
		return;
	}

	/* all checks out, ACK */
	csend(CLASS_SYS | SYS_ACK | 0 /* id */ | 0 /* data */);

	for (i = 0; i < length; i++) {
		//printf("Sending word %d / %d\n", i, length);
		usleep(1000); /* give it a bit between writes, it seems to desync if we spam it too hard */
		send(*(u32 *)M_GuestToHost(addr + (i * sizeof(u32))));
		usleep(1000);
	}
	puts("doing CRCs and sending it");

	/* sent memory, send SYS_MW_TX_DONE */
	/* FIXME: if this crosses a memblock boundary, we're screwed */
	crcVal = calc_crc16((u8 *)M_GuestToHost(addr), length * sizeof(u32));
	csend(CLASS_SYS | SYS_MW_TX_DONE | 0 /* id */ | (crcVal << DATA_SHIFT) /* data */);

	puts("read done");

	return;
}

static void memWrite(void) {

}

static void doEmuComms(void) {
	u32 rx;

	rx = srecv();
	if (rx == 0) /* anything going on? */
		return;

	if (!crcValid(rx)) {
		puts("parity invalid");
		return;
	}

	switch (rx & PKT_CLASS) {
	case CLASS_SYS: {
		switch (rx & PKT_SUBCMD) {
		case SYS_ACK: {
			u32 id;
			id = (rx & PKT_CMD_ID) >> CMD_ID_SHIFT;
			if (cmdCallbacks[id])
				cmdCallbacks[id](rx);
			else
				printf("Spurious ACK for command id %d\n", id);

			break;
		}
		case SYS_MW_TX_DONE:
		case SYS_PING: /* TODO: maybe actually implement ping + reply for mainloop */
		case SYS_PING_REPLY:
		case SYS_KERNEL_LOAD: {
			printf("Got weird SYS subcmd: 0x%08X\n", (rx & PKT_SUBCMD) >> SUBCMD_SHIFT);
			sleep(1);
			break;
		}
		default: {
			printf("Unknown SYS subcmd: 0x%08X (rx=0x%08x)\n", (rx & PKT_SUBCMD) >> SUBCMD_SHIFT, rx);
			break;
		}
		}
		break;
	}
	case CLASS_MEM: {
		switch (rx & PKT_SUBCMD) {
		case MEM_READ: {
			memRead();
			break;
		}
		case MEM_WRITE: {
			memWrite();
			break;
		}
		default: {
			printf("Unknown MEM subcmd: 0x%08X\n", (rx & PKT_SUBCMD) >> SUBCMD_SHIFT);
			break;
		}
		}
		break;
	}
	default: {
		printf("Unknown class: 0x%08x\n", (rx & PKT_CLASS) >> CLASS_SHIFT);
		break;
	}
	}
}

void C_Process(void) {
	switch (curState) {
	case STATE_READ_LINUX_LOADER: {
		readLinuxLoader();
		break;
	}
	case STATE_WAIT_GBA: {
		checkGBA();
		break;
	}
	case STATE_MULTIBOOT_SETUP: {
		doMultibootSetup();
		break;
	}
	case STATE_MULTIBOOT: {
		doMultiboot();
		break;
	}
	case STATE_HANDSHAKE_EMU: {
		doHandshake();
		break;
	}
	case STATE_READ_KERNEL: {
		readKernel();
		break;
	}
	case STATE_LOAD_KERNEL: {
		loadKernel();
		break;
	}
	case STATE_READY: {
		doEmuComms();
		break;
	}
	}
	return;
}
