if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(REQUIRES mbedtls esp_eth esp_netif esp_event esp_timer esp_hw_support lwip heap)
    return()
endif()
get_filename_component(SW "../../../../software" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")
if(NOT EXISTS "${TLS_STREAM_ROOT}/src/tls_stream.c" OR NOT EXISTS "${FIXTURE_DIR}/fixture.h")
    message(FATAL_ERROR "Supply frozen TLS_STREAM_ROOT and per-run FIXTURE_DIR")
endif()
set(PROBE_SOURCE "${SW}/network/tests/provider_runtime.cc")
if(PROVIDER_TASK_PROBE)
    set(PROBE_SOURCE "${SW}/network/tests/provider_task_runtime.cc" "${SW}/u64ctrl/main/provider_control_task.cc")
endif()
idf_component_register(SRCS ${PROBE_SOURCE}
    "${SW}/network/tests/provider_runtime_network.c"
    "${SW}/network/provider_control.cc" "${SW}/network/provider_session_owner.cc"
    "${SW}/network/provider_data.cc" "${SW}/network/provider_tls_backend.cc"
    "${SW}/network/tls_http_connection.cc" "${SW}/u64ctrl/main/provider_tls_platform.cc"
    "${TLS_STREAM_ROOT}/src/tls_stream.c" "${TLS_STREAM_ROOT}/platform/lwip_dns.c"
    INCLUDE_DIRS "${SW}/network" "${SW}/infra" "${SW}/u64ctrl/main"
        "${TLS_STREAM_ROOT}/include" "${TLS_STREAM_ROOT}/platform" "${FIXTURE_DIR}" "${SW}/io/uart"
    REQUIRES mbedtls esp_eth esp_netif esp_event esp_timer esp_hw_support lwip heap)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
if(PROVIDER_TASK_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_ULTIMATE_PROVIDER_CONTROL=1 CONFIG_ULTIMATE_PROVIDER_TLS=1)
endif()
