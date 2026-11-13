set(EXTERNAL "../../../io/uart")
# The TLS engine and its RPC commands are shared with other module firmware: software/wifi/tls
set(TLS_DIR "../../tls")
# BRIDGE_TLS_STUB (idf.py -D BRIDGE_TLS_STUB=1) builds a size-measurement image without the TLS engine
if(BRIDGE_TLS_STUB)
    set(TLS_SRCS "${TLS_DIR}/tls_engine_stub.c")
else()
    set(TLS_SRCS "${TLS_DIR}/tls_engine.c")
endif()
set(COMMON "../../common")
idf_component_register(SRCS "main.c" "my_uart.c" "rpc_dispatch.c" "dump_hex.c" "wifi_modem.c" "sntp.c" "${TLS_DIR}/tls_rpc.c" ${TLS_SRCS} ${EXTERNAL}/cmd_buffer.c ${COMMON}/wifi_radio.c ${COMMON}/wifi_radio_esp.c ${COMMON}/radio_boot.c ${COMMON}/radio_entropy.c ${COMMON}/radio_entropy_esp.c
                    INCLUDE_DIRS "." ${TLS_DIR} ${EXTERNAL} ${COMMON}
                    LDFRAGMENTS "linker.lf")
