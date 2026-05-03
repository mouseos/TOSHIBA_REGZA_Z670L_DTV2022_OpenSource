#ifndef UBOOTAPI_H_
#define UBOOTAPI_H_
#include <asm/types.h>

//#define DEBUG

#ifdef ARC_FIRMWARE
#define UCV                 _Uncached volatile
#define UC                  _Uncached
#else
#define UCV
#define UC
#endif

typedef enum tag_MODE
{
    AES_ECB = 0, 
    AES_CBC
} MODE;

#define CRYPT_OP_NONE   0
#define CRYPT_OP_ENC    1
#define CRYPT_OP_DEC    2

// function prototypes
extern int secure_boot_verify(ulong addr, u32 tlen);

extern int secure_boot_decrypt(image_header_t *hdr, ulong addr);

extern int AESOperation(const u8 encrypt, const MODE mode, const u8* pData, u32 dataLen, const u8* pOutput, const u32 keyHandle, const u32 IVHandle, const u8* const pIV);

extern void calc_sig(UCV const u32* const sig);

extern u32 pad_for_SHA(u8* const buf, const u32 len);

extern u32 check_hash(const u8* const p, const u32 len);

extern int decrypt_image(u8* src, u8* dst, u32 size, u32 cw_idx, u32 iv_idx);

extern int crypto_aes_ecb_128(u8* src, u8* dst, u32 size, u32 enc, u32 cw_idx);
#endif /*UBOOTAPI_H_*/
