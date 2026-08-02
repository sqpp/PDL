#ifndef PDL_LINUX_POCSAG_DECRYPT_H
#define PDL_LINUX_POCSAG_DECRYPT_H

#ifdef __linux__

/* Try to decrypt a POCSAG message encrypted with pocsag-golang (AES-256-CTR, Base64, CRC32).
 * encrypted_b64: Base64-encoded ciphertext (IV prepended in payload).
 * out_plain: output buffer for decrypted message.
 * out_size: size of out_plain.
 * key: null-terminated passphrase (hashed with SHA256 for AES-256).
 * Returns true if decryption and CRC verification succeeded; then out_plain contains the message. */
int pocsag_try_decrypt(const char *encrypted_b64, char *out_plain, unsigned int out_size, const char *key);

#endif
#endif
