#include <common.h>
#include <command.h>
#include <spi_flash.h>
#include <asm/io.h>
#include <asm/arch/xcodeRegDef.h>
#include <asm/arch-xc6/security.h>
#include <asm/armv7.h>
#include <linux/types.h>

/* secure function debug */
//#define SEC_DEBUG
#ifdef SEC_DEBUG
#undef debug
#define debug(fmt, args...)	printf(fmt, ##args)
#endif

/* debug with software loaded RSA-2048 modulus */
//#define DEBUG_LDRSAKEY

#define DMA_WAIT_CNT    1000
#define mmreg_read(addr)			(*((volatile u32 *)(XC_SOC_PROC_MMREG_BASE + addr)))
#define mmreg_write(addr, data) 	(*((volatile u32 *)(XC_SOC_PROC_MMREG_BASE + addr)) = (data))
#define reverse_endian(in)              ((((in) & 0xFF) << 24) | ((((in) >> 8) & 0xFF) << 16) | ((((in) >> 16) & 0xFF) << 8) | (((in) >> 24) & 0xFF))

void dma_init(void)
{	
	/* handle the clocks */
    mmreg_write(CG_CLK_STOP0, mmreg_read(CG_CLK_STOP0) & \
		~(XCLK_STOP_MASK | DCLK_STOP_MASK | DMACLK_STOP_MASK | REF27CLK_STOP_MASK |\
		 CFCLK_STOP_MASK | ECCCLK_STOP_MASK | MRCLK_STOP_MASK | MCLK_STOP_MASK));
    mmreg_write(ACC_BLK_STOP0, mmreg_read(ACC_BLK_STOP0) & ~(DMA_BLK_STOP_MASK));
    mmreg_write(ACC_RESET_REG0, mmreg_read(ACC_RESET_REG0) & ~(DMA_RESET_MASK));
}

/*
 * wait for DMA DIRECT_A channel idel 
 */
static int dma_wait_idle(void)
{
    u32 loop = 0;
    while (mmreg_read(0x0C34) & 4) {
        if (loop++ > DMA_WAIT_CNT) {
            printf("Error, DMA direct-A not idle!");
            return (-1);
        }
    }
    return 0;
}

/*
 * dma_sync: a dummy dma copy transfer.
 * Add a dma_sync after dma operation to
 * avoid the DMA and PROC5 race condition.
 */
static void dma_sync(void)
{
       mmreg_write(0x0C84, 0);
       mmreg_write(0x0C80, 0x100000);
       mmreg_write(0x0C8C, 0);
       mmreg_write(0x0C88, 0x100000);

       mmreg_write(0x0C94, 0x0);
       mmreg_write(0x0CD0, 0x0);
       CP15DSB;
       mmreg_write(0x0C90,
                       0x02000000|
                       0x01000000|
                       64);
       CP15DSB;
       while (mmreg_read(0x0C34) & 4);
}

/* only for secure boot */
#ifdef CONFIG_SECURE_BOOT

#ifdef CONFIG_SW_SHA256
#define	BLOCK_MASK			0x00FF	

#define TRUNC32(x) ((x) & 0xFFFFFFFFUL)
#define Ch(x,y,z)  (((x) & (y)) ^ ((~(x)) & (z)))
#define Maj(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define SHRn(x,n)  TRUNC32(((x)>>n))
#define RORn(x,n)  (((x)>>n)|((x)<<(32-n)))
#define ROLn(x,n)  (((x)<<n)|((x)>>(32-n)))
#define SIG0(x)    ((RORn((x),2)  ^ RORn((x),13) ^ RORn((x),22)))
#define SIG1(x)    ((RORn((x),6)  ^ RORn((x),11) ^ RORn((x),25)))
#define WRO0(x)    ((RORn((x),7)  ^ RORn((x),18) ^ SHRn((x),3)))
#define WRO1(x)    ((RORn((x),17) ^ RORn((x),19) ^ SHRn((x),10)))



const u32 SHA256_Constant[64] = {
    0x428a2f98UL,0x71374491UL,0xb5c0fbcfUL,0xe9b5dba5UL,
    0x3956c25bUL,0x59f111f1UL,0x923f82a4UL,0xab1c5ed5UL,
    0xd807aa98UL,0x12835b01UL,0x243185beUL,0x550c7dc3UL,
    0x72be5d74UL,0x80deb1feUL,0x9bdc06a7UL,0xc19bf174UL,
    0xe49b69c1UL,0xefbe4786UL,0x0fc19dc6UL,0x240ca1ccUL,
    0x2de92c6fUL,0x4a7484aaUL,0x5cb0a9dcUL,0x76f988daUL,
    0x983e5152UL,0xa831c66dUL,0xb00327c8UL,0xbf597fc7UL,
    0xc6e00bf3UL,0xd5a79147UL,0x06ca6351UL,0x14292967UL,
    0x27b70a85UL,0x2e1b2138UL,0x4d2c6dfcUL,0x53380d13UL,
    0x650a7354UL,0x766a0abbUL,0x81c2c92eUL,0x92722c85UL,
    0xa2bfe8a1UL,0xa81a664bUL,0xc24b8b70UL,0xc76c51a3UL,
    0xd192e819UL,0xd6990624UL,0xf40e3585UL,0x106aa070UL,
    0x19a4c116UL,0x1e376c08UL,0x2748774cUL,0x34b0bcb5UL,
    0x391c0cb3UL,0x4ed8aa4aUL,0x5b9cca4fUL,0x682e6ff3UL,
    0x748f82eeUL,0x78a5636fUL,0x84c87814UL,0x8cc70208UL,
    0x90befffaUL,0xa4506cebUL,0xbef9a3f7UL,0xc67178f2UL };



void SHA256_Init (u32* const H)
{
    H[0]=0x6a09e667UL;
    H[1]=0xbb67ae85UL;
    H[2]=0x3c6ef372UL;
    H[3]=0xa54ff53aUL;
    H[4]=0x510e527fUL;
    H[5]=0x9b05688cUL;
    H[6]=0x1f83d9abUL;
    H[7]=0x5be0cd19UL;
}

void SHA256_Hash_Calculation(u32* const H, const u32* const m, const int N)
{
    u32 W[64],T1,T2;
    u32 a,b,c,d,e,f,g,h;
    int i,n,t;

    SHA256_Init(H);
    n=0;
    for(i=0;i<N;i+=512)
    {
        for (t=0;t<16;t++)
        {
            W[t]=reverse_endian(m[n]);
            n++;
        }
        for (;t<64;t++)
        {
            W[t]=TRUNC32(WRO1(W[t-2])+TRUNC32(W[t-7]+TRUNC32(TRUNC32(WRO0(W[t-15]))+W[t-16])));
        }
        a = H[0];   
        b = H[1];   
        c = H[2];   
        d = H[3];
        e = H[4];   
        f = H[5];   
        g = H[6];   
        h = H[7];
        for(t=0;t<64;t++)
        {
            T1=TRUNC32(h+SIG1(e)+Ch(e,f,g)+SHA256_Constant[t]+W[t]);
            T2=TRUNC32(SIG0(a)+Maj(a,b,c));
            h=g;
            g=f;
            f=e;
            e=TRUNC32(d+T1);
            d=c;
            c=b;
            b=a;
            a=TRUNC32(T1+T2);
        }
        H[0]=a+H[0];    
        H[1]=b+H[1];    
        H[2]=c+H[2];    
        H[3]=d+H[3];
        H[4]=e+H[4];    
        H[5]=f+H[5];    
        H[6]=g+H[6];    
        H[7]=h+H[7];
    }

#ifdef DEBUG
    printf("sha256 result:\n");
    for(i=0;i<8;i++) {
        printf("%x ",H[i]);
    }
    printf("\n");
#endif
}
#else
/* 
 * HW SHA256 calculation
 * length in byte
 */
void calc_hash(const void * const buf, u32 len)
{
	u32 pos = (u32)buf;
	u32 mask = 0x00400000;
	u32 dma_size, hw_size;

    if (dma_wait_idle())
        return;

	while(len) {
		dma_size = (len > (64 * 1024)) ? (64 * 1024) : len;
		hw_size = (dma_size == (64 * 1024)) ? 0 : dma_size;

		mmreg_write(0x0C80, pos);
		mmreg_write(0x0C84, 0);
		mmreg_write(0x0C94, 0x00400000 | 0x00100000);
		mmreg_write(0x0C90, 0x01000000 | mask | hw_size);
		while(mmreg_read(0x0C34) & 0x00000004);

		pos += dma_size;
		len -= dma_size;
		mask = 0;
	}
}
#endif //CONFIG_SW_SHA256


static u32	get_OTP_block_value(u32 block, u8 * value)
{
    u32 i;
	for (i = 0; i < 8; i++)
	{	/* polling for ready */
        while ((mmreg_read(0x1B04) & 0x2) == 0);

		/*wrtie block index */
        mmreg_write(0x1B00, 0x1000000 | ((((8*((u32)block) + i) << 3) << 8) & 0x3FFF00));

		/* polling for ready */
        while ((mmreg_read(0x1B04) & 0x2) == 0);
		
		/* read check lock */
        if (mmreg_read(0x1B04) & 0x8)
            return(-1);
		/* read value */
        value[7-i] = mmreg_read(0x1B00) & 0x0FF;
    }

	return(0);
}

static u32 get_OTP_BOOT_Public_Key(u8* exponent, u8* modulus)
{
    u32  rc,i;

	u32 data[2];
	for (i = 0; i < 64; i++) {
		if ((i % 2) == 0) {
			get_OTP_block_value(0xF + i, data);
			debug("otp blk[%d] = 0x%08x 0x%08x\n", 0xf + i, data[0], data[1]);
		}
	}


    rc=get_OTP_block_value(0x0E,exponent);
    for(i=0;i<32;i++)
    {
        if(rc == 0){
            rc=get_OTP_block_value(0x0F+i,&modulus[i*8]);
        }
    }
    return(rc);
}

/* update key for debug */
void UpdateCWIV(u32 index, u32* pbuf)
{
    u32 i, j;
    u32 temp;

    for (i = 0; i < 4; i += 2)
    {
        mmreg_write(0x1824, *(pbuf + i));
        mmreg_write(0x1828, *(pbuf + i+1));
        temp = 0;
        temp = index << 0;
        mmreg_write(0x182C, temp);
        index++;

        j = 0;
        while(j < 5)
        {
            temp = mmreg_read(0x182C);
            temp &= 0x80000000;
            if (temp == 0x80000000)
                j++;
        }     
    }
}

int decrypt_image(u8* src, u8* dst, u32 size, u32 cw_idx, u32 iv_idx)
{
    u32 first = 1, mode, iv_idx_dst;
	
    debug("enter decrypt image, mode AES_CBC_128, src %x size %x\n", (int)src, size);

	if (size & 0xf) {
		printf("Invalid AES-128 size %d\n", size);
		return (-1);
	}

	mode = CONFIG_SECURE_BOOT_MODE;
	switch (mode) {
	case 1:
		iv_idx_dst = 254;
		break;
	case 2:
		iv_idx_dst = CONFIG_AES_IV_DST_SLOT;
		break;
	case 3:
		iv_idx_dst = CONFIG_AES_IV_SLOT;
		break;
	default:
		iv_idx_dst = 12;
		break;
	}

    if (dma_wait_idle())
        return (-1);

    while (size)
    {
		u32 thissize;
		u32 hwsize;

		if (size >= 64*1024)
			thissize = 64*1024;
		else
			thissize = size;

		if (thissize == 64*1024)
			hwsize = 0;
		else
			hwsize = thissize;

		hwsize <<= 0;
		hwsize &= 0xFFFF;

		mmreg_write(0x0C84, 0);
		mmreg_write(0x0C80, (u32)src);
		mmreg_write(0x0C8C, 0);
		mmreg_write(0x0C88, (u32)dst); 

		mmreg_write(0x0C94, 0x80);
		mmreg_write(0x0CD0, ((first ? iv_idx : iv_idx_dst) << 0) |\
				(iv_idx_dst << 8) |\
				(cw_idx << 16));
		CP15DSB;
		mmreg_write(0x0C90,
				0x02000000|                     
				0x01000000|                    
				(2 << 20) |         
				(3 << 16) | 
				hwsize);
		CP15DSB;
		while (mmreg_read(0x0C34) & 4);

		src += thissize;
		dst += thissize;
		size -= thissize;
		first = 0;	
    }
	dma_sync();
	return 0;
}

void calc_sig(UCV const u32* const sig)
{
    u32 v, i;
    u32 modulus[256>>2];
    u32 public_key[8>>2];

#ifdef DEBUG_LDRSAKEY
/* Use debug key*/
u8 modulus1[256];
u8 modulus2[256] = {
	0xD8, 0xB1, 0xA8, 0x27, 0x15, 0x7C, 0x0A, 0x5C, 0x58, 0x13, 0x21, 0x79,
	0x19, 0x71, 0xE1, 0xC6, 0x4F, 0x49, 0x0E, 0x80, 0x5F, 0x4A, 0xF9, 0xEC,
	0x95, 0xB1, 0xCD, 0x38, 0x62, 0xAE, 0x52, 0x01, 0xFA, 0x96, 0x9E, 0x53,
	0x96, 0xD2, 0x2E, 0x0C, 0xC1, 0x6C, 0x48, 0x96, 0x24, 0x19, 0x86, 0xF6,
	0x03, 0x2C, 0xC2, 0xF6, 0x66, 0x38, 0xDC, 0x38, 0x0B, 0x21, 0xCA, 0x2B,
	0x6C, 0xC1, 0xC0, 0xB5, 0xE7, 0x65, 0xA1, 0x0A, 0xD1, 0x05, 0x73, 0xEF,
	0x72, 0x00, 0x64, 0x8F, 0x89, 0x8D, 0x5D, 0x11, 0x59, 0x38, 0x41, 0xBE,
	0xF1, 0x61, 0xE1, 0x63, 0xAF, 0xB5, 0xEF, 0x6D, 0xCA, 0xC0, 0x9E, 0x9B,
	0x30, 0x19, 0x6E, 0x48, 0xF3, 0x62, 0x9C, 0x50, 0x59, 0x3B, 0x71, 0x44,
	0xF5, 0x83, 0x54, 0xBE, 0x9C, 0xAA, 0xE0, 0x07, 0x31, 0x2D, 0x89, 0xB2,
	0x78, 0x13, 0x7A, 0x1C, 0x67, 0xEA, 0x68, 0x1A, 0x92, 0x0D, 0xEB, 0x18,
	0xAE, 0xD2, 0xCE, 0x52, 0x44, 0x8E, 0x4A, 0x43, 0xB0, 0x83, 0x9F, 0x16,
	0x4C, 0x99, 0xA8, 0x41, 0xE8, 0xF1, 0x82, 0xF8, 0xBC, 0xDD, 0xC6, 0x3B,
	0x87, 0x18, 0xEF, 0x48, 0xDA, 0x78, 0x6A, 0xC9, 0xC4, 0xEE, 0x21, 0x31,
	0x77, 0x00, 0x97, 0x73, 0x68, 0x99, 0x13, 0x69, 0x9C, 0x37, 0xD9, 0xFD,
	0xDE, 0x35, 0xA1, 0x82, 0x31, 0xA2, 0x97, 0xED, 0xF0, 0x0B, 0xA1, 0xDD,
	0xEC, 0xA7, 0xA9, 0xD9, 0xA2, 0x74, 0x8D, 0xB9, 0x96, 0x27, 0x8B, 0xDC,
	0xC0, 0xB8, 0x08, 0x0A, 0x87, 0xEB, 0x28, 0x6F, 0xF4, 0x0A, 0xB3, 0x8D,
	0xAD, 0x55, 0x71, 0xA4, 0x7B, 0xA9, 0x77, 0x9D, 0xD6, 0xEF, 0xEE, 0xE7,
	0xE3, 0xF3, 0x75, 0xC2, 0x91, 0x9D, 0x09, 0x4D, 0x98, 0x8B, 0xE9, 0x9A,
	0xAD, 0xE7, 0x75, 0x73, 0x1B, 0xD8, 0x4F, 0x0F, 0xF5, 0x4A, 0x3A, 0xA3,
	0xF0, 0x49, 0x26, 0x15
};																						
    u8 exponent[256]={0x1,0x00,0x01,0x00};
    int j;
    int modLen = 64, expLen = 1;

    u32* pModulus = (void*)modulus1;
    u32* pExp = (void*)exponent;

    for(j=0; j<256; j++)
        modulus1[j]=modulus2[255-j];

    mmreg_write(0x1800, 0);
    /* modulus */
    for (i = 0; i < 64; i++)
    {
        v = 1| i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);
        debug("load modulus[%d] = 0x%08x\n", i, pModulus[i]);
        if (i < modLen)
            mmreg_write(0x1804, pModulus[i]);
        else
            mmreg_write(0x1804, 0);
    }        
    
    // public key
    for (i = 0; i < 64; i++)
    {
        v = 2| i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);
 		debug("load public key[%d] = 0x%08x\n", i, pExp[i]);       
        if (i < expLen)
            mmreg_write(0x1804, pExp[i]);
        else
            mmreg_write(0x1804, 0);
    }

#else
	/* use OTP keys */
    get_OTP_BOOT_Public_Key((u8*)public_key, (u8*)modulus);

    mmreg_write(0x1800, 0);
    for (i = 0; i < 64; i++)
    {
        v = 1 | i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);
        debug("load modulus[%d] = 0x%08x\n", i, modulus[i]);
        mmreg_write(0x1804, modulus[i]);
    }

    for (i = 0; i < 64; i++)
    {
        v = 2| i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);

        if (i < 2)
            mmreg_write(0x1804, public_key[i]);
        else
            mmreg_write(0x1804, 0);
    }
#endif


    for (i = 0; i < 64; i++)
    {
        v = 4 | i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);

        mmreg_write(0x1804, reverse_endian(sig[63-i]));
    }

    mmreg_write(0x1800, 0);
    while (mmreg_read(0x1800) != 0);

    while (mmreg_read(0x1814) & 8);
    mmreg_write(0x1814, 0x00100000);

    while (mmreg_read(0x1814) & 8);
    while (! (mmreg_read(0x181C) & 2));
    mmreg_write(0x181C, 2);
}


u32 pad_for_SHA(u8* const buf, const u32 len)
{
    u32 n, i;

    i = len;
    n = i << 3;

    buf[i++] = 0x80;
    while ((i % (512>>3)) != (448>>3))
        buf[i++] = 0x00;

    buf[i++] = 0x00;
    buf[i++] = 0x00;
    buf[i++] = 0x00;
    buf[i++] = 0x00;
    buf[i++] = (n >> 24) & 0xFF;
    buf[i++] = (n >> 16) & 0xFF;
    buf[i++] = (n >>  8) & 0xFF;
    buf[i++] = (n      ) & 0xFF;

#ifdef DEBUG
    printf("padded for sha %d\n", i-len);
#endif

    return(i - len);
}


u32 check_hash(const u8* const p, const u32 len)
{
	u32 v, i;
    const u32* const pbuf = (const u32* const)p;

#ifdef DEBUG
    printf("rsa read back: \n");
    for (i = 0; i < 64; i++)   
    {
        if(i%8 == 0)
            printf("\n");
        v = i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);
        v = mmreg_read(0x1808);

        printf("0x%08x ", v); 
    }
#endif

#ifdef CONFIG_SW_SHA256
    u32 H[8];

    SHA256_Hash_Calculation(H, pbuf, len << 3); 

    for (i = 0; i < 8; i++)   
    {
        v = i << 4;
        mmreg_write(0x1800, v);
        while (mmreg_read(0x1800) != v);
        v = mmreg_read(0x1808);

        if (v != H[7 - i])
            return(0);
    }
#else
	flush_dcache_range((u32)pbuf, ALIGN_CACHE_SIZE((u32)pbuf + len));
	calc_hash(pbuf, len);

	for (i=0; i<8; i++) {
		mmreg_write(0x0D00, 7-i);
		v = i << 4;
		mmreg_write(0x1800, v);
		while(mmreg_read(0x1800) != v);
		v = mmreg_read(0x1808);
		if (v != mmreg_read(0x0D04))
			return (0);
	}
#endif
    return(1);
}
#endif //CONFIG_SECURE_BOOT

int crypto_aes_ecb_128(u8* src, u8* dst, u32 size, u32 enc, u32 cw_idx)
{
    debug("[%s], <%s> src 0x%x dst 0x%x size 0x%x, key slot %d\n", __func__, (enc == CRYPT_OP_ENC)?"encrypt":"decrypt", 
            (unsigned)src, (unsigned)dst, size, cw_idx);

	if (size & 0xf) {
        printf("[%s] Invalid block size %d!\n", __func__, size);
		return (-1);
	}
    if ((enc != CRYPT_OP_ENC)&&(enc != CRYPT_OP_DEC)) {
        printf("[%s] Invalid crypto mode %d!\n", __func__, enc);
        return (-1);
    }

    if (dma_wait_idle())
        return (-1);

	while (size)
	{
		u32 thissize;
		u32 hwsize;

		if (size >= 64 * 1024) {
			thissize = 64 * 1024;
		} else {
			thissize = size;
		}
		if (thissize == (64 * 1024))
			hwsize = 0;
		else
			hwsize = thissize;

		mmreg_write(0x0C84, 0);
		mmreg_write(0x0C80, (u32)src);
		mmreg_write(0x0C8C, 0);
		mmreg_write(0x0C88, (u32)dst); 

		mmreg_write(0x0C94, 0x0);
		mmreg_write(0x0CD0, (cw_idx << 16));

		CP15DSB;
		mmreg_write(0x0C90, \
				0x02000000 |\
				0x01000000 |\
                (enc << 20)  |\
				(3 << 16) |\
				hwsize);
        debug("[%s] submit command 0x%x\n", __func__, mmreg_read(0x0C90));		
		CP15DSB;
		while (mmreg_read(0x0C34) & 4);

		src += thissize;
		dst += thissize;
		size -= thissize;
    }
	dma_sync();
	return 0;
}


#ifdef CONFIG_SCSA
static int load_keyslot(u8 slot, u8 *pkey)
{
	int i;

	uint dw0, dw1, dw2, dw3;
	dw0=dw1=dw2=dw3=0;
	for (i=0; i<4; i++) {
		dw0 |= pkey[i] << (i*8);
		dw1 |= pkey[i+4] << (i*8);
		dw2 |= pkey[i+8] << (i*8);
		dw3 |= pkey[i+12] << (i*8);
	}

	debug("key store slot %d, from %p value 0x%x_%x_%x_%x\n", slot, pkey, dw0, dw1, dw2,dw3);

	i = 100;
	while((mmreg_read(0x182c) & (1 << 31)) == 0) {
		if(i-- < 0) {
			printf("load keytstore fail, rsa array not idle\n");
			return 1;
		}
	}
	mmreg_write(0x1824, dw0);
	mmreg_write(0x1828, dw1);
	mmreg_write(0x182c, slot);
	i = 100;
	while((mmreg_read(0x182c) & (1 << 31)) == 0) {
		if(i-- < 0) {
			printf("load keytstore fail, rsa array not idle\n");
			return 1;
		}
	}
	mmreg_write(0x1824, dw2);
	mmreg_write(0x1828, dw3);
	mmreg_write(0x182c, slot + 1);

	return 0;
}

static void decrypt_load_keyslot(u8 slot, u8 *pkey, u32 len)
{
	u32 temp;

    if(dma_wait_idle())
        return;

	debug("from 0x%x to slot[%d]\n", (unsigned)pkey, slot);

	mmreg_write(0x0c84, 0);
	mmreg_write(0x0c80, (u32)pkey);
	mmreg_write(0x0c8c, 0);
	mmreg_write(0x0c88, slot);

	/* AES_ECB  */
	mmreg_write(0x0c94, (1 << 30));
	mmreg_write(0x0cd0, (4 << 16));

	temp = len << 0;
	temp |= (3 << 24); /* FB to FB */
	temp |= (2 << 20); /* decryption */
	temp |= (3 << 16); /* AES */

	mmreg_write(0x0c90, temp);

	while(mmreg_read(0x0c34) & 4); 
}

static int load_keystore(u8 num_of_keys, u8 index, u8 *data)
{
	int i, j;
#ifdef CONFIG_OTP_KEYLOAD
		/* Fixed the key endianess */
		u8 new[16];
		for (i=0; i<num_of_keys; i++) {
			for (j=0; j<16; j++)
				new[j] = data[15 - j + i * 16];
			memcpy((u8 *)((u32)data + i * 16), new, 16);
		}
		flush_dcache_range(CFG_SCRATCH_ADDR, ALIGN_CACHE_SIZE(CFG_SCRATCH_ADDR + num_of_keys * 16));
		CP15DSB;
		/* load keyslot */
		decrypt_load_keyslot(index, data, num_of_keys * 16);
#else
	/* every 2 slots hold a 128 bits AES key */
	for (i=0; i<(num_of_keys << 1); i+=2)
		if(load_keyslot(index + i, (u8 *)((u32)data + (i << 3)))) 
			return 1;
#endif		
	return 0;
}

static int do_load_keys (cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	u32 bus = CONFIG_SF_DEFAULT_BUS;
	u32 cs = CONFIG_SF_DEFAULT_CS;
	u32 speed = CONFIG_SF_DEFAULT_SPEED;
	u32 mode = CONFIG_SF_DEFAULT_MODE;

	u32 cert_addr = CONFIG_CERT_OFFSET;
	u32 cert_len = CONFIG_CERT_LENGTH;
	u32 key_addr = CONFIG_KEY_OFFSET; 
	u32 key_len = CONFIG_KEY_LENGTH;

	u8 sig[256];
	u32 temp;
	char *endp;
	void *buf, *ptr;
	struct spi_flash *flash;
	int ret = 0;
	int i;

	if (argc > 1)
		return CMD_RET_USAGE;


	flash = spi_flash_probe(bus, cs, speed, mode);
	if (!flash) {
		printf("Failed to initialize SPI flash at %u:%u\n", bus, cs);
		return 1;
	}


	/* key load */
	invalidate_dcache_range(CFG_SCRATCH_ADDR, ALIGN_CACHE_SIZE(CFG_SCRATCH_ADDR + key_len));
#ifdef CONFIG_OTP_KEYLOAD
	ptr = (char *)CFG_SCRATCH_ADDR;
	for (i=0; i < (CONFIG_SCSA_KEY_CNT * 2); i++) {
		ret = get_OTP_block_value(CONFIG_SCSA_KEY_SRC + i, (u8 *)((u32)ptr + i * 8));
		if(ret) {
			printf("Failed to read OTP block %d\n", i);
			goto exit;
		}
		u64 *hack;
		hack = (u64 *)((u32)ptr + i * 8);
		debug("OTP block[%d] = 0x%016llx\n", CONFIG_SCSA_KEY_SRC + i, *hack);
	}
#else
	ret = spi_flash_read(flash, key_addr, key_len, (void *)CFG_SCRATCH_ADDR);
	if (ret) {
		printf("%s read SPI Flash failed\n", __func__);
		goto exit;
	}
#endif
	flush_dcache_range(CFG_SCRATCH_ADDR, ALIGN_CACHE_SIZE(CFG_SCRATCH_ADDR + key_len));
	CP15DSB;
	ret = load_keystore(CONFIG_SCSA_KEY_CNT, CONFIG_SCSA_KEY_BASE, (u8 *)CFG_SCRATCH_ADDR);
	CP15DSB;
	dma_sync();

	/* Certification blob load */
	debug("read cert data header from 0x%x\n", cert_addr);
	invalidate_dcache_range(0x200000, ALIGN_CACHE_SIZE(0x200000 + cert_len));
	invalidate_dcache_range(0x300000, ALIGN_CACHE_SIZE(0x300000 + cert_len));
	ret = spi_flash_read(flash, cert_addr, 64, (void *)0x200000);
	if (ret) {
		printf("Read SPI Flash failed\n");
		goto exit;
	}

	CP15DSB;
	flush_dcache_range(0x200000, 0x200040);
	CP15DSB;
    crypto_aes_ecb_128((u8 *)0x200000, (u8 *)0x300000, 0x40, CRYPT_OP_DEC, CONFIG_FLASH_KEY_SLOT);
	CP15DSB;
	invalidate_dcache_range(0x300000, 0x300040);

	ptr = (char *)0x300000;
	if(memcmp(ptr, "SCSA", 4)) {
		printf("Invalid CA cerficates magic\n");
		ret = 1;
		goto exit;
	}

	ptr = (void *)0x300004;
	temp = *(u32 *)ptr;
	temp = reverse_endian(temp);
	printf("CA certificates location 0x%x, size 0x%x\n", temp, cert_len);
	mmreg_write(CG_DUMMY_REG2, temp);

	invalidate_dcache_range(temp, ALIGN_CACHE_SIZE(temp + cert_len));
	ret = spi_flash_read(flash, cert_addr, cert_len, (void *)temp);
	if (ret) {
		printf("SPI Flash read fail\n");
		goto exit;
	}

	memcpy((u8 *)sig, (u8 *)(temp + cert_len - 256), 256);
	CP15DSB;
	flush_dcache_range(temp, ALIGN_CACHE_SIZE(temp + cert_len));
	CP15DSB;
	/* decrypt the certification blob except the signature */
    crypto_aes_ecb_128((u8 *)temp, (u8 *)temp, cert_len - 256, CRYPT_OP_DEC, CONFIG_FLASH_KEY_SLOT);
	CP15DSB;
	invalidate_dcache_range(temp, ALIGN_CACHE_SIZE(temp + cert_len));
	CP15DSB;

	calc_sig((const u32*)sig);
	u32 len = cert_len - 256;
	len += pad_for_SHA(temp, len);
	if (check_hash(temp, len))
		ret = 0;
	else
		ret = 1;
exit:	
	spi_flash_free(flash);
	return ret;
}

U_BOOT_CMD(
			loadkeys, 2, 1, do_load_keys,
			"initiate certificates and keys",
			"address"
		);
#endif //CONFIG_SCSA
