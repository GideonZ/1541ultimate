set(EXTERNAL "../../io/uart")
set(COMMON "../../wifi/common")
idf_component_register(SRCS "button_handler.c" "control_main.c" "my_uart.c" "dump_hex.c" "rpc_dispatch.c" "wifi_modem.c" "button_handler.c" "jtag.c" "sntp.c" "power_state.c" "wol_magic.c" "recovery_manifest.c" "recovery_verify.c" "recovery_image.c" "recovery_url.c" "recovery.c" "recovery_trust.c" ${EXTERNAL}/cmd_buffer.c ${COMMON}/wifi_radio.c ${COMMON}/wifi_radio_esp.c
                       INCLUDE_DIRS "." ${EXTERNAL} ${COMMON}
                    LDFRAGMENTS "linker.lf")
