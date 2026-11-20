set(EXTERNAL "../../io/uart")
set(COMMON "../../wifi/common")
# The TLS engine and its RPC commands are shared with other module firmware: software/wifi/tls
set(TLS_DIR "../../wifi/tls")
idf_component_register(SRCS "button_handler.c" "control_main.c" "my_uart.c" "dump_hex.c" "rpc_dispatch.c" "wifi_modem.c" "button_handler.c" "jtag.c" "sntp.c" "power_state.c" "wol_magic.c" "recovery_manifest.c" "recovery_verify.c" "recovery_image.c" "recovery_url.c" "recovery.c" "recovery_trust.c" "${TLS_DIR}/tls_rpc.c" "${TLS_DIR}/tls_engine.c" "${TLS_DIR}/tls_rng_gate.c" ${EXTERNAL}/cmd_buffer.c ${COMMON}/wifi_radio.c ${COMMON}/wifi_radio_esp.c ${COMMON}/radio_entropy.c ${COMMON}/radio_entropy_esp.c
                       INCLUDE_DIRS "." ${TLS_DIR} ${EXTERNAL} ${COMMON}
                    LDFRAGMENTS "linker.lf")
# The TLS task's random draws hold the entropy gate (tls_rng_gate.h): every draw of mbedTLS goes through this function
target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=mbedtls_psa_external_get_random")
