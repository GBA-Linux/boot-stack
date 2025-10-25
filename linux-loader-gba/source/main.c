/*
 * GBA Linux Loader - GBA Side - Main bootstrap/loop
 *
 * Copyright (C) 2025 Techflash
 */

#include <gba.h>
#include <stdio.h>
#include <stdlib.h>
#include "comms.h"

/* uc-rv32ima-gba entry */
extern void app_main(void);

int main(void) {
	u32 rx;

	irqInit();
	irqEnable(IRQ_SERIAL);

	consoleInit(	0,	// charbase
			4,	// mapbase
			0,	// background number
			NULL,	// font
			0, 	// font size
			15	// 16 color palette
	);

	// set the screen colors, color 0 is the background color
	// the foreground color is index 1 of the selected 16 color palette
	BG_COLORS[0] = RGB8(0, 0, 0);
	BG_COLORS[241] = RGB5(31, 31, 31);

	SetMode(MODE_0 | BG0_ON);

	iprintf("Hello World!\n");

	/* handle incoming ping */
	while (1) {
		while (!(REG_JSTAT & 0x2)) {
			printf("REG_JSTAT: 0x%04x\n", REG_JSTAT);
		}

		rx = REG_JOYRE;

		if (!crcValid(rx)) {
			//puts("crc invalid");
			continue;
		}

		if ((rx & PKT_CLASS) != CLASS_SYS ||
		   (rx & PKT_SUBCMD) != SYS_PING  ||
		   (rx & PKT_CMD_ID) != 0         ||
#if 0
		   (rx & PKT_FMT)    != FMT_IMM   ||
#endif
		   (rx & PKT_DATA)   != (0x4849 << DATA_SHIFT)) {
			printf("BS packet: 0x%08lX\n", rx);
			continue;
		}

		break;
	}
	puts("Got ping");

	/* send ACK */
	REG_JOYTR = crc(CLASS_SYS | SYS_ACK | 0 /* id */ | 0 /* data */);
	puts("Sent ACK");

	/* wait for host to read it */
	while (REG_JSTAT & 0x8);

	/* send ping reply */
	REG_JOYTR = crc(CLASS_SYS | SYS_PING_REPLY | 0 /* id */ | (0x4849 << DATA_SHIFT));
	puts("Sent ping reply");

	/* wait for host to read it */
	while (REG_JSTAT & 0x8);

	/* wait for incoming ping reply ACK */
	while (1) {
		while (!(REG_JSTAT & 0x2));
		rx = REG_JOYRE;

		if (!crcValid(rx))
			continue;

		if ((rx & PKT_CLASS) != CLASS_SYS ||
		   (rx & PKT_SUBCMD) != SYS_ACK   ||
		   (rx & PKT_CMD_ID) != 0         ||
#if 0
		   (rx & PKT_FMT)    != FMT_IMM   ||
#endif
		   (rx & PKT_DATA)   != 0)
			continue;

		break;
	}
	puts("Got ping reply ACK");


	/* uc-rv32ima-gba entry */
	app_main();

	__builtin_unreachable();
}


