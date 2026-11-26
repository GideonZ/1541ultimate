/* MBEDTLS_USER_CONFIG_FILE for the host build of the engine test: Mbed TLS 4.1.1's default configuration, with the one
 * option tls_engine.c refuses ("Renegotiation must stay off") that the default turns on. Library and test must be
 * built with the same file. */
#undef MBEDTLS_SSL_RENEGOTIATION
