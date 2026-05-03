/*
 * (C) Copyright 2011 - 2012 Samsung Electronics
 * EXT4 filesystem implementation in Uboot by
 * Uma Shankar <uma.shankar@samsung.com>
 * Manjunatha C Achar <a.manjunatha@samsung.com>

 * (C) Copyright 2004
 * esd gmbh <www.esd-electronics.com>
 * Reinhard Arlt <reinhard.arlt@esd-electronics.com>
 *
 * made from cmd_reiserfs by
 *
 * (C) Copyright 2003 - 2004
 * Sysgo Real-Time Solutions, AG <www.elinos.com>
 * Pavel Bartusek <pba@sysgo.com>
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 *
 */

/*
 * Ext2fs support
 */
#include <fs.h>


int do_ext2ls (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{     
	return do_ls(cmdtp, flag, argc, argv, FS_TYPE_EXT);
}

/******************************************************************************
 * Ext2fs boot command intepreter. Derived from diskboot
 */
int do_ext2load (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{       
	unsigned long addr;
	char *addr_str = NULL;
	char *filename = NULL;
	char *value = NULL;
	char *argvalue = "ethaddr=00:88:23:84:17:41 eth1addr=00:88:43:84:47:81 \0";
	const char*varname = "bootargs";
	unsigned long count, boot_mode=0, multi_boot=0;
	char *env_str = NULL;

	env_str = getenv("multiboot");
	if (env_str != NULL)
		multi_boot = simple_strtoul(env_str, NULL,16);
	debug ("-- Multi_boot Value: %x\n",(unsigned)multi_boot);

	if (multi_boot == 0) {
		debug("Non-multi-boot Mode\n");
		return do_load(cmdtp, flag, argc, argv, FS_TYPE_EXT, 16);
	} else {

		//#if (CONFIG_COMMANDS&CFG_CMD_I2C)	
		i2c_read(0x53, 0x00, 0x02, (uchar*)(&boot_mode), 0x04);
		//#endif
		boot_mode &= 0xff;
		printf("Multi-boot, Mode:%x\n",boot_mode);		
		switch (boot_mode) {
			case 0x0:
				filename = getenv("bootfile");
				value = "root=/dev/sda2\0";
				break;

			case 0x1:
				filename = getenv("bootfile_1");
				value = getenv("bootargs_1");			
				break;

			case 0x2:				
				filename = getenv("bootfile_2");
				value = getenv("bootargs_2");
				break;

			case 0x3:
				filename = getenv("bootfile_3");
				value = getenv("bootargs_3");
				break;

			case 0x4:
				filename = getenv("bootfile_4");
				value = getenv("bootargs_4");
				break;

			case 0x5:
				filename = getenv("bootfile_5");
				value = getenv("bootargs_5");
				break;

			case 0x6:
				filename = getenv("bootfile_6");
				break;

			case 0x7:
				filename = getenv("bootfile_7");
				break;

			case 0x8:
				filename = getenv("bootfile_8");
				break;

			case 0x9:
				filename = getenv("bootfile_9");
				break;

			case 0xa:
				filename = getenv("bootfile_10");
				break;

			case 0xb:
				filename = getenv("bootfile_11");
				break;

			default:
				printf("Using Default bootfile\n");
				filename = getenv("bootfile");
				break;	

		}
		printf ("Boot file: %s\n", filename);
		strcpy (argv[4],filename);
		printf ("argv4: %s\n", argv[4]);
		strcat (argvalue, value);                
		setenv (varname, argvalue);
		printf ("bootargs: %s\n", argvalue);

		return do_load(cmdtp, flag, argc, argv, FS_TYPE_EXT, 16);
	}
}

U_BOOT_CMD(
	ext2ls,	4,	1,	do_ext2ls,
	"list files in a directory (default /)",
	"<interface> <dev[:part]> [directory]\n"
	"    - list files from 'dev' on 'interface' in a 'directory'"
);

U_BOOT_CMD(
	ext2load,	6,	0,	do_ext2load,
	"load binary file from a Ext2 filesystem",
	"<interface> <dev[:part]> [addr] [filename] [bytes]\n"
	"    - load binary file 'filename' from 'dev' on 'interface'\n"
	"      to address 'addr' from ext2 filesystem.\n"
	"      All numeric parameters are assumed to be hex."
);
