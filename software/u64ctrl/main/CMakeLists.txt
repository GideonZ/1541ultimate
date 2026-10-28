set(EXTERNAL "../../io/uart")
set(PROVIDER_OWNER "../../network/provider_session_owner.cc")
idf_component_register(SRCS "provider_boot.c" "button_handler.c" "control_main.c" "my_uart.c" "dump_hex.c" "rpc_dispatch.c" "wifi_modem.c" "button_handler.c" "jtag.c" "sntp.c" "power_state.c" "wol_magic.c" "recovery_manifest.c" "recovery_verify.c" "recovery_image.c" "recovery_url.c" "recovery.c" "recovery_trust.c" ${EXTERNAL}/cmd_buffer.c
                       INCLUDE_DIRS "." ${EXTERNAL})
target_sources(${COMPONENT_LIB} PRIVATE ${PROVIDER_OWNER} "../../network/provider_control.cc" "provider_control_task.cc")
target_include_directories(${COMPONENT_LIB} PRIVATE "../../infra")
