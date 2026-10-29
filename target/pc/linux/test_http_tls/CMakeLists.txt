cmake_minimum_required(VERSION 3.20)
project(http_tls_adapter_test C CXX)
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TLS_STREAM_ROOT "" CACHE PATH "Frozen standalone TLS source with pinned .deps/mbedtls")
if(NOT EXISTS "${TLS_STREAM_ROOT}/src/tls_stream.c")
    message(FATAL_ERROR "Supply TLS_STREAM_ROOT; see README.md for the validated revision")
endif()
option(HTTP_TLS_SANITIZE "Instrument adapter, parser and TLS library" ON)
if(HTTP_TLS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined -fno-sanitize-recover=all)
endif()
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(${TLS_STREAM_ROOT}/.deps/mbedtls mbedtls EXCLUDE_FROM_ALL)
get_filename_component(SW "${CMAKE_CURRENT_SOURCE_DIR}/../../../../software" ABSOLUTE)
set(HTTP_TEST_SOURCES
    ${SW}/network/tests/http_target_test_support.cc
    ${SW}/io/command_interface/http_target.cc
    ${SW}/network/http_request.cc
    ${SW}/api/json.cc ${SW}/components/mystring.cc ${SW}/components/pattern.cc
    ${SW}/system/small_printf.cc
    ${SW}/httpd/c-version/lib/http_protocol.c
    ${SW}/httpd/c-version/lib/multipart.c
    ${SW}/httpd/c-version/lib/url.c)
add_executable(test_http_tls ${HTTP_TEST_SOURCES}
    ${SW}/network/tests/test_http_tls.cc
    ${SW}/network/tls_http_connection.cc
    ${SW}/network/owner_http_provider.cc
    ${SW}/network/provider_tls_backend.cc
    ${TLS_STREAM_ROOT}/src/tls_stream.c)
add_executable(test_https_routing ${HTTP_TEST_SOURCES} ${SW}/network/tests/test_https_routing.cc)
add_executable(test_command_cancellation ${SW}/network/tests/test_command_cancellation.cc
    ${SW}/io/command_interface/command_intf.cc)
target_include_directories(test_command_cancellation PRIVATE
    ${SW}/network/tests/command_stubs ${SW}/io/command_interface ${SW}/infra)
target_compile_options(test_command_cancellation PRIVATE -Wall -Wextra -Wno-format)
add_executable(test_provider_lifecycle ${SW}/network/tests/test_provider_lifecycle.cc)
add_executable(test_provider_owner ${SW}/network/tests/test_provider_owner.cc ${SW}/network/provider_session_owner.cc)
target_include_directories(test_provider_owner PRIVATE ${SW}/network)
target_compile_options(test_provider_owner PRIVATE -Wall -Wextra -Werror)
target_sources(test_http_tls PRIVATE ${SW}/network/provider_session_owner.cc ${SW}/network/provider_control.cc)
add_executable(test_provider_control ${SW}/network/tests/test_provider_control.cc
    ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc)
target_include_directories(test_provider_control PRIVATE ${SW}/network ${SW}/infra)
target_compile_options(test_provider_control PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_control PRIVATE pthread)
add_executable(test_provider_manager ${SW}/network/tests/test_provider_manager.cc
    ${SW}/network/provider_manager.cc ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc)
add_executable(test_provider_provisioning ${SW}/network/tests/test_provider_provisioning.cc
    ${SW}/network/provider_manager.cc ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc)
target_include_directories(test_provider_provisioning PRIVATE ${SW}/network ${SW}/infra)
target_compile_options(test_provider_provisioning PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_provisioning PRIVATE pthread)
add_executable(test_provider_flash ${SW}/network/tests/test_provider_flash_journal.cc ${SW}/network/provider_flash_journal.cc)
target_include_directories(test_provider_flash PRIVATE ${SW}/network ${SW}/infra ${SW}/io/flash ${SW}/components ${SW}/system)
target_compile_definitions(test_provider_flash PRIVATE ENTER_SAFE_SECTION= LEAVE_SAFE_SECTION=)
target_compile_options(test_provider_flash PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
add_executable(test_provider_sources ${SW}/network/tests/test_provider_sources.cc ${SW}/u64ctrl/main/provider_sources.cc)
target_include_directories(test_provider_sources PRIVATE ${SW}/network/tests/source_stubs ${SW}/network ${SW}/u64ctrl/main)
target_compile_options(test_provider_sources PRIVATE -Wall -Wextra -Werror)
target_include_directories(test_provider_manager PRIVATE ${SW}/network ${SW}/infra)
target_compile_options(test_provider_manager PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_manager PRIVATE pthread)
target_sources(test_http_tls PRIVATE ${SW}/network/provider_manager.cc)
target_sources(test_http_tls PRIVATE ${SW}/network/provider_manager_delivery.cc ${SW}/network/provider_manager_runner.cc)
add_executable(test_provider_delivery ${SW}/network/tests/test_provider_delivery.cc
    ${SW}/network/provider_manager_delivery.cc ${SW}/io/wifi/provider_manager_uart.cc
    ${SW}/network/provider_manager.cc ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc)
target_include_directories(test_provider_delivery PRIVATE ${SW}/network/tests/manager_stubs
    ${SW}/network ${SW}/infra ${SW}/io/wifi)
target_compile_options(test_provider_delivery PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_delivery PRIVATE pthread)
add_executable(test_provider_runner ${SW}/network/tests/test_provider_runner.cc
    ${SW}/network/provider_manager_runner.cc ${SW}/network/provider_manager_delivery.cc
    ${SW}/io/wifi/provider_manager_uart.cc ${SW}/network/provider_manager.cc
    ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc)
target_include_directories(test_provider_runner PRIVATE ${SW}/network/tests/manager_stubs
    ${SW}/network ${SW}/infra ${SW}/io/wifi)
target_compile_options(test_provider_runner PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_runner PRIVATE pthread)
add_executable(test_provider_task ${SW}/network/tests/test_provider_task.cc
    ${SW}/network/provider_manager_task.cc ${SW}/network/provider_manager_runner.cc
    ${SW}/network/provider_manager_delivery.cc ${SW}/io/wifi/provider_manager_uart.cc
    ${SW}/network/provider_manager.cc ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc)
target_include_directories(test_provider_task PRIVATE ${SW}/network/tests/owner_stubs
    ${SW}/network/tests/manager_stubs ${SW}/network ${SW}/infra ${SW}/io/wifi)
target_compile_options(test_provider_task PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_task PRIVATE pthread)
foreach(provider_target test_http_tls test_provider_delivery test_provider_runner test_provider_task)
    target_sources(${provider_target} PRIVATE ${SW}/network/provider_data.cc)
endforeach()
foreach(provider_target test_http_tls test_provider_runner test_provider_task)
    target_sources(${provider_target} PRIVATE ${SW}/network/provider_data_client.cc)
endforeach()
add_executable(test_provider_data ${SW}/network/tests/test_provider_data.cc ${SW}/network/provider_data.cc
    ${SW}/network/provider_control.cc ${SW}/network/provider_session_owner.cc ${SW}/network/provider_data_client.cc
    ${SW}/network/provider_manager.cc ${SW}/network/provider_manager_delivery.cc ${SW}/network/provider_manager_runner.cc)
target_include_directories(test_provider_data PRIVATE ${SW}/network ${SW}/infra)
target_compile_options(test_provider_data PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_provider_data PRIVATE pthread)
add_executable(test_owner_http ${SW}/network/tests/test_owner_http.cc ${SW}/network/owner_http_provider.cc)
target_include_directories(test_owner_http PRIVATE ${SW}/network ${SW}/infra)
target_compile_options(test_owner_http PRIVATE -Wall -Wextra -Werror)
add_executable(test_uart_loss ${SW}/network/tests/test_uart_loss.cc ${SW}/io/uart/dma_uart.cc)
target_include_directories(test_uart_loss PRIVATE ${SW}/network/tests/uart_stubs ${SW}/io/uart)
target_compile_definitions(test_uart_loss PRIVATE RISCV=1)
target_compile_options(test_uart_loss PRIVATE -Wall -Wextra -Wno-format -Wno-unused-variable)
target_include_directories(test_provider_lifecycle PRIVATE ${SW}/io/wifi ${SW}/infra)
add_executable(test_provider_boot ${SW}/network/tests/test_provider_boot.c ${SW}/u64ctrl/main/provider_boot.c)
target_include_directories(test_provider_boot PRIVATE ${SW}/network/tests/command_stubs ${SW}/u64ctrl/main)
foreach(test_target test_http_tls test_https_routing)
target_include_directories(${test_target} PRIVATE
    ${SW}/network ${SW}/api ${SW}/infra ${SW}/components ${SW}/io/stream ${SW}/system ${SW}/io/wifi
    ${SW}/io/command_interface ${SW}/httpd/c-version/lib ${TLS_STREAM_ROOT}/include)
target_compile_definitions(${test_target} PRIVATE RUNS_ON_PC MHS_PORT=8001 _DEFAULT_SOURCE)
target_compile_options(${test_target} PRIVATE -Wall -Wextra -Wno-format -Wno-format-extra-args)
endforeach()
target_link_libraries(test_http_tls PRIVATE mbedtls mbedx509 mbedcrypto pthread)
target_link_options(test_http_tls PRIVATE "LINKER:--wrap=calloc" "LINKER:--wrap=free")
target_link_options(test_https_routing PRIVATE "LINKER:--wrap=gethostbyname_r,--wrap=socket,--wrap=connect,--wrap=send,--wrap=recv,--wrap=close")
