#ifndef __CONFIG_XC6_SDK_H
#define __CONFIG_XC6_SDK_H
/* commands to include */
#include <config_cmd_default.h>

#define XMK_STR(x)	#x
#define MK_STR(x)	XMK_STR(x)
#define CFG_LOAD_ADDR (0x10000000)
#define CFG_SCRATCH_ADDR (0x100000)
/*
 * High Level Configuration Options
 */
/* Secure Boot */
//#define CONFIG_SECURE_BOOT		1
#ifdef CONFIG_SECURE_BOOT
#define CONFIG_SECURE_BOOT_MODE	2
#define CONFIG_AES_CW_SLOT		8
#define CONFIG_AES_IV_SLOT		10
#define CONFIG_AES_IV_DST_SLOT	12
//#define CONFIG_SW_SHA256
#endif


/* display */
#define CONFIG_VIDEO
#ifdef CONFIG_VIDEO
#define CONFIG_VIDEO_BMP_GZIP
#define CONFIG_VIXS_DISP
#define CONFIG_VIXS_DISP_FB_ADDR (0x3f000000)
#define CONFIG_CMD_BMP
#define CONFIG_SYS_VIDEO_LOGO_MAX_SIZE (0x800000)
#endif

/* USB */
//#define CONFIG_MUSB_UDC			1

/* USB device configuration */
//#define CONFIG_USB_DEVICE		1
//#define CONFIG_USB_TTY			1

/* USB UHH support options */
#define CONFIG_CMD_USB
#define CONFIG_USB_HOST
#define CONFIG_USB_EHCI
#define CONFIG_USB_EHCI_SDUH
#define CONFIG_USB_STORAGE
#define CONFIG_SYS_USB_EHCI_MAX_ROOT_PORTS 2

/* USB Networking options */
//#define CONFIG_USB_HOST_ETHER
//#define CONFIG_USB_ETHER_SMSC95XX

#define CONFIG_UBOOT_ENABLE_PADS_ALL

#define CONFIG_CMD_PING
#define CONFIG_CMD_DHCP

//#define CONFIG_USB_ULPI
//#define CONFIG_USB_ULPI_VIEWPORT_OMAP

#define CONFIG_DOS_PARTITION		1

/* SCSI */
#define CONFIG_SCSI_AHCI_XCODE
#define CONFIG_SCSI_AHCI_PLAT

#ifdef CONFIG_SCSI_AHCI_XCODE
#define CONFIG_SYS_SCSI_MAX_SCSI_ID	1
#define CONFIG_SYS_SCSI_MAX_LUN	1	// 2
#define CONFIG_SYS_SCSI_MAX_DEVICE	(CONFIG_SYS_SCSI_MAX_SCSI_ID * CONFIG_SYS_SCSI_MAX_LUN)
#define CONFIG_SYS_SCSI_MAXDEVICE	CONFIG_SYS_SCSI_MAX_DEVICE
#define CONFIG_SCSI
#endif /* CONFIG_SCSI_AHCI_XCODE */

#define CONFIG_ENV_SIZE			(4 << 10)
#define CONFIG_ENV_SECT_SIZE    (256 * 1024)

/* SPI */
#define CONFIG_CMD_SPI
#ifdef CONFIG_CMD_SPI
#define CONFIG_CMD_SF
#define CONFIG_SPI_FLASH
#define CONFIG_XCODE_SPI            1
#define CONFIG_ENV_IS_IN_SPI_FLASH
#define CONFIG_SPI_FLASH_MACRONIX
#define CONFIG_SPI_FLASH_WINBOND
#define CONFIG_SPI_FLASH_STMICRO
#define CONFIG_SPI_FLASH_SPANSION
#define CONFIG_SPI_FLASH_BAR
#define CONFIG_SYS_NO_FLASH         1
//#define CONFIG_SF_DEFAULT_MODE		SPI_MODE_0
#endif

/* MMC */
#define CONFIG_CMD_MMC		/* MMC support                  */
#ifdef CONFIG_CMD_MMC
#define CONFIG_GENERIC_MMC		1
#define CONFIG_MMC				1
#define CONFIG_XCODE_MMC		1
//#define CONFIG_MMC_TRACE		1
//#define CONFIG_MMC_ENC          1
#define CONFIG_MMC_SQUASH_INFO  (0x37fff) /* ((112 * 1048576 - 512)/512) = 112MB - 512KB*/
#define CONFIG_MMC_KEY_SLOT     (14)
#endif

/* Enabled commands */
#define CONFIG_CMD_EXT2		/* EXT2 Support                 */
#define CONFIG_CMD_FAT		/* FAT support                  */
#define CONFIG_CMD_SCSI
#define CONFIG_CMD_NET
#define CONFIG_CMD_CACHE
#define CONFIG_CMD_DHCP
#define CONFIG_CMD_MD5SUM

#define CONFIG_CMD_I2C		/* I2C serial bus support	*/
#ifdef CONFIG_CMD_I2C
#define CONFIG_SYS_I2C_SPEED	100000
#define CONFIG_SYS_I2C_SLAVE	0x15
#define CONFIG_SYS_I2C_BUS		0
#define CONFIG_CMD_EEPROM
#define CONFIG_SYS_I2C_EEPROM_ADDR 0x50
#define CONFIG_SYS_I2C_EEPROM_ADDR_LEN 1
#endif

/* Disabled commands */
#undef CONFIG_CMD_NFS
#undef CONFIG_CMD_FPGA		/* FPGA configuration Support   */
#undef CONFIG_CMD_IMLS		/* List all found images        */

#define CONFIG_MII
#define CONFIG_CMD_MII
#define CONFIG_PHYLIB
#define CONFIG_PHY_GIGE

#define CONFIG_SYS_MAX_NAND_DEVICE 1
#define CONFIG_SYS_NAND_SELF_INIT

// Multiboot enable
//#define CONFIG_MULTIBOOT	1
#ifdef CONFIG_MULTIBOOT
#define CONFIG_BOOTFILE1		/boot/uImage_arm_gz
#define CONFIG_BOOTFILE2		/boot/uImage_mips_android_gz
#define CONFIG_BOOTFILE3		/boot/uImage_mips_gz_bd
#define CONFIG_BOOTFILE4 		/boot/uImage_mips_gz
#define CONFIG_BOOTFILE5 		/boot/uImage_mips_gz
#define CONFIG_BOOTARGS1		root=/dev/sda2 rw
#define CONFIG_BOOTARGS2  		root=/dev/sda2
#define CONFIG_BOOTARGS3  		root=/dev/sda6 rw
#define CONFIG_BOOTARGS4  		root=/dev/sda7 rw
#define CONFIG_BOOTARGS5  		root=/dev/sda2 rw
#define CONFIG_BOOTNAME			DEBUG
#define CONFIG_BOOTNAME_1		VIXS_SDK
#define CONFIG_BOOTNAME_2		Android
#define CONFIG_BOOTNAME_3		BDPlayer
#endif /* CONFIG_MULTIBOOT */

/*
 * Environment setup
 */

#define CONFIG_BOOTDELAY	2
#define CONFIG_ZERO_BOOTDELAY_CHECK    /* check console even if bootdelay = 0 */

#define CONFIG_ENV_OVERWRITE
#define CONFIG_SYS_MEMTEST_INTEGRITY
#ifndef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS \
	"rgmii=yes\0" \
	"gmii=yes\0" \
	"ethact=eth0\0" \
	"sata_clkfreq=100\0" \
	"sata_clktype=1\0" \
	"sata_ssmode=1\0" \
	"set_bootargs=setenv bootargs ethaddr=$ethaddr root=/dev/mmcblk0p2 rootwait\0" \
	"loadaddr=0x10000000\0" \
	"verifyaddr=0x20000000\0" \
	"flash_boot_ofs=0x0\0" \
	"flash_boot_sz=0x130000\0" \
	"eraseenv=dcache off; sf probe; sf erase 1ff000 +1000; dcache on\0" \
	"bootfile=/boot/uImage_arm_gz\0" \
	"ipaddr=172.23.10.55\0" \
	"serverip=172.23.10.85\0" \
	"Board_ID=0x0030\0" \
	"prog_boot=dcache off;sf probe;sf erase $flash_boot_ofs +$flash_boot_sz;sf write $loadaddr $flash_boot_ofs $flash_boot_sz;sf read $verifyaddr $flash_boot_ofs $flash_boot_sz;dcache on;cmp.b $loadaddr $verifyaddr $flash_boot_sz\0" \
	"bootemmc=mmc dev 0; ext2load mmc 0 10000000 /boot/uImage_arm_gz; bootm 10000000\0" \
	"ethaddr=00:88:23:84:17:41\0" \
	"netmask=255.255.0.0\0"\
	"watchdog=off\0" \
	"memtest=dcache off; mtest; dcache on\0" \
	"memsize=0x20000000\0" \
	"video-mode=xcfb:1280x720-24@60\0"\
	""
#endif

#define CONFIG_NET_RETRY_COUNT 20

#ifndef CONFIG_BOOTCOMMAND
#define CONFIG_BOOTCOMMAND \
	"run set_bootargs;run bootemmc"
#endif

#define CONFIG_AUTO_COMPLETE		1

#include <configs/xcode_common.h>
#define CONFIG_CMD_NET

/* GPIO */
#define CONFIG_CMD_GPIO

#define CONFIG_SYS_PROMPT		"XC6SDK # "
#define CONFIG_XCODE6_BOARDID	(0x0030)
#endif /* __CONFIG_XC6_SDK_H */
