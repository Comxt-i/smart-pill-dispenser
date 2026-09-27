#pragma once
#include <stddef.h>
struct mbedtls_md_info_t {};
constexpr int MBEDTLS_MD_SHA256 = 1;
inline const mbedtls_md_info_t *mbedtls_md_info_from_type(int) { static mbedtls_md_info_t info; return &info; }
inline int mbedtls_md_hmac(const mbedtls_md_info_t *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char *out) { for (int i=0;i<32;i++) out[i]=i; return 0; }
