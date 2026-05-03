#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/irqchip/arm-gic.h>
#include <linux/spinlock.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>

#include <mach/hardware.h>
#include <mach/xcode6-common.h>

#ifdef CONFIG_CACHE_L2X0
void __iomem *l2cache_base;
#endif

void __iomem *gic_dist_base_addr;

// IIA indirect register access lock
spinlock_t iia_reg_lock;
spinlock_t gpio_reg_lock;
EXPORT_SYMBOL(gpio_reg_lock);

void __init gic_init_irq(void)
{
	void __iomem *gic_cpu_base;

	/* Static mapping, never released */
	gic_dist_base_addr = (void __iomem *)XCODE6_GIC_DIST_BASE;

	/* Static mapping, never released */
	gic_cpu_base = (void __iomem *)XCODE6_GIC_CPU_BASE;

	gic_init(0, 29, gic_dist_base_addr, gic_cpu_base);

	spin_lock_init(&iia_reg_lock);
	spin_lock_init(&gpio_reg_lock);
}

#ifdef CONFIG_CACHE_L2X0

static void xcode6_l2x0_disable(void)
{
	/* Disable PL310 L2 Cache controller */
	xcode6_smc1(0x102, 0x0);
}

static void xcode6_l2x0_set_debug(unsigned long val)
{
	/* Program PL310 L2 Cache controller debug register */
	xcode6_smc1(0x100, val);
}

static int __init xcode6_l2_cache_init(void)
{
	u32 aux_ctrl = 0;

	/* Static mapping, never released */
	l2cache_base = (void __iomem *)CORE_PERIPHERAL_BASE+CORE_L2CACHE_OFFSET;
	BUG_ON(!l2cache_base);

	aux_ctrl = ((1 << L2X0_AUX_CTRL_ASSOCIATIVITY_SHIFT) |
			(0x1 << 25) |
			(0x1 << L2X0_AUX_CTRL_NS_LOCKDOWN_SHIFT) |
			(0x1 << L2X0_AUX_CTRL_NS_INT_CTRL_SHIFT));

#ifdef CONFIG_PLAT_XCODE64xx    // 1M L2 cache
	aux_ctrl |= ((0x3 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) |
#else   // CONFIG_PLAT_XCODE68xx   // 512K L2 cache
    aux_ctrl |= ((0x2 << L2X0_AUX_CTRL_WAY_SIZE_SHIFT) |
#endif
			(1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
			(1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT) |
			(1 << L2X0_AUX_CTRL_EARLY_BRESP_SHIFT));

	l2x0_init(l2cache_base, aux_ctrl, L2X0_AUX_CTRL_MASK);

	/*
	 * Override default outer_cache.disable 
	*/
	outer_cache.disable = xcode6_l2x0_disable;
	outer_cache.set_debug = xcode6_l2x0_set_debug;

	return 0;
}
early_initcall(xcode6_l2_cache_init);
#endif

unsigned int IIAGenericWrite(unsigned int id, unsigned int type, unsigned int irq, unsigned int val)
{
    unsigned int reg=HOST_INT_CTRL+0x10*(id);
    unsigned long flags;

	spin_lock_irqsave(&iia_reg_lock, flags);
    writel((val), (volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE+reg+4));
    writel((((irq)<<PROC5_INT_CTRL_INT_INDEX_SHIFT) & PROC5_INT_CTRL_INT_INDEX_MASK) | ((0<<PROC5_INT_CTRL_R_WN_SHIFT) & PROC5_INT_CTRL_R_WN_MASK) | (((type) << PROC5_INT_CTRL_TGT_SEL_SHIFT) & PROC5_INT_CTRL_TGT_SEL_MASK),
        (volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE+reg)); 
    spin_unlock_irqrestore(&iia_reg_lock, flags);
	return 0;
}
EXPORT_SYMBOL(IIAGenericWrite);

unsigned int IIAGenericRead(unsigned int id, unsigned int type, unsigned int irq)
{
    unsigned int reg=HOST_INT_CTRL+0x10*(id);
    unsigned long flags;
    unsigned int val;
	
	spin_lock_irqsave(&iia_reg_lock, flags);
    writel((((irq)<<PROC5_INT_CTRL_INT_INDEX_SHIFT) & PROC5_INT_CTRL_INT_INDEX_MASK) | ((1<<PROC5_INT_CTRL_R_WN_SHIFT) & PROC5_INT_CTRL_R_WN_MASK) | (((type) << PROC5_INT_CTRL_TGT_SEL_SHIFT) & PROC5_INT_CTRL_TGT_SEL_MASK),
        (volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE+(reg)));
    val = readl((volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE+(reg)+8));
    spin_unlock_irqrestore(&iia_reg_lock, flags);
 	return  val;
}
EXPORT_SYMBOL(IIAGenericRead);

unsigned long virt_to_phys_from_pte(void *addr)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
	
	extern struct mm_struct init_mm;
	struct	mm_struct *mm;

	unsigned long virt_addr = (unsigned long)addr;
	unsigned long phys_addr = 0UL;

	if (current->mm == NULL)
		mm = &init_mm;
	else
		mm = current->mm;

	/* get the page global directory. */
	pgd = pgd_offset(mm, virt_addr);

	if (!pgd_none(*pgd)) {
		/* get the page upper directory */
		pud = pud_offset(pgd, virt_addr);
		if (!pud_none(*pud)) {
			/* get the page middle directory */
			pmd = pmd_offset(pud, virt_addr);
			if (!pmd_none(*pmd)) {
				/* get a pointer to the page table entry */
				ptep = pte_offset_map(pmd, virt_addr);
				pte = *ptep;
				/* check for a valid page */
				if (pte_present(pte)) {
					/* get the physical address the page is
					 * refering to */
					phys_addr = (unsigned long)
						page_to_phys(pte_page(pte));
					/* add the offset within the page */
					phys_addr |= (virt_addr & ~PAGE_MASK);
				}
			}
		}
	}

	return phys_addr;
}
EXPORT_SYMBOL(virt_to_phys_from_pte);

/* handle the secure ramdisk image */
#define IH_TYPE_INVALID		0	/* Invalid Image		*/
#define IH_TYPE_KERNEL		2	/* OS Kernel Image		*/
#define IH_TYPE_RAMDISK		3	/* RAMDisk Image		*/
#define IH_TYPE_FILESYSTEM	7	/* Filesystem Image (any type)	*/
#define IH_MAGIC 0x27051956
#define IH_NMLEN 32
#define XCODE_DMA_SIZE	(64 * 1024)
#define mmreg_read(reg) readl((volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE + reg))
#define mmreg_write(reg, val) writel(val, (volatile void __iomem *)(XC_SOC_PROC_MMREG_BASE + reg))
#define reverse_endian(in) ((((in) & 0xFF) << 24) | ((((in) >> 8) & 0xFF) << 16) | ((((in) >> 16) & 0xFF) << 8) | (((in) >> 24) & 0xFF))

typedef struct uImage_header {
	u32		ih_magic;	/* Image Header Magic Number	*/
	u32		ih_hcrc;	/* Image Header CRC Checksum	*/
	u32		ih_time;	/* Image Creation Timestamp	*/
	u32		ih_size;	/* Image Data Size		*/
	u32		ih_load;	/* Data	 Load  Address		*/
	u32		ih_ep;		/* Entry Point Address		*/
	u32		ih_dcrc;	/* Image Data CRC Checksum	*/
	u8		ih_os;		/* Operating System		*/
	u8		ih_arch;	/* CPU architecture		*/
	u8		ih_type;	/* Image Type			*/
	u8		ih_comp;	/* Compression Type		*/
	u8		ih_name[IH_NMLEN];	/* Image Name		*/
} uImage_header_t;


/*
 * auxiliary dma ops to sync the dma access
 */
static void dma_sync(void)
{
	mmreg_write(0x0C80, 0);
	mmreg_write(0x0C84, 0);
	mmreg_write(0x0C88, 0);
	mmreg_write(0x0C8C, 0);
	mmreg_write(0x0C94, 0);
	mmreg_write(0x0CD0, 0);
	mmreg_write(0x0C90, 0x03000040);
	while(mmreg_read(0x0C34) & 4);
}

/*
 * get otp block content
 */
static int xcode6_get_otp_value(u32 index, u8 *value)
{
	u32 i;
	for (i=0; i<8; i++) {
		while((mmreg_read(0x1B04) & 2) == 0);
		mmreg_write(0x1B00, (0x1000000 | ((((8 * index + i) << 3) << 8) & 0x3fff00)));
		while((mmreg_read(0x1B04) & 2) == 0);

		if(mmreg_read(0x1B04) & 8)
			return (-1);

		value[7-i] = mmreg_read(0x1B00) & 0xff;
	}
	return 0;
}

#define RSA_EXP_SLOT	14
#define RSA_MOD_SLOT	15

static int xcode6_get_public_key(u8 *exp, u8 *mod)
{
	u32 ret, i;

	ret = xcode6_get_otp_value(RSA_EXP_SLOT, exp);
	BUG_ON(ret);

	for (i=0; i<32; i++) {
		ret = xcode6_get_otp_value(RSA_MOD_SLOT + i, &mod[i * 8]);
	//	printk("mod[%d] = 0x%016llx\n", i, ((unsigned long long *)mod)[i]);
		BUG_ON(ret);
	}
	return 0;
}

/* 
 * calculate SHA256
 * length in bytes
 */
static void xcode6_calc_hash(const void *const buf, u32 len)
{
	u32 pos = (u32)buf;
	u32 dma_sz, hw_sz;
	u32 first = 0x00400000;
	while (len) {
		dma_sz = (len > XCODE_DMA_SIZE) ? XCODE_DMA_SIZE : len;
		hw_sz = (dma_sz == XCODE_DMA_SIZE) ? 0 : dma_sz;
		mmreg_write(0x0C80, pos);
		mmreg_write(0x0C84, 0);
		mmreg_write(0x0C94, 0x00500000);
		mmreg_write(0x0C90, (0x01000000 | first | hw_sz));
		while(mmreg_read(0x0C34) & 4);

		pos += dma_sz;
		len -= dma_sz;
		first = 0;
	}
}

static u32 pad_for_SHA(u8 * const buf, const u32 len)
{
	u32 n, i;
	i = len;
	n = i << 3;

	buf[i++] = 0x80;
	while((i % (512>>3)) != (448>>3))
		buf[i++] = 0;
	buf[i++] = 0;
	buf[i++] = 0;
	buf[i++] = 0;
	buf[i++] = 0;
	buf[i++] = (n >> 24) & 0xff;
	buf[i++] = (n >> 16) & 0xff;
	buf[i++] = (n >> 8) & 0xff;
	buf[i++] = n & 0xff;

	//printk("padded for sha %d\n", i-len);
	return (i - len);
}

static void xcode6_calc_sig(const u32 * const signature)
{
	u32 v, i;
	u32 exponent[8 >> 2];
	u32 modulus[256 >> 2];

	xcode6_get_public_key((u8*)exponent, (u8*)modulus);

	mmreg_write(0x1800, 0);
	for (i=0; i<64; i++) {
		v = 1 | (i<<4);
		mmreg_write(0x1800, v);
		while(mmreg_read(0x1800) != v);
		mmreg_write(0x1804, modulus[i]);
	}

	for (i=0; i<64; i++) {
		v = 2 | (i<<4);
		mmreg_write(0x1800, v);
		while(mmreg_read(0x1800) != v);

		if (i < 2) {
			mmreg_write(0x1804, exponent[i]);
		} else {
			mmreg_write(0x1804, 0);
		}
	}

	for (i=0; i<64; i++) {
		v = 4 | (i<<4);
		mmreg_write(0x1800, v);
		while(mmreg_read(0x1800) != v);
		mmreg_write(0x1804, reverse_endian(signature[63 - i]));
	}

	mmreg_write(0x1800, 0);
	while(mmreg_read(0x1800) != 0);

	while(mmreg_read(0x1814) & 8);
	mmreg_write(0x1814, 0x00100000);
	while(mmreg_read(0x1814) & 8);
	while(!(mmreg_read(0x181C) & 2));
	mmreg_write(0x181C, 2);

#if 0
	for(i=0; i<64; i++) {
		if((i%8) == 0)
			printk("\n");
		v = i << 4;
		mmreg_write(0x1800, v);
		while(mmreg_read(0x1800) != v);
		printk("0x%08x", mmreg_read(0x1808));
	}
#endif
	return;
}

static int xcode6_check_hash(const u8 *pa, const u32 len)
{
	u32 v, i;
	const u32 * const pbuf = (const u32 * const)pa;

	xcode6_calc_hash(pbuf, len);

	for(i=0; i<8; i++) {
		mmreg_write(0x0D00, 7-i);
		v = i << 4;
		mmreg_write(0x1800, v);
		while(mmreg_read(0x1800) != v);
		v = mmreg_read(0x1808);
//		printk("\n%s 0x%x ?= 0x%x\n", __func__, v, mmreg_read(0x0D04));
		if(v != mmreg_read(0x0D04))
			return -1;
	}
	return 0;
}

#define AES_CW_SLOT 	8 
#define AES_IV_SLOT 	10
#define AES_IV_DST_SLOT	12
static int xcode6_decrypt_image(u8 *src, u8 *dst, u32 size, u32 cw_idx, u32 iv_idx)
{
	u32 first = 1;
	u32 dma_sz, hw_sz;
	u32 from = (u32)src;
	u32 to = (u32)dst;

	/* DMA AES size must be 16 bytes aligned */
	if(size & 0xf) {
		printk("%s requested size 0x%x is not aligned to 16 bytes\n", __func__, (unsigned)size);
		return -1;
	}

	while (size) {
		dma_sz = (size > XCODE_DMA_SIZE) ? XCODE_DMA_SIZE : size;
		hw_sz = (dma_sz == XCODE_DMA_SIZE) ? 0 : dma_sz;

		mmreg_write(0x0C80, from);
		mmreg_write(0x0C84, 0);
		mmreg_write(0x0C88, to);
		mmreg_write(0x0C8C, 0);
		mmreg_write(0x0C94, 0x80);
		mmreg_write(0x0CD0, (first ? iv_idx : AES_IV_DST_SLOT) |\
							(AES_IV_DST_SLOT << 8) |\
							(cw_idx << 16));
		mmreg_write(0x0C90, 0x03000000 | (2 << 20) | (3 << 16) | hw_sz);
		while(mmreg_read(0x0C34) & 4);

		from += dma_sz;
		to += dma_sz;
		size -= dma_sz;
		first = 0;
	}
	dma_sync();
	return 0;
}

static u32 is_secure_boot(void)
{
	u8 value[8];
	u32 strap_bit, stop_bit;
	strap_bit = mmreg_read(0x0478);

	stop_bit = mmreg_read(0x0050);
	stop_bit &= ~((1<<5) | (1<<8));
	mmreg_write(0x0050, stop_bit);

	/* treat fail as secure boot */
	if(xcode6_get_otp_value(2, value))
		return 1;

	//printk("otp blk 2 = %02x  %02x  %02x  %02x  %02x  %02x  %02x  %02x\n", value[0], value[1], value[2], value[3],value[4], value[5], value[6], value[7]);
	if((value[7] & 1) || (strap_bit & (1<<27)))
		return 1;
	return 0;
}

int decrypt_and_validate_image(u32 va, u32 len)
{
	uImage_header_t *hdr;
	u8 *sig;
	u32 pa, cnt;
	u32 secu_boot = 0;

	va -= sizeof(uImage_header_t);
	hdr = (uImage_header_t *)va;

	/* only allow non-secure boot boot the clear image */
	secu_boot = is_secure_boot();
	if(secu_boot == 0) {
		if(reverse_endian(hdr->ih_magic) == IH_MAGIC) {
			/* clear image */
			return 0;
		}
	}

	if((u32)va & 0x1f) {
		printk("image start at unaligned address 0x%x\n", va);
		return -1;
	}

	pa = __virt_to_phys(va);
	/* align the size to 16 bytes block */
	cnt = (len % 16) ? (len & (~15)) + 16 : len;
	cnt +=  sizeof(uImage_header_t);
	printk("Checking image at 0x%x@0x%x image size 0x%x padded size 0x%x ...\n", va, pa, len, cnt);

	/* decrypt the image */
	xcode6_decrypt_image((u8 *)pa, (u8 *)pa, cnt, AES_CW_SLOT, AES_IV_SLOT);
	inv_dcache_range((u32)hdr, (u32)hdr + sizeof(uImage_header_t));
	if(reverse_endian(hdr->ih_magic) != IH_MAGIC) {
		printk("unknown image magic 0x%x, fail !\n", reverse_endian(hdr->ih_magic));
		return -1;
	}

	/* validate the image */
	sig =(u8 *)((u32)va + cnt);
	//printk("signature located at 0x%p\n", (void *)sig);
	xcode6_calc_sig((const u32 *)sig);

	/* include the image header */
	cnt += pad_for_SHA((u8 * const)va, cnt);
	flush_dcache_range((va + sizeof(uImage_header_t) + len) & ~31, (va + cnt + 32) & ~31);
	if(xcode6_check_hash((u8 * const)pa, cnt) != 0) {
		printk("image validation fail !\n");
		return -1;
	}
	return 0;
}
EXPORT_SYMBOL(decrypt_and_validate_image);
