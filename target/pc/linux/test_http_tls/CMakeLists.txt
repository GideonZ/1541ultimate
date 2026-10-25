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
    ${TLS_STREAM_ROOT}/src/tls_stream.c)
add_executable(test_https_routing ${HTTP_TEST_SOURCES} ${SW}/network/tests/test_https_routing.cc)
add_executable(test_command_cancellation ${SW}/network/tests/test_command_cancellation.cc
    ${SW}/io/command_interface/command_intf.cc)
target_include_directories(test_command_cancellation PRIVATE
    ${SW}/network/tests/command_stubs ${SW}/io/command_interface ${SW}/infra)
target_compile_options(test_command_cancellation PRIVATE -Wall -Wextra -Wno-format)
add_executable(test_provider_lifecycle ${SW}/network/tests/test_provider_lifecycle.cc)
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
