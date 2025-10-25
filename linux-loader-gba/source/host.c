/*
 * GBA Linux Loader - GBA Side - Host operations
 *
 * Copyright (C) 2025 Techflash
 */

#include <stdio.h>
#include <string.h>
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
	REG_JOYTR = crc(CLASS_MEM | MEM_READ | 0 /* id */ | (2 << DATA_SHIFT) /* 2x u32 to describe goal */);

	/* set up our read */
	tmp[0] = __builtin_bswap32(addr);
	tmp[1] = __builtin_bswap32(len);
	crcVal = calc_crc16((u8 *)tmp, 2 * sizeof(u32));
	tmp[0] = addr;
	tmp[1] = len;

	/* wait for host to read our command */
	while (REG_JSTAT & 0x8);

	/* wait for incoming ACK */
	while (1) {
		while (!(REG_JSTAT & 0x2));
		rx = REG_JOYRE;

		if (!crcValid(rx))
			goto tryStart;

		if ((rx & PKT_CLASS) != CLASS_SYS ||
		   (rx & PKT_SUBCMD) != SYS_ACK   ||
		   (rx & PKT_CMD_ID) != 0         ||
		   (rx & PKT_DATA)   != 0)
			goto tryStart;

		break;
	}

	/* write addr, len, and CRC */
	while (REG_JSTAT & 0x8);
	REG_JOYTR = tmp[0]; /* addr */
	while (REG_JSTAT & 0x8);
	REG_JOYTR = tmp[1]; /* len */
	while (REG_JSTAT & 0x8);
	REG_JOYTR = crc(CLASS_SYS | SYS_MW_TX_DONE | 0 /* id */ | (crcVal << DATA_SHIFT));
	while (REG_JSTAT & 0x8);

	/* wait for incoming ACK */
	while (1) {
		while (!(REG_JSTAT & 0x2));
		rx = REG_JOYRE;

		if (!crcValid(rx))
			goto tryStart;

		if ((rx & PKT_CLASS) != CLASS_SYS ||
		   (rx & PKT_SUBCMD) != SYS_ACK   ||
		   (rx & PKT_CMD_ID) != 0         ||
		   (rx & PKT_DATA)   != 0)
			goto tryStart;

		break;
	}

	/* we got an ACK, we now have tmp[1] + 1 words incoming */
	for (i = tmp[1]; i > 0; i--) {
		u32 *buf32 = (u32 *)buf;

		while (!(REG_JSTAT & 0x2));
		buf32[i] = __builtin_bswap32(REG_JOYRE); /* put it back into BE temporarily for the CRC */
	}

	while (!(REG_JSTAT & 0x2));
	rx = REG_JOYRE;

	if ((rx & PKT_CLASS) != CLASS_SYS      ||
	   (rx & PKT_SUBCMD) != SYS_MW_TX_DONE ||
	   (rx & PKT_CMD_ID) != 0)
		goto tryStart;

	crcVal = (rx & PKT_DATA) >> DATA_SHIFT;
	calcCrcVal = calc_crc16(buf, tmp[1]);

	if (crcVal != calcCrcVal)
		goto tryStart;

	/* success! */
	return;
}


void H_WriteMemBuf(void *buf, u32 addr, int len) {
	printf("Writing %dB to 0x%08lx\n", len, addr);
}
