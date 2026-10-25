set(EXTERNAL "../../io/uart")
set(PROVIDER_OWNER "../../network/provider_session_owner.cc")
idf_component_register(SRCS "provider_boot.c" "button_handler.c" "control_main.c" "my_uart.c" "dump_hex.c" "rpc_dispatch.c" "wifi_modem.c" "button_handler.c" "jtag.c" "sntp.c" "power_state.c" "wol_magic.c" ${EXTERNAL}/cmd_buffer.c
                       INCLUDE_DIRS "." ${EXTERNAL})
target_sources(${COMPONENT_LIB} PRIVATE ${PROVIDER_OWNER})
