/* Pegaso: Windows threading primitives for Mbed TLS (MBEDTLS_THREADING_ALT).
 * Copied by tools/build-deps.ps1 into mbedtls/tf-psa-crypto/include/ so that both
 * the library and the application see the same definitions.
 * The functions operating on these types live in srchybrid/TLSthreading.cpp. */
#ifndef PEGASO_THREADING_ALT_H
#define PEGASO_THREADING_ALT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef struct mbedtls_platform_mutex_t {
    CRITICAL_SECTION cs;
    char is_valid;
} mbedtls_platform_mutex_t;

/* Condition variables are not used by the code paths the application exercises;
 * the callbacks are no-ops, so the object carries no state. */
typedef struct mbedtls_platform_condition_variable_t {
    char unused;
} mbedtls_platform_condition_variable_t;

#endif /* PEGASO_THREADING_ALT_H */
