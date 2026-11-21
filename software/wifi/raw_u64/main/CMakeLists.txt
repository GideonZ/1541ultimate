set(EXTERNAL "../../../io/uart")
set(COMMON "../../common")
idf_component_register(SRCS "main.c" "my_uart.c" "rpc_dispatch.c" "dump_hex.c" "wifi_modem.c" "sntp.c" ${EXTERNAL}/cmd_buffer.c ${COMMON}/wifi_radio.c ${COMMON}/wifi_radio_esp.c ${COMMON}/radio_boot.c
                    INCLUDE_DIRS "." ${EXTERNAL} ${COMMON})