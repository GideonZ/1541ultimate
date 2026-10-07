set(HTTPS_CORE "../../../network/https")
idf_component_register(
    SRCS "${HTTPS_CORE}/tls_stream.c" "${HTTPS_CORE}/https_client.c" "${HTTPS_CORE}/tls_wire.c" "${HTTPS_CORE}/tls_service.c"
    INCLUDE_DIRS "${HTTPS_CORE}"
    REQUIRES mbedtls
)
