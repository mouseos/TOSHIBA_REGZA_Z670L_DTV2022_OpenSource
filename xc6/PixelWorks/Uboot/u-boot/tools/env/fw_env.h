/*
 * (C) Copyright 2002-2008
 * Wolfgang Denk, DENX Software Engineering, wd@denx.de.
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

/* Pull in the current config to define the default environment */
#ifndef __ASSEMBLY__
#define __ASSEMBLY__ /* get only #defines from config.h */
#include <config.h>
#undef	__ASSEMBLY__
#else
#include <config.h>
#endif

/*
 * To build the utility with the static configuration
 * comment out the next line.
 * See included "fw_env.config" sample file
 * for notes on configuration.
 */
#define CONFIG_FILE     "/etc/fw_env.config"

#ifndef CONFIG_FILE
#define HAVE_REDUND /* For systems with 2 env sectors */
#define DEVICE1_NAME      "/dev/mtd1"
#define DEVICE2_NAME      "/dev/mtd2"
#define DEVICE1_OFFSET    0x0000
#define ENV1_SIZE         0x4000
#define DEVICE1_ESIZE     0x4000
#define DEVICE1_ENVSECTORS     2
#define DEVICE2_OFFSET    0x0000
#define ENV2_SIZE         0x4000
#define DEVICE2_ESIZE     0x4000
#define DEVICE2_ENVSECTORS     2
#endif

#ifndef CONFIG_BAUDRATE
#define CONFIG_BAUDRATE		115200
#endif
#define XMK_STR(x)	#x
#define MK_STR(x)	XMK_STR(x)

/* For XC6400 SDK Box Multiboot configuration */
#ifdef	CONFIG_MULTIBOOT
	"multiboot=" MK_STR(CONFIG_MULTIBOOT)"\0"
#ifdef	CONFIG_BOOTFILE1
	"bootfile_1=" MK_STR (CONFIG_BOOTFILE1) "\0"
#endif
#ifdef	CONFIG_BOOTFILE2
	"bootfile_2=" MK_STR (CONFIG_BOOTFILE2) "\0"
#endif
#ifdef	CONFIG_BOOTFILE3
	"bootfile_3=" MK_STR (CONFIG_BOOTFILE3) "\0"
#endif
#ifdef	CONFIG_BOOTFILE4
	"bootfile_4=" MK_STR (CONFIG_BOOTFILE4) "\0"
#endif
#ifdef	CONFIG_BOOTFILE5
	"bootfile_5=" MK_STR (CONFIG_BOOTFILE5) "\0"
#endif
#ifdef CONFIG_BOOTNAME_1
			"bootname_1=" MK_STR(CONFIG_BOOTNAME_1) "\0"
#endif
#ifdef CONFIG_BOOTNAME_2
			"bootname_2=" MK_STR(CONFIG_BOOTNAME_2) "\0"
#endif
#ifdef CONFIG_BOOTNAME_3
			"bootname_3=" MK_STR(CONFIG_BOOTNAME_3) "\0"
#endif
#ifdef CONFIG_BOOTARGS1
	"bootargs_1=" MK_STR(CONFIG_BOOTARGS1) "\0"
#endif
#ifdef CONFIG_BOOTARGS2
	"bootargs_2=" MK_STR(CONFIG_BOOTARGS2) "\0"
#endif
#ifdef CONFIG_BOOTARGS3
	"bootargs_3=" MK_STR(CONFIG_BOOTARGS3) "\0"
#endif
#ifdef CONFIG_BOOTARGS4
	"bootargs_4=" MK_STR(CONFIG_BOOTARGS4) "\0"
#endif
#ifdef CONFIG_BOOTARGS5
	"bootargs_5=" MK_STR(CONFIG_BOOTARGS5) "\0"
#endif
#endif /* END CONFIG_MULTIBOOT */
#ifndef CONFIG_BOOTDELAY
#define CONFIG_BOOTDELAY	5	/* autoboot after 5 seconds	*/
#endif

#ifndef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND							\
	"bootp; "								\
	"setenv bootargs root=/dev/nfs nfsroot=${serverip}:${rootpath} "	\
	"ip=${ipaddr}:${serverip}:${gatewayip}:${netmask}:${hostname}::off; "	\
	"bootm"
#endif

extern int   fw_printenv(int argc, char *argv[]);
extern char *fw_getenv  (char *name);
extern int fw_setenv  (int argc, char *argv[]);
extern int fw_parse_script(char *fname);
extern int fw_env_open(void);
extern int fw_env_write(char *name, char *value);
extern int fw_env_close(void);

extern unsigned	long  crc32	 (unsigned long, const unsigned char *, unsigned);
