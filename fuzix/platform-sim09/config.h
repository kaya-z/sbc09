/* SPDX-License-Identifier: GPL-2.0-only */
/* Enable to make ^Z dump the inode table for debug */
#undef CONFIG_IDUMP
/* Enable to make ^A drop back into the monitor */
#undef CONFIG_MONITOR
/* Profil syscall support (not yet complete) */
#undef CONFIG_PROFIL
/* Multiple processes in memory at once */
#undef CONFIG_MULTI
/* Pure swap: one process in memory, the others are on the swap partition */
#define CONFIG_SWAP_ONLY
#define CONFIG_SPLIT_UDATA
#define UDATA_BLKS 1
#define CONFIG_USERMEM_DIRECT

#define CONFIG_LEVEL_0		/* Minimal system */

#define CONFIG_BANKS	1
/* And swapping: the swap partition is found by its type in the partition table */
#define CONFIG_DYNAMIC_SWAP

extern uint16_t swap_dev;

#define SWAPDEV     swap_dev    /* From partiton */
#define SWAP_SIZE   0x3B	/* 512 byte blocks: 58 for 6C00-DFFF and the udata block */
#define SWAPBASE    0x6C00	/* We swap the lot, including stashed uarea */
#define SWAPTOP     0xE000	/* so it's a round number of 512 byte sectors */
#define UDATA_SIZE  0x0200	/* one block */
#define MAX_SWAPS   32

#define PTABSIZE	8

/* Permit large I/O requests to bypass cache and go direct to userspace */
#define CONFIG_LARGE_IO_DIRECT(x)	1

/* Allow for our swap heavy nature */
#define MAXTICKS 25
#define CONFIG_PARENT_FIRST	/* For pure swap this is far faster */

/* Serial console on the ACIA, no video terminal */

#define TICKSPERSEC 50   /* Ticks per second: the timer of the simulator runs at 50Hz */
#define PROGBASE    0x6C00  /* also data base */
#define PROGLOAD    0x6C00  /* also data base */
#define PROGTOP     0xE000  /* Top of program: the I/O page starts at E000 */

/* The root file system is the first partition of the first disk */
#define BOOTDEVICENAMES "hd#"
#define CMDLINE	"hda1"	  /* Location of root dev name */

/* Device parameters */
#define NUM_DEV_TTY 1
#define TTYDEV   513	 /* Device used by kernel for messages, panics */
#define NBUFS    4       /* Number of block buffers */
#define NMOUNTS	 2	  /* Number of mounts at a time */
#define swap_map(x)	((uint8_t *)(x))

#define CONFIG_TD_NUM	2

#define plt_copyright(x)

/* /dev/rtc: the clock of the simulator */
#define CONFIG_RTC_FULL

#define CONFIG_SMALL
