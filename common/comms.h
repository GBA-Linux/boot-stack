/*
 * GBA Linux Loader - Common - GBA <--> Host Communications
 *
 * Copyright (C) 2025 Techflash
 */
#ifndef _COMMS_H
#define _COMMS_H

#if defined(HW_RVL) || defined(HW_DOL)
#define ntohl(x) (x)
#define htonl(x) (x)
/* SI channel to look for the GBA on, 0-indexed */
#define GBA_CHAN (1)

extern void C_Process(void);
#else
#define htonl(x) (__builtin_bswap32(x))
#define ntohl(x) (__builtin_bswap32(x))
#endif /* HW_AGB */

#if 0
/* even parity in bit 0 */
static inline u32 parity(u32 msg) {
	msg &= ~1;
	if ((msg >> 1) % 2)
		msg |= 1;
	return msg;
}

/* check for even parity */ 
#define parityValid(x) ((x % 2) == 0)
#endif

/* class stuff in bits 0-2 */
#define CLASS_SHIFT     (0)
#define PKT_CLASS       (7 << CLASS_SHIFT)
#define MKCLASS(x)      ((x << CLASS_SHIFT) & PKT_CLASS)
#define CLASS_SYS       MKCLASS(0)
#define CLASS_MEM       MKCLASS(1)

/* subcmd stuff in bits 3-5 */
#define SUBCMD_SHIFT    (3)
#define PKT_SUBCMD      (7 << SUBCMD_SHIFT)
#define MKSUBCMD(x)     ((x << SUBCMD_SHIFT) & PKT_SUBCMD)

#define SYS_ACK         MKSUBCMD(1) /* SYS stuff starts at subcmd 1 to avoid confusion with class=0 subcmd=0 */
#define SYS_MW_TX_DONE  MKSUBCMD(2)
#define SYS_PING        MKSUBCMD(3)
#define SYS_PING_REPLY  MKSUBCMD(4)
#define SYS_KERNEL_LOAD MKSUBCMD(5)

#define MEM_READ        MKSUBCMD(0)
#define MEM_WRITE       MKSUBCMD(1)

/* cmd id stuff in bits 6-7 */
#define CMD_ID_SHIFT    (6)
#define PKT_CMD_ID      (3 << CMD_ID_SHIFT)

#if 0
/* fmt stuff in bit 8 */
#define FMT_SHIFT       (8)
#define PKT_FMT         (1 << FMT_SHIFT)
#define MKFMT(x)        ((x << FMT_SHIFT) & PKT_FMT)
#define FMT_IMM         MKFMT(0)
#define FMT_NUMWORD     MKFMT(1)
#endif

/* data stuff in bit 8 - 23 */
#define DATA_SHIFT      (8)
#define PKT_DATA        (65535 << DATA_SHIFT)

#define CRC8_SHIFT      (24)
#define PKT_CRC8        (255 << CRC8_SHIFT)

/* Tableless CRC-8 (polynomial 0x07), initial 0x00 */
static inline u8 calc_crc8(const u8 *data, int len) {
	int i, j;
	u8 crc = 0;
	for (i = 0; i < len; ++i) {
		crc ^= data[i];
		for (j = 0; j < 8; ++j) {
			crc = (crc & 0x80) ?
				(u8)((crc << 1) ^ 0x07) :
				(u8)(crc << 1);
		}
	}
	return crc;
}

/* CRC-16 CCITT (polynomial 0x1021), initial 0xffff */
static inline u16 calc_crc16(const u8 *data, int len) {
	u16 crc = 0xffff;
	int i;
	while (len--) {
		crc ^= ((u16)*data++) << 8;
		for (i = 0; i < 8; ++i) {
			if (crc & 0x8000)
				crc = (crc << 1) ^ 0x1021;
			else
				crc <<= 1;
		}
	}
	return crc;
}

static inline u32 crc(u32 msg) {
	u8 crc;
	u32 tmpMsg;

	tmpMsg = msg & ~PKT_CRC8;
	/* enforce BE order so that calc_crc8 doesn't explode */
	tmpMsg = htonl(tmpMsg);

	crc = calc_crc8((u8 *)&tmpMsg, 3); /* CRC-8 over the first 24 bits */
	msg |= (crc << CRC8_SHIFT);

	return msg;
}

static inline bool crcValid(u32 msg) {
	u8 calcCrc, readCrc;
	u32 tmpMsg;

	readCrc = (msg & PKT_CRC8) >> CRC8_SHIFT;
	tmpMsg = msg & ~PKT_CRC8; /* mask out the read CRC, just in case */
	/* enforce BE order so that calc_crc8 doesn't explode */
	tmpMsg = htonl(tmpMsg);

	calcCrc = calc_crc8((u8 *)&tmpMsg, 3); /* CRC-8 over the first 24 bits */

#if 0
	if (calcCrc != readCrc)
		printf("crc invalid: 0x%02X != 0x%02X\n", calcCrc, readCrc);
#endif
	return calcCrc == readCrc;
}



#endif /* _COMMS_H */
