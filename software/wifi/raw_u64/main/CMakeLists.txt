set(EXTERNAL "../../../io/uart")
idf_component_register(SRCS "main.c" "my_uart.c" "rpc_dispatch.c" "dump_hex.c" "wifi_modem.c" "sntp.c" ${EXTERNAL}/cmd_buffer.c
                    INCLUDE_DIRS "." ${EXTERNAL})