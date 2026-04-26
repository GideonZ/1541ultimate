set(EXTERNAL "../../io/uart")
idf_component_register(SRCS "button_handler.c" "control_main.c" "my_uart.c" "dump_hex.c" "rpc_dispatch.c" "wifi_modem.c" "button_handler.c" "jtag.c" "sntp.c" ${EXTERNAL}/cmd_buffer.c
                       INCLUDE_DIRS "." ${EXTERNAL})
