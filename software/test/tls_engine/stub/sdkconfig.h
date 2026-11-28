/* Host stand-in for the ESP-IDF sdkconfig: what tls_engine.c checks, set as the module's sdkconfig sets it (TLS 1.3 and 1.2,
 * time and date, no certificate bundle: the test pins keys). */
#define CONFIG_MBEDTLS_SSL_PROTO_TLS1_3 1
#define CONFIG_MBEDTLS_SSL_PROTO_TLS1_2 1
#define CONFIG_MBEDTLS_HAVE_TIME_DATE   1
#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 0
