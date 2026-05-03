/*
 * (C) Copyright 2003
 * Kyle Harris, kharris@nexus-tech.net
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
 */

#include <common.h>
#include <command.h>
#include <malloc.h>
#include <mmc.h>

#define RSA2048_SIZE (2048 >> 3)
static int curr_device = -1;
static char mmc_part[8][7] = {"user", "boot-0", "boot-1", "rpmb", "gp-0", "gp-1", "gp-2", "gp-3"};

#ifndef CONFIG_GENERIC_MMC
int do_mmc (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	int dev;

	if (argc < 2)
		return CMD_RET_USAGE;

	if (strcmp(argv[1], "init") == 0) {
		if (argc == 2) {
			if (curr_device < 0)
				dev = 1;
			else
				dev = curr_device;
		} else if (argc == 3) {
			dev = (int)simple_strtoul(argv[2], NULL, 10);
		} else {
			return CMD_RET_USAGE;
		}

		if (mmc_legacy_init(dev) != 0) {
			puts("No MMC card found\n");
			return 1;
		}

		curr_device = dev;
		printf("mmc%d is available\n", curr_device);
	} else if (strcmp(argv[1], "device") == 0) {
		if (argc == 2) {
			if (curr_device < 0) {
				puts("No MMC device available\n");
				return 1;
			}
		} else if (argc == 3) {
			dev = (int)simple_strtoul(argv[2], NULL, 10);

#ifdef CONFIG_SYS_MMC_SET_DEV
			if (mmc_set_dev(dev) != 0)
				return 1;
#endif
			curr_device = dev;
		} else {
			return CMD_RET_USAGE;
		}

		printf("mmc%d is current device\n", curr_device);
	} else {
		return CMD_RET_USAGE;
	}

	return 0;
}

U_BOOT_CMD(
	mmc, 3, 1, do_mmc,
	"MMC sub-system",
	"init [dev] - init MMC sub system\n"
	"mmc device [dev] - show or set current device"
);
#else /* !CONFIG_GENERIC_MMC */

enum mmc_state {
	MMC_INVALID,
	MMC_READ,
	MMC_WRITE,
	MMC_ERASE,
	MMC_READ_NO_WAIT,
};
static void print_mmcinfo(struct mmc *mmc)
{
	printf("Device: %s\n", mmc->name);
	printf("Manufacturer ID: %x\n", mmc->cid[0] >> 24);
	printf("OEM: %x\n", (mmc->cid[0] >> 8) & 0xffff);
	printf("Name: %c%c%c%c%c \n", mmc->cid[0] & 0xff,
			(mmc->cid[1] >> 24), (mmc->cid[1] >> 16) & 0xff,
			(mmc->cid[1] >> 8) & 0xff, mmc->cid[1] & 0xff);

	printf("Tran Speed: %d\n", mmc->tran_speed);
	printf("Rd Block Len: %d\n", mmc->read_bl_len);

	printf("%s version %d.%d", IS_SD(mmc) ? "SD" : "MMC",
			EXTRACT_SDMMC_MAJOR_VERSION(mmc->version),
			EXTRACT_SDMMC_MINOR_VERSION(mmc->version));
	if(EXTRACT_SDMMC_CHANGE_VERSION(mmc->version) != 0)
		printf(".%d", EXTRACT_SDMMC_CHANGE_VERSION(mmc->version));
	printf("\n");

	printf("High Capacity: %s\n", mmc->high_capacity ? "Yes" : "No");
	puts("Capacity: ");
	print_size(mmc->capacity, "\n");

	printf("Bus Width: %d-bit\n", mmc->bus_width);
}

static int do_mmcinfo(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct mmc *mmc;

	if (curr_device < 0) {
		if (get_mmc_num() > 0)
			curr_device = 0;
		else {
			puts("No MMC device available\n");
			return 1;
		}
	}

	mmc = find_mmc_device(curr_device);

	if (mmc) {
		mmc_init(mmc);

		print_mmcinfo(mmc);
		return 0;
	} else {
		printf("no mmc device at slot %x\n", curr_device);
		return 1;
	}
}

U_BOOT_CMD(
	mmcinfo, 1, 0, do_mmcinfo,
	"display MMC info",
	"- display info of the current MMC device"
);

#ifdef CONFIG_VERINFO_IN_EEPROM

#define BOOT_FLG_OFFSET		0
#define BOOT_FLG_SIZE		1
#define VUP_CMP_FLAG_OFFSET	16
#define VUP_CMP_FLAG_SIZE	1
#define RESULT_FLAG_OFFSET	20
#define RESULT_FLAG_SIZE	4

#define INVALID_FLAG		0xFF
#define FLASH_NORMAL_A		0x6E
#define FLASH_VERUP1_A		0x61
#define FLASH_VERUP2_A		0x62
#define FLASH_VERUP3_A		0x63
#define FLASH_VERUP4_A		0x64
#define FLASH_NORMAL_B		0xAE
#define FLASH_VERUP1_B		0xA1
#define FLASH_VERUP2_B		0xA2
#define FLASH_VERUP3_B		0xA3
#define FLASH_VERUP4_B		0xA4

#define PART_GPP0			4
#define PART_GPP1			5

static int SelectPart = PART_GPP0;

static int get_boot_flag( void )
{
	printf( "Get SelectPart:(0x%x)\n", SelectPart );
	return SelectPart;
}

static void vClearDlInfomation( void )
{
	uchar ucWriteFlg[4] = { 0xFF, 0xFF, 0xFF, 0xFF };

	if( eeprom_write( CONFIG_SYS_DEF_EEPROM_ADDR, VUP_CMP_FLAG_OFFSET, (uchar *)&ucWriteFlg, VUP_CMP_FLAG_SIZE ) != 0 ) {
		printf( "VUP_CMP_FLAG write error\n" );
	}
	udelay(20 * 1000);

	if( eeprom_write( CONFIG_SYS_DEF_EEPROM_ADDR, RESULT_FLAG_OFFSET, (uchar *)&ucWriteFlg, RESULT_FLAG_SIZE ) != 0 ) {
		printf( "RESULT_FLAG write error\n" );
	}
	udelay(20 * 1000);
}

static int chk_boot_flag( void )
{
	int rcount;
	uchar ucBootFlg = INVALID_FLAG;
	uchar ucPreFlag;
	uchar ucWriteBootFlg = INVALID_FLAG;

	/* Read BootFlag */
	for ( rcount = 0; rcount < 3; rcount++ ) {
		if ( eeprom_read( CONFIG_SYS_DEF_EEPROM_ADDR, BOOT_FLG_OFFSET, (uchar *)&ucBootFlg, BOOT_FLG_SIZE ) != 0 ) {
			printf( "eeprom_read error\n" );
			break;
		}
		if ( rcount == 0 ) {
			/* First Time */
			ucPreFlag = ucBootFlg;
		}
		else {
			if ( ucPreFlag != ucBootFlg ) {
				printf( "eeprom_read illegal data pre(0x%x) cur(0x%x)\n", ucPreFlag, ucBootFlg );
				break;
			}
		}
	}

	printf( "BootFlag:(0x%x)\n", ucBootFlg );

	/* Check BootFlag */
	switch( ucBootFlg ) {
		case FLASH_NORMAL_A:	/* ÊíN®A */
			ucWriteBootFlg = INVALID_FLAG;
			SelectPart = PART_GPP0;
			break;
		case FLASH_NORMAL_B:	/* ÊíN®B */
			ucWriteBootFlg = INVALID_FLAG;
			SelectPart = PART_GPP1;
			break;
		case FLASH_VERUP1_A:	/* o[WAbvN®A 1 ñÚ */
		case FLASH_VERUP2_A:	/* o[WAbvN®A 2 ñÚ */
		case FLASH_VERUP3_A:	/* o[WAbvN®A 3 ñÚ */
			ucWriteBootFlg = ucBootFlg + 1;
			SelectPart = PART_GPP0;
			break;
		case FLASH_VERUP1_B:	/* o[WAbvN®B 1 ñÚ */
		case FLASH_VERUP2_B:	/* o[WAbvN®B 2 ñÚ */
		case FLASH_VERUP3_B:	/* o[WAbvN®B 3 ñÚ */
			ucWriteBootFlg = ucBootFlg + 1;
			SelectPart = PART_GPP1;
			break;
		case FLASH_VERUP4_A:	/* G[N®B */
			ucWriteBootFlg = FLASH_VERUP1_B;
			SelectPart = PART_GPP1;
			vClearDlInfomation();
			break;
		case FLASH_VERUP4_B:	/* G[N®A */
			ucWriteBootFlg = FLASH_VERUP1_A;
			SelectPart = PART_GPP0;
			vClearDlInfomation();
			break;
		default:				/* INVALID_FLAG -- Others */
			ucWriteBootFlg = FLASH_VERUP2_A;
			SelectPart = PART_GPP0;
			break;
	}

	/* Change BootFlag */
	if( ucWriteBootFlg != INVALID_FLAG ) {
		printf( "WriteBootFlg:(0x%x)\n", ucWriteBootFlg );
		/* Write BootFlag */
		if( eeprom_write( CONFIG_SYS_DEF_EEPROM_ADDR, BOOT_FLG_OFFSET, &ucWriteBootFlg, BOOT_FLG_SIZE ) != 0 ) {
			printf( "eeprom_write error\n" );
		}
	}

	printf( "SelectPart:(0x%x)\n", SelectPart );
    if (SelectPart == PART_GPP0) {
        setenv("rootdev", "/dev/mmcblk0gp0");
    } else {
        setenv("rootdev", "/dev/mmcblk0gp1");
    }

	return SelectPart;

}
#endif

static int emmc_load_image(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct mmc *mmc;
	char *boot_device = NULL;
	char *boot_part = NULL;
	int idx, part_num;
	ulong addr, n;
    ulong offset = 0;
	ulong blkcnt;
	size_t cnt;
	char *s, str[32];
	image_header_t *hdr = NULL;
	void* buff =NULL;

	bootstage_mark(BOOTSTAGE_ID_NAND_PART);
	switch (argc) {
	case 1:
		addr = CONFIG_SYS_LOAD_ADDR;
		boot_device = getenv("bootdevice");
		boot_part = "4";
		break;
	case 2:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = getenv("bootdevice");
		boot_part = "4";
		break;
	case 3:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		boot_part = "4";
		break;
	case 4:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		boot_part = argv[3];
		break;
	case 5:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		boot_part = argv[3];
        offset = simple_strtoul(argv[4], NULL, 16);
        if (offset & (MMC_MAX_BLOCK_LEN -1)) {
            printf("** start offset 0x%x is not 512 byte aligned\n", offset);
            bootstage_error(BOOTSTAGE_ID_NAND_PART);
            return 1;
        }
        offset = offset / 512;
		break;
	default:
		bootstage_error(BOOTSTAGE_ID_NAND_SUFFIX);
		return CMD_RET_USAGE;
	}

	if(addr & (CONFIG_SYS_CACHELINE_SIZE-1)) {
		printf("** destination address 0x%x is not cache aligned\n", addr);
		bootstage_error(BOOTSTAGE_ID_NAND_PART);
		return 1;
	}

	if (!boot_device) {
		puts("\n** No boot device **\n");
		bootstage_error(BOOTSTAGE_ID_NAND_BOOT_DEVICE);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_BOOT_DEVICE);

	idx = simple_strtoul(boot_device, NULL, 16);

	mmc = find_mmc_device(idx);
	if (!mmc) {
		printf("\n** Device %d not available\n", idx);
		bootstage_error(BOOTSTAGE_ID_NAND_AVAILABLE);
		return 1;
	}

	mmc_init(mmc);

	part_num = simple_strtoul(boot_part, NULL, 16);

#ifdef CONFIG_VERINFO_IN_EEPROM
	part_num = chk_boot_flag();
#endif

	if (part_num != (mmc->part_config & PARTCONF_MASK)) {
		n = mmc_switch_part(idx, part_num);
		printf("mmc partition switch %s\n", (!n) ? "OK" : "ERROR");
		if(!n) {
			printf("switch partition access %s\n",mmc_part[mmc->part_num]);
		}
		else {
			printf("\n** switch partition access error\n");
			bootstage_error(BOOTSTAGE_ID_NAND_AVAILABLE);
			return 1;
		}
	}

	bootstage_mark(BOOTSTAGE_ID_NAND_AVAILABLE);

    printf("\nLoad uImage from %s: %d, partition %s blk start %d to 0x%lx\n", 
            mmc->name, idx, mmc_part[part_num], offset, addr);
		
    /* read uImage header first */	
	flush_dcache_range((ulong)addr, (ulong)addr+ sizeof(image_header_t));
	blkcnt = 1;
	n = mmc->block_dev.block_read(idx, offset, blkcnt, addr);
	if ( n != blkcnt ) {
		puts("** Read image header error\n");
		bootstage_error(BOOTSTAGE_ID_NAND_HDR_READ);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_HDR_READ);

#ifdef CONFIG_SECURE_BOOT
    /* allocate image header buffer */
	buff = (void*)malloc(sizeof(image_header_t) + 64);	
	if(buff == NULL) {
		puts("** no memory\n");
		bootstage_error(BOOTSTAGE_ID_NAND_HDR_READ);
		return 1;
	}
	/* Align the header to cahce */
	hdr = ALIGN_CACHE_SIZE((ulong)buff);
	debug("eMMC load secure image header to 0x%p\n", hdr);	

#ifdef CONFIG_MMC_ENC
    /* MMC encryption support does the decryption in mmc driver */
    memcpy(hdr, addr, sizeof(image_header_t));
    printf("eMMC load secure image header to 0x%p\n", hdr);

    if (!image_check_hcrc(hdr)) {
        printf("Bad Header CRC\n");
        bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
        return 1;
    }
    cnt = image_get_image_size(hdr);

    if (image_check_type(hdr, IH_TYPE_FILESYSTEM)) {
        /* 
         * squashfs file system start from offset 0 and
         * does not inculde the image header 
         */
        offset = 0;
        cnt -= sizeof(image_header_t);
    }
    
    /* align the size to 16 byte block */    
    if (cnt % 16 )
        cnt = (cnt & ~0xF) + 16;

    /* read size include the RSA-2048 signature */    
    cnt += RSA2048_SIZE;
    printf("[%s] before padding mmc load size %d at %d\n", __func__, cnt, offset);

    /* read data from flash */
    blkcnt = (cnt + mmc->read_bl_len - 1) / mmc->read_bl_len;   
    flush_dcache_range((ulong)addr, (ulong)addr + blkcnt * mmc->read_bl_len);
    n = mmc->block_dev.block_read(idx, offset, blkcnt, addr);
    if ( n != blkcnt ) {
        printf("** request %d blocks but read %d blocks\n", blkcnt, n);
        bootstage_error(BOOTSTAGE_ID_NAND_READ);
        return 1;
    }
    invalidate_dcache_range((ulong)addr, (ulong)addr + blkcnt * mmc->read_bl_len);

    /* validate the image */
    if(hdr->ih_type == IH_TYPE_FILESYSTEM) {
        printf("validate the squasfs image ...\n");
        if(secure_boot_verify(addr, cnt - RSA2048_SIZE)) {			
            printf("failed\n");
            bootstage_error(BOOTSTAGE_ID_CHECK_CHECKSUM);
            return 1;
        }
        printf("passed.\n");
    }
#else
    /* map src and dst to dma engine and decrypt the uImage header */
	flush_dcache_range((ulong)hdr, (ulong)hdr + sizeof(image_header_t));
	if (decrypt_image((u8*)addr, (u8*)hdr, sizeof(image_header_t), CONFIG_AES_CW_SLOT, CONFIG_AES_IV_SLOT)) {
		bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
		return 1;
	}
	invalidate_dcache_range((ulong)hdr, (ulong)hdr + sizeof(image_header_t));

	if (!image_check_hcrc(hdr)) {
		printf("Bad Header CRC\n");
		bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
		return 1;
	}

	cnt = image_get_image_size(hdr);
	/* Image padding */
	debug("before padding image size 0x%x\n", cnt);
	cnt = (cnt%16)?(((cnt>>4)<<4)+80):(cnt+64);
    cnt += RSA2048_SIZE;
	debug("secure image size 0x%x\n", cnt);		

	blkcnt = (cnt + mmc->read_bl_len - 1) / mmc->read_bl_len;

	flush_dcache_range((ulong)addr, (ulong)addr + blkcnt * mmc->read_bl_len);
	n = mmc->block_dev.block_read(idx, offset, blkcnt, addr);
	if ( n != blkcnt ) {
		printf("** request %d blocks but read %d blocks\n", blkcnt, n);
		bootstage_error(BOOTSTAGE_ID_NAND_READ);
		return 1;
	}
	invalidate_dcache_range((ulong)addr, (ulong)addr + blkcnt * mmc->read_bl_len);

	if(hdr->ih_type == IH_TYPE_FILESYSTEM) {
		printf("decrypt and validate the initrd image ...\n");
		cnt = secure_boot_decrypt(hdr, addr);
		if(secure_boot_verify(addr, cnt)) {			
			printf("failed\n");
			bootstage_error(BOOTSTAGE_ID_CHECK_CHECKSUM);
			return 1;
		}
		printf("passed.\n");
	}
#endif

#else
	if (!image_check_hcrc((image_header_t *)addr)) {
		printf("Bad Header CRC\n");
		bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
		return 1;
	}

	switch (genimg_get_format ((void *)addr))
	{	
	case IMAGE_FORMAT_LEGACY:
		hdr = (image_header_t *)addr;
		bootstage_mark(BOOTSTAGE_ID_CHECK_IMAGETYPE);
		cnt = image_get_image_size(hdr);
		break;

	default:
		bootstage_error(BOOTSTAGE_ID_NAND_TYPE);
		puts ("** Unknown image type\n");
		return 1;
	}	

	blkcnt = (cnt + mmc->read_bl_len - 1) / mmc->read_bl_len;

	flush_dcache_range((ulong)addr, (ulong)addr + blkcnt * mmc->read_bl_len);
	n = mmc->block_dev.block_read(idx, offset, blkcnt, addr);
	if ( n != blkcnt ) {
		printf("** request %d blocks but read %d blocks\n", blkcnt, n);
		bootstage_error(BOOTSTAGE_ID_NAND_READ);
		return 1;
	}
	invalidate_dcache_range((ulong)addr, (ulong)addr + blkcnt * mmc->read_bl_len);

	if(hdr->ih_type == IH_TYPE_FILESYSTEM) {
		printf("Check the initrd image...");
		if(!image_check_dcrc((image_header_t *)addr)) {
			printf("Bad data CRC! \n");
			bootstage_error(BOOTSTAGE_ID_CHECK_CHECKSUM);
			return 1;
		}
		printf("OK.\n");
	}
#endif

	bootstage_mark(BOOTSTAGE_ID_KERNEL_LOADED);

	if(hdr->ih_type == IH_TYPE_FILESYSTEM) {
		bootstage_mark(BOOTSTAGE_ID_RAMDISK);			
		sprintf(str, "%d", ntohl(hdr->ih_size));
		setenv("initrd_size", str);
		sprintf(str, "0x%x", ntohl(hdr->ih_load) + sizeof(image_header_t));
		setenv("initrd_addr", str);
		debug("initrd address %s, size %s\n", getenv("initrd_addr"), getenv("initrd_size"));
	}

	image_print_contents (hdr);
	if(buff);
		free(buff);

	/* Loading ok, update default load address */
	load_addr = addr;
	return 0;
}

U_BOOT_CMD(mmcboot, 5, 0, emmc_load_image,
	"boot uImage from eMMC device",
	"loadaddr dev partition offset, it is equivalent to command mmc boot\n"
);

/*
 * SQUASHFS located at 16M offset, shared same GP partition with kernel image  
 */
#define SQUASHFS_OFFSET   (16*1024*2)     // 16*1024*1024/512

static int emmc_load_image_nw(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	struct mmc *mmc;
	char *boot_device = NULL;
	char *boot_part = NULL;
	int idx, part_num;
	ulong addr, n;
	ulong blkcnt;
	size_t cnt;
	char *s, str[32];
	image_header_t *hdr = NULL;
	void* buff =NULL;
    ulong offset = SQUASHFS_OFFSET;

	bootstage_mark(BOOTSTAGE_ID_NAND_PART);
	switch (argc) {
	case 1:
		addr = CONFIG_SYS_LOAD_ADDR;
		boot_device = getenv("bootdevice");
		boot_part = "4";
		break;
	case 2:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = getenv("bootdevice");
		boot_part = "4";
		break;
	case 3:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		boot_part = "4";
		break;
	case 4:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		boot_part = argv[3];
		break;
	case 5:
		addr = simple_strtoul(argv[1], NULL, 16);
		boot_device = argv[2];
		boot_part = argv[3];
        offset = simple_strtoul(argv[4], NULL, 16);
        if (offset & (MMC_MAX_BLOCK_LEN -1)) {
            printf("** start offset 0x%x is not 512 byte aligned\n", offset);
            bootstage_error(BOOTSTAGE_ID_NAND_PART);
            return 1;
        }
        offset = offset / 512;  // block offset
		break;        
	default:
		bootstage_error(BOOTSTAGE_ID_NAND_SUFFIX);
		return CMD_RET_USAGE;
	}

	if(addr & (CONFIG_SYS_CACHELINE_SIZE-1)) {
		printf("** destination address 0x%x is not cache aligned\n", addr);
		bootstage_error(BOOTSTAGE_ID_NAND_PART);
		return 1;
	}

	if (!boot_device) {
		puts("\n** No boot device **\n");
		bootstage_error(BOOTSTAGE_ID_NAND_BOOT_DEVICE);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_BOOT_DEVICE);

	idx = simple_strtoul(boot_device, NULL, 16);

	mmc = find_mmc_device(idx);
	if (!mmc) {
		printf("\n** Device %d not available\n", idx);
		bootstage_error(BOOTSTAGE_ID_NAND_AVAILABLE);
		return 1;
	}

	mmc_init(mmc);

	part_num = simple_strtoul(boot_part, NULL, 16);

#ifdef CONFIG_VERINFO_IN_EEPROM
	part_num = get_boot_flag();
#endif

	if (part_num != (mmc->part_config & PARTCONF_MASK)) {
		n = mmc_switch_part(idx, part_num);
		printf("mmc partition switch %s\n", (!n) ? "OK" : "ERROR");
		if(!n) {
			printf("switch partition access %s\n",mmc_part[mmc->part_num]);
		}
		else {
			printf("\n** switch partition access error\n");
			bootstage_error(BOOTSTAGE_ID_NAND_AVAILABLE);
			return 1;
		}
	}

	bootstage_mark(BOOTSTAGE_ID_NAND_AVAILABLE);

	printf("\nLoad uImage from %s: %d, partition %s blk start %d to 0x%lx\n", mmc->name, idx, mmc_part[part_num], offset, addr);
		
	invalidate_dcache_range((ulong)addr, (ulong)addr+ sizeof(image_header_t));
	blkcnt = 1;
	n = mmc->block_dev.block_read(idx, offset, blkcnt, addr);
	if ( n != blkcnt ) {
		puts("** Read image header error\n");
		bootstage_error(BOOTSTAGE_ID_NAND_HDR_READ);
		return 1;
	}
	bootstage_mark(BOOTSTAGE_ID_NAND_HDR_READ);

#ifdef CONFIG_SECURE_BOOT
	/* allocate header buffer */
	buff = (void*)malloc(sizeof(image_header_t) + 64);	
	if(buff == NULL) {
		puts("** no memory\n");
		bootstage_error(BOOTSTAGE_ID_NAND_HDR_READ);
		return 1;
	}
	/* Align the header to cahce */
	hdr = ALIGN_CACHE_SIZE((ulong)buff);
	debug("eMMC load secure image header to 0x%p\n", hdr);	

	/* map src adn dst to dma engine */
	flush_dcache_range((ulong)hdr, (ulong)hdr + sizeof(image_header_t));
	if (decrypt_image((u8*)addr, (u8*)hdr, sizeof(image_header_t), CONFIG_AES_CW_SLOT, CONFIG_AES_IV_SLOT)) {
		bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
		return 1;
	}

	invalidate_dcache_range((ulong)hdr, (ulong)hdr + sizeof(image_header_t));
	if (!image_check_hcrc(hdr)) {
		printf("Bad Header CRC\n");
		bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
		return 1;
	}

	cnt = image_get_image_size(hdr);
	debug("before padding image size 0x%x\n", cnt);
	/* Image padding */
	cnt = (cnt%16)?(((cnt>>4)<<4)+80):(cnt+64);
	/* Signature */
	cnt += 256;
	debug("secure image size 0x%x\n", cnt);		

#else
	if (!image_check_hcrc((image_header_t *)addr)) {
		printf("Bad Header CRC\n");
		bootstage_error(BOOTSTAGE_ID_CHECK_HEADER);
		return 1;
	}
	switch (genimg_get_format ((void *)addr))
	{	
	case IMAGE_FORMAT_LEGACY:
		bootstage_mark(BOOTSTAGE_ID_CHECK_IMAGETYPE);
		hdr = (image_header_t *)addr;
		cnt = image_get_image_size(hdr);
		break;

	default:
		bootstage_error(BOOTSTAGE_ID_NAND_TYPE);
		puts ("** Unknown image type\n");
		return 1;
	}	
#endif

	blkcnt = (cnt + mmc->read_bl_len - 1) / mmc->read_bl_len;
	n = mmc->block_dev.block_read_no_wait(idx, offset+1, blkcnt-1, addr+(512));
	if ( n != (blkcnt-1) ) {
		printf("** request %d blocks but read %d blocks\n", blkcnt, n);
		bootstage_error(BOOTSTAGE_ID_NAND_READ);
		return 1;
	}

	if(hdr->ih_type == IH_TYPE_FILESYSTEM) {
		bootstage_mark(BOOTSTAGE_ID_RAMDISK);			
		sprintf(str, "%d", ntohl(hdr->ih_size));
		setenv("initrd_size", str);
		sprintf(str, "0x%x", addr + sizeof(image_header_t));
		setenv("initrd_addr", str);
		debug("initrd address %s, size %s\n", getenv("initrd_addr"), getenv("initrd_size"));
	}

	image_print_contents(hdr);
	if(buff)
		free(buff);

	load_addr = addr;
	return 0;
}

U_BOOT_CMD(mmcbootnw, 5, 0, emmc_load_image_nw,
	"load rfs img without wait from eMMC device",
	"loadaddr dev partition offset, it will update env initrd_addr and initrd_size\n"
);

static int do_mmcops(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	enum mmc_state state;

	if (argc < 2)
		return CMD_RET_USAGE;

	if (curr_device < 0) {
		if (get_mmc_num() > 0)
			curr_device = 0;
		else {
			puts("No MMC device available\n");
			return 1;
		}
	}

	if (strcmp(argv[1], "rescan") == 0) {
		struct mmc *mmc = find_mmc_device(curr_device);

		if (!mmc) {
			printf("no mmc device at slot %x\n", curr_device);
			return 1;
		}

		mmc->has_init = 0;

		if (mmc_init(mmc))
			return 1;
		else
			return 0;
	} else if (strncmp(argv[1], "partconf", 8) == 0) {		
		if (argc < 3) {
			printf("Invalid command arguments for partconf\n");
			return CMD_RET_USAGE;
		}

		int dev, config, ret;
		struct mmc *mmc;
		s8 boot_part = -1, part_access = -1;
		
		dev = simple_strtoul(argv[2], NULL, 10);
		if (argc > 3)
			boot_part = (u8)simple_strtoul(argv[3], NULL, 10);
		if (argc > 4)
			part_access = (u8)simple_strtoul(argv[4], NULL, 10);

		mmc = find_mmc_device(dev);
		if(!mmc) {
			printf("no mmc device in slot %d!\n", dev);
			return CMD_RET_FAILURE;
		}

		mmc_init(mmc);

		if(IS_SD(mmc)) {
			printf("Only eMMC flash support partconf command\n");
			return CMD_RET_FAILURE;
		}
		curr_device = dev;

		if (argc == 3) {
			printf("current device %d, boot partition: %s, access partition: %s\n", \
				dev, mmc_part[(mmc->part_config & BOOT_PART_MASK)>> 3], mmc_part[mmc->part_config & PART_ACCESS_MASK] );
			return CMD_RET_SUCCESS;
		}

		if ((boot_part > 2) || (part_access > 7)) {
			printf("Request partition is out of spec!\n");
			return CMD_RET_FAILURE;
		}
#if 0
		if ((dev != 0)&&(boot_part != 0)) {
			printf("only mmc dev 0 is available for boot!\n");
			return CMD_RET_FAILURE;
		}
#endif
		config = boot_part << 3;
		if(part_access >= 0)
			config |= part_access;
		else
			config |= mmc->part_config & PART_ACCESS_MASK;

		if (config != (mmc->part_config & PARTCONF_MASK)) {
			ret = mmc_switch_part(dev, config);
			printf("mmc partition switch %s\n", (!ret) ? "OK" : "ERROR");
			if(!ret) {
				printf("switch boot partition: %s, partition access %s\n", mmc_part[boot_part], mmc_part[mmc->part_num]);
			}
		} 		
		return CMD_RET_SUCCESS;
	} else if (strncmp(argv[1], "part", 4) == 0) {
		/* check for typo */
		if(strlen(argv[1]) > 4)
			return CMD_RET_USAGE;

		block_dev_desc_t *mmc_dev;
		struct mmc *mmc = find_mmc_device(curr_device);

		if (!mmc) {
			printf("no mmc device at slot %x\n", curr_device);
			return 1;
		}
		mmc_init(mmc);
		mmc_dev = mmc_get_dev(curr_device);
		if (mmc_dev != NULL &&
				mmc_dev->type != DEV_TYPE_UNKNOWN) {
			print_part(mmc_dev);
			return 0;
		}

		puts("get mmc type error!\n");
		return 1;
	} else if (strcmp(argv[1], "list") == 0) {
		print_mmc_devices('\n');
		return 0;
	} else if (strcmp(argv[1], "dev") == 0) {
		int dev, part = -1;
		struct mmc *mmc;

		if (argc == 2)
			dev = curr_device;
		else if (argc == 3)
			dev = simple_strtoul(argv[2], NULL, 10);
		else if (argc == 4) {
			dev = (int)simple_strtoul(argv[2], NULL, 10);
			part = (int)simple_strtoul(argv[3], NULL, 10);
			if (part > PART_ACCESS_MASK) {
				printf("#part_num shouldn't be larger"
					" than %d\n", PART_ACCESS_MASK);
				return 1;
			}
		} else
			return CMD_RET_USAGE;

		mmc = find_mmc_device(dev);
		if (!mmc) {
			printf("no mmc device at slot %x\n", dev);
			return 1;
		}

		mmc_init(mmc);
		if (part != -1) {
			int ret;
			if (mmc->part_config == MMCPART_NOAVAILABLE) {
				printf("Card doesn't support part_switch\n");
				return 1;
			}

			if (part != mmc->part_num) {
				ret = mmc_switch_part(dev, part | (mmc->part_config & BOOT_PART_MASK));
				if (!ret)
					mmc->part_num = part;

				printf("switch to partions #%d, %s\n",
						part, (!ret) ? "OK" : "ERROR");
			}
		}
		curr_device = dev;
		if (mmc->part_config == MMCPART_NOAVAILABLE)
			printf("mmc%d is current device\n", curr_device);
		else
			printf("mmc%d(part %d) is current device\n",
				curr_device, mmc->part_num);

		return 0;
	} else if (strcmp(argv[1], "boot") == 0) {
		int ret;
		char *argv_tmp[4];
		argv_tmp[0] = argv[1]; /* cmd */
		argv_tmp[1] = argv[2]; /* addr */
		argv_tmp[2] = argv[3]; /* bootdevice */
		argv_tmp[3] = argv[4]; /* bootpart */
		ret = emmc_load_image(cmdtp, flag, 4, argv_tmp);
		return ret;
	}

	state = MMC_INVALID;
	if (argc == 5 && strcmp(argv[1], "read") == 0)
		state = MMC_READ;
	else if (argc == 5 && strcmp(argv[1], "write") == 0)
		state = MMC_WRITE;
	else if (argc == 4 && strcmp(argv[1], "erase") == 0)
		state = MMC_ERASE;
	else if (argc == 5 && strcmp(argv[1], "readnw") == 0)
		state = MMC_READ_NO_WAIT;

	if (state != MMC_INVALID) {
		struct mmc *mmc = find_mmc_device(curr_device);
		int idx = 2;
		u32 blk, cnt, n;
		void *addr;

		if (state != MMC_ERASE) {
			addr = (void *)simple_strtoul(argv[idx], NULL, 16);
			++idx;
		} else
			addr = NULL;
		blk = simple_strtoul(argv[idx], NULL, 16);
		cnt = simple_strtoul(argv[idx + 1], NULL, 16);

		if (!mmc) {
			printf("no mmc device at slot %x\n", curr_device);
			return 1;
		}

		printf("\nMMC %s: dev # %d, block # %d, count %d ... ",
				argv[1], curr_device, blk, cnt);

		mmc_init(mmc);

		switch (state) {
		case MMC_READ:
			n = mmc->block_dev.block_read(curr_device, blk,
						      cnt, addr);
			/* flush cache after read */
			flush_cache((ulong)addr, cnt * 512); /* FIXME */
			break;
		case MMC_WRITE:
			n = mmc->block_dev.block_write(curr_device, blk,
						      cnt, addr);
			break;
		case MMC_ERASE:
			n = mmc->block_dev.block_erase(curr_device, blk, cnt);
			break;
		case MMC_READ_NO_WAIT:
			n = mmc->block_dev.block_read_no_wait(curr_device, blk,
						      cnt, addr);
						
			printf("read no wait\n");
			break;
		default:
			BUG();
		}

		printf("%d blocks %s: %s\n",
				n, argv[1], (n == cnt) ? "OK" : "ERROR");
		return (n == cnt) ? 0 : 1;
	}

	return CMD_RET_USAGE;
}

U_BOOT_CMD(
	mmc, 6, 1, do_mmcops,
	"MMC sub system",
	"read addr blk# cnt\n"
	"mmc readnw addr blk# cnt\n"
	"mmc write addr blk# cnt\n"
	"mmc erase blk# cnt\n"
	"mmc rescan\n"
	"mmc list - lists available devices\n"
	"mmc part - lists available partition on current mmc device\n"
	"mmc dev dev part - show or set current mmc device [partition]\n"
	"mmc partconf dev bootpart part_access\n"
	" - Change the bits of the EXT_CSD[179] PARTITION_CONFIF field of the specific device\n"
	"   only eMMC flash support the command, only mmc host 0 is bootable\n"
	"   dev: host 0 and 1 are available, when pass argument dev only, show current partition config\n"
	"   boot_part:   0 - boot from user partition;\n"
	"                1 - boot from boot part 1;\n"
	"                2 - boot from boot part 2;\n"
	"   part_access: 0 - access user partition;\n"
	"                1 - access boot part 1;\n"
	"                2 - access boot part 2;\n"
	"                3 - access RPMB part;\n"
	"                4,5,6,7 - access general part 0,1,2,3\n" 
	"mmc boot addr dev partition\n"
	" - Boot uImage from eMMC flash offset 0\n"
	"   addr:        destination address.\n"
	"   boot_dev:    boot device 0 or 1.\n"
	"   partition:   0 for user partiton, 4 - 7 general purpose partition.\n"
);
#endif
 
