/*
 * GBA Linux Loader - GBA Side - Host operations
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <gba_sio.h>
#include <gba_types.h>
#include "comms.h"

void H_ReadMemBuf(void *buf, u32 addr, int len) {
	u32 tmp[2], rx;
	u16 crcVal, calcCrcVal;
	int i;
	printf("Reading %dB from 0x%08lx\n", len, addr);
tryStart:

	/* start read */
	while (REG_JSTAT & 0xa); /* drain buffers */
	puts("Sending MEM_READ");
	REG_JOYTR = crc(CLASS_MEM | MEM_READ | 0 /* id */ | (2 << DATA_SHIFT) /* 2x u32 to describe goal */);

	/* set up our read */
	tmp[0] = __builtin_bswap32(addr);
	tmp[1] = __builtin_bswap32((len + 3) / 4);
	crcVal = calc_crc16((u8 *)tmp, 2 * sizeof(u32));
	tmp[0] = addr;
	tmp[1] = (len + 3) / 4;

	/* wait for host to read our command */
	while (REG_JSTAT & 0x8);

	puts("waiting for ACK 1");
	/* wait for incoming ACK */
	while (1) {
		while (!(REG_JSTAT & 0x2));
		rx = REG_JOYRE;

		if (!crcValid(rx)) {
			puts("invalid CRC (ACK 1)");
			goto tryStart;
		}

		if ((rx & PKT_CLASS) != CLASS_SYS ||
		   (rx & PKT_SUBCMD) != SYS_ACK   ||
		   (rx & PKT_CMD_ID) != 0         ||
		   (rx & PKT_DATA)   != 0) {
			puts("invalid data (ACK 1)");
			goto tryStart;
		}

		break;
	}

	/* write addr, len, and CRC */
	puts("Sending address, len, CRC");
	while (REG_JSTAT & 0x8);
	REG_JOYTR = tmp[0]; /* addr */
	while (REG_JSTAT & 0x8);
	REG_JOYTR = tmp[1]; /* len */
	while (REG_JSTAT & 0x8);
	REG_JOYTR = crc(CLASS_SYS | SYS_MW_TX_DONE | 0 /* id */ | (crcVal << DATA_SHIFT));
	while (REG_JSTAT & 0x8);

	puts("waiting for ACK 2");

	/* wait for incoming ACK */
	while (1) {
		while (!(REG_JSTAT & 0x2));
		rx = REG_JOYRE;

		if (!crcValid(rx)) {
			puts("invalid CRC (ACK 2)");
			goto tryStart;
		}

		if ((rx & PKT_CLASS) != CLASS_SYS ||
		   (rx & PKT_SUBCMD) != SYS_ACK   ||
		   (rx & PKT_CMD_ID) != 0         ||
		   (rx & PKT_DATA)   != 0) {
			puts("invalid data (ACK 2)");
			goto tryStart;
		}

		break;
	}
	puts("Got ACK!  Reading data...");

	/* we got an ACK, we now have tmp[1] + 1 words incoming */
	for (i = 0; i < tmp[1]; i++) {
		u32 *buf32 = (u32 *)buf;

		//printf("Waiting for word %d/%d\n", i, tmp[1]);
		while (!(REG_JSTAT & 0x2));
		buf32[i] = __builtin_bswap32(REG_JOYRE); /* put it back into BE temporarily for the CRC */
	}

	while (!(REG_JSTAT & 0x2));
	rx = REG_JOYRE;

	if ((rx & PKT_CLASS) != CLASS_SYS      ||
	   (rx & PKT_SUBCMD) != SYS_MW_TX_DONE ||
	   (rx & PKT_CMD_ID) != 0) {
		puts("invalid data (MW_TX_DONE)");
		//while(1);
		goto tryStart;
	}

	crcVal = (rx & PKT_DATA) >> DATA_SHIFT;
	calcCrcVal = calc_crc16(buf, tmp[1] * sizeof(u32));

	if (crcVal != calcCrcVal) {
		printf("invalid CRC on data (0x%08x != 0x%08x)\n", crcVal, calcCrcVal);
		//while(1);
		goto tryStart;
	}

	/* we actually do want to keep it in BE, we care about it being byte-identical, not word-interpretation-identical */
#if 0
	for (i = 0; i < tmp[1]; i++) {
		u32 *buf32 = (u32 *)buf;
		buf32[i] = __builtin_bswap32(buf32[i]); /* put it back into LE so we can use it */
	}
#endif

	/* success! */
	puts("memory read done!!");
	return;
}


void H_WriteMemBuf(void *buf, u32 addr, int len) {
	printf("Writing %dB to 0x%08lx\n", len, addr);
}
