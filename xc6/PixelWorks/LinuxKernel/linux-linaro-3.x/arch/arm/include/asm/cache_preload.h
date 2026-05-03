#ifndef ARCH_ASM_CACHE_PRELOAD_H
#define ARCH_ASM_CACHE_PRELOAD_H

#define PLEIDR_PRESENT			0x00000001
#define PLEIDR_PLE_FIFO_SIZE	0x001f0000
#define PLEASR_R				0x00000001
#define PLEFSR_AVAILABLE		0x0000001f
#define PLEUAR_U				0x00000001
#define PLEPCR_WAIT_STATES		0x000000ff
#define PLEPCR_BLK_NUM			0x0000ff00
#define PLEPCR_BLK_SIZE			0x3fff0000

#define CACHE_PRELOAD_MAX_BLKSIZE 			(((PLEPCR_BLK_SIZE>>16)+1)*4)
#define CACHE_PRELOAD_MAX_BLKNUM 			((PLEPCR_BLK_NUM>>8)+1)
#define CACHE_PRELOAD_MAX_RATE				(PLEPCR_WAIT_STATES+1)

#if  __LINUX_ARM_ARCH__ >= 6

/* Check if cache preload engine exists or not and return the maximum command FIFO size */
static int cache_preload_init(void)
{
	u32 u;

 	__asm__ __volatile__("mrc p15, 0, %0, c11, c0, 0" : "=r"(u));

	if(!(u&PLEIDR_PRESENT))
		return -EINVAL;

	return ((u&PLEIDR_PLE_FIFO_SIZE)>>16);
}

/* Return number of free entries in command FIFO */
static int cache_preload_fifo_status(void)
{
	u32 u;

	__asm__ __volatile__("mrc p15, 0, %0, c11, c0, 4" : "=r"(u)); 

	return (u&PLEFSR_AVAILABLE);
}

/* Tells if preload engine is executing command or not */
static int cache_preload_busy(void)
{
	u32 u;
 
	__asm__ __volatile__("mrc p15, 0, %0, c11, c0, 2" : "=r"(u)); 

	return (u&PLEASR_R);
}

/* Tell if user can access preload engine or not */
static int cache_preload_get_user_acccess(void)
{
	u32 u;

 	__asm__ __volatile__("mrc p15, 0, %0, c11, c1, 0" : "=r"(u)); 

	return (u&PLEUAR_U);
}

/* Set if user can access preload engine or not */
static void cache_preload_set_user_acccess(int allow)
{
 	__asm__ __volatile__("mcr p15, 0, %0, c11, c1, 0" :: "r"(allow)); 
}

/* Config the preload engine */
static int cache_preload_set_config(int max_blk_size, int max_num_blk, int rate)
{
	u32 u;

	if((max_blk_size&0x3) || (max_blk_size>CACHE_PRELOAD_MAX_BLKSIZE) ||
		(max_num_blk>CACHE_PRELOAD_MAX_BLKNUM) ||
		(rate>CACHE_PRELOAD_MAX_RATE))
		return -EINVAL;

	u=(((max_blk_size>>2)-1)<<16) | ((max_num_blk-1)<<8) | (rate-1);

 	__asm__ __volatile__("mcr p15, 0, %0, c11, c1, 1" :: "r"(u)); 

	return 0;
}

static void cache_preload_get_config(int *max_blk_size, int *max_num_blk, int *rate)
{
	u32 u;

 	__asm__ __volatile__("mrc p15, 0, %0, c11, c1, 1" : "=r"(u));

	if(max_blk_size)
		*max_blk_size=(((u&PLEPCR_BLK_SIZE)>>16)+1)*4;

	if(max_num_blk)
		*max_num_blk=(((u&PLEPCR_BLK_NUM)>>8)+1);

	if(rate)
		*rate=(u&PLEPCR_WAIT_STATES)+1;

	return;
}

/* Instruct preload engine to load consequence virtual address blocks into L2 cachea.
	Each block start address will be separated by stride_size bytes.

 */
static void cache_preload_cmd(u32 virt_start_addr, u32 blksize, u32 numblk, u32 stride_size)
{
	u32 u;
	int max_blk_size, max_num_blk;

	cache_preload_get_config(&max_blk_size, &max_num_blk, NULL);
	
	if((blksize>max_blk_size) || (numblk>max_num_blk) || (stride_size>1024))
		return -EINVAL;

	if(virt_start_addr&0x3) //Address must be aligned to 4 bytes
		return -EINVAL;

	if(blksize&0x3) //Block size must be aligned to 4 bytes
		return -EINVAL;

	if(stride_size&0x3) 
		return -EINVAL;

 	u=(((blksize>>2)-1)<<18) | (((stride_size>>2)-1)<<10) | ((numblk-1)<<2);

	__asm__ __volatile__("mcrr p15, 0, %0, %1, c11" : : "r"(virt_start_addr), "r"(u)); 

	return;	
}


#else

#define cache_preload_init() (-EINVAL)
#define cache_preload_fifo_status() (0)
#define cache_preload_busy() (0)
#define cache_preload_get_user_acccess() (0)
#define cache_preload_set_user_acccess(u)
#define cache_preload_set_config(blksize, blknum, rate)	(-EINVAL)
#define cache_preload_get_config(a, b, c) 
#define cache_preload_cmd(a, b, c, d)

#endif


#endif
