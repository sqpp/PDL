/*
 * POCSAG decryption compatible with pocsag-golang (AES-256-CTR, Base64, CRC32).
 * See: https://github.com/.../pocsag-golang encryption.go
 */
#ifdef __linux__

#include "linux/pocsag_decrypt.h"
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#define AES_BLOCK_SIZE 16
#define KEY_SIZE_256   32

static int base64_decode(const char *in, unsigned char *out, size_t *out_len)
{
	BIO *bio, *b64;
	size_t in_len = strlen(in);
	int dec_len;

	b64 = BIO_new(BIO_f_base64());
	if (!b64) return -1;
	BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
	bio = BIO_new_mem_buf(in, (int)in_len);
	if (!bio) { BIO_free(b64); return -1; }
	bio = BIO_push(b64, bio);

	dec_len = BIO_read(bio, out, (int)in_len);
	BIO_free_all(bio);
	if (dec_len <= 0) return -1;
	*out_len = (size_t)dec_len;
	return 0;
}

static unsigned long crc32_ieee(const unsigned char *data, size_t len)
{
	unsigned long crc = 0xFFFFFFFF;
	static unsigned long table[256];
	static int inited = 0;

	if (!inited) {
		for (unsigned long i = 0; i < 256; i++) {
			unsigned long c = i;
			for (int k = 0; k < 8; k++) {
				if (c & 1) c = 0xEDB88320u ^ (c >> 1);
				else c = c >> 1;
			}
			table[i] = c;
		}
		inited = 1;
	}

	for (size_t i = 0; i < len; i++) {
		crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	}
	return crc ^ 0xFFFFFFFF;
}

int pocsag_try_decrypt(const char *encrypted_b64, char *out_plain, unsigned int out_size, const char *key)
{
	unsigned char key_bin[KEY_SIZE_256];
	unsigned char *decoded = NULL;
	size_t decoded_len;
	EVP_CIPHER_CTX *ctx = NULL;
	int outl, ok = 0;
	unsigned long expected_crc, actual_crc;
	size_t msg_len;
	const unsigned char *iv_ptr;
	unsigned char *cipher_ptr;
	size_t cipher_len;

	if (!encrypted_b64 || !out_plain || out_size == 0 || !key || !key[0])
		return 0;

	/* Reasonable upper bound for Base64 decoded size */
	size_t b64_len = strlen(encrypted_b64);
	if (b64_len > 8192) return 0;
	decoded = (unsigned char *)malloc(b64_len + 1);
	if (!decoded) return 0;

	if (base64_decode(encrypted_b64, decoded, &decoded_len) != 0) {
		free(decoded);
		return 0;
	}

	if (decoded_len < AES_BLOCK_SIZE + 1) {
		free(decoded);
		return 0;
	}

	/* Key: SHA256(passphrase) for AES-256 */
	SHA256((const unsigned char *)key, strlen(key), key_bin);

	iv_ptr = decoded;
	cipher_ptr = decoded + AES_BLOCK_SIZE;
	cipher_len = decoded_len - AES_BLOCK_SIZE;

	ctx = EVP_CIPHER_CTX_new();
	if (!ctx) goto done;

	if (EVP_DecryptInit_ex(ctx, EVP_aes_256_ctr(), NULL, key_bin, iv_ptr) != 1)
		goto done;
	if (EVP_DecryptUpdate(ctx, (unsigned char *)out_plain, &outl, cipher_ptr, (int)cipher_len) != 1)
		goto done;
	msg_len = (size_t)outl;
	if (EVP_DecryptFinal_ex(ctx, (unsigned char *)out_plain + outl, &outl) != 1)
		goto done;
	msg_len += (size_t)outl;
	out_plain[msg_len] = '\0';

	/* pocsag-golang format: message + \0 + 8 hex chars (CRC32) */
	if (msg_len < 9) goto done;
	if (out_plain[msg_len - 9] != '\0') goto done;

	expected_crc = crc32_ieee((const unsigned char *)out_plain, msg_len - 9);
	if (sscanf((const char *)&out_plain[msg_len - 8], "%8lx", &actual_crc) != 1)
		goto done;
	if (expected_crc != actual_crc) goto done;

	/* Strip CRC and null; keep only message */
	if (msg_len - 9 >= out_size) goto done;
	out_plain[msg_len - 9] = '\0';
	ok = 1;

done:
	if (ctx) EVP_CIPHER_CTX_free(ctx);
	free(decoded);
	return ok;
}

#endif
