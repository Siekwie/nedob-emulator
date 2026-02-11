/* tiny-AES-c - CTR mode for NCCH decryption */
#ifndef _AES_H_
#define _AES_H_

#include <stddef.h>

#ifndef CBC
#define CBC 0
#endif
#ifndef ECB
#define ECB 1
#endif
#ifndef CTR
#define CTR 1
#endif

#define AES128 1
#define AES_BLOCKLEN 16
#define AES_KEYLEN 16
#define AES_keyExpSize 176

struct AES_ctx {
    unsigned char RoundKey[AES_keyExpSize];
#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
    unsigned char Iv[AES_BLOCKLEN];
#endif
};

void AES_init_ctx(struct AES_ctx* ctx, const unsigned char* key);
#if (defined(CBC) && (CBC == 1)) || (defined(CTR) && (CTR == 1))
void AES_init_ctx_iv(struct AES_ctx* ctx, const unsigned char* key, const unsigned char* iv);
void AES_ctx_set_iv(struct AES_ctx* ctx, const unsigned char* iv);
#endif

#if defined(ECB) && (ECB == 1)
void AES_ECB_encrypt(const struct AES_ctx* ctx, unsigned char* buf);
#endif

#if defined(CTR) && (CTR == 1)
void AES_CTR_xcrypt_buffer(struct AES_ctx* ctx, unsigned char* buf, size_t length);
#endif

#endif
