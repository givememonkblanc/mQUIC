// client/tls_trust.c
#include "tls_trust.h"
#include "picotls.h"
#include <string.h>

static int trust_all_cb(ptls_verify_certificate_t *self,
                        ptls_t *tls,
                        const char *server_name,
                        int (**verify_sign)(void *verify_ctx, uint16_t algo,
                                            ptls_iovec_t data, ptls_iovec_t sign),
                        void **verify_data,
                        ptls_iovec_t *certs, size_t num_certs)
{
    (void)self; (void)tls; (void)server_name;
    (void)verify_sign; (void)verify_data; (void)certs; (void)num_certs;
    return 0; // DEV ONLY: 모든 인증서 허용
}

static const uint16_t TRUST_ALL_ALGOS[] = {
#ifdef PTLS_SIGNATURE_RSA_PSS_RSAE_SHA256
    PTLS_SIGNATURE_RSA_PSS_RSAE_SHA256,
#endif
#ifdef PTLS_SIGNATURE_RSA_PSS_RSAE_SHA384
    PTLS_SIGNATURE_RSA_PSS_RSAE_SHA384,
#endif
#ifdef PTLS_SIGNATURE_RSA_PKCS1_SHA256
    PTLS_SIGNATURE_RSA_PKCS1_SHA256,
#endif
#ifdef PTLS_SIGNATURE_RSA_PKCS1_SHA384
    PTLS_SIGNATURE_RSA_PKCS1_SHA384,
#endif
#ifdef PTLS_SIGNATURE_ECDSA_SECP256R1_SHA256
    PTLS_SIGNATURE_ECDSA_SECP256R1_SHA256,
#endif
#ifdef PTLS_SIGNATURE_ECDSA_SECP384R1_SHA384
    PTLS_SIGNATURE_ECDSA_SECP384R1_SHA384,
#endif
    0
};

ptls_verify_certificate_t PTLS_TRUST_ALL = {
    .cb    = trust_all_cb,
    .algos = TRUST_ALL_ALGOS
};
