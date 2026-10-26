# The certificate bundle is the vendored Mozilla root store, software/wifi/ca_store/cacert.pem. ca_store.py checks it against
# CA_PINS and the configuration this build really uses (sdkconfig.json) at every configure and again at every build,
# before the bundle is made, so an earlier configure cannot skip it. RELEASE=1 also fails a stale store. See README.md.
# Include this from a project's own CMakeLists.txt, after project(), wherever the project is. The store and the check
# are found from this file (CMAKE_CURRENT_LIST_DIR here is the store's own directory); the project being checked is
# CMAKE_SOURCE_DIR.
set(CA_STORE ${CMAKE_CURRENT_LIST_DIR})
set(CA_STORE_CHECK ${CMAKE_CURRENT_LIST_DIR}/../../../tools/ca_store/ca_store.py)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CA_STORE}/cacert.pem ${CA_STORE}/CA_PINS ${CA_STORE_CHECK})
idf_build_get_property(_python PYTHON)
idf_build_get_property(_sdkconfig_json SDKCONFIG_JSON)
set(_ca_check ${_python} ${CA_STORE_CHECK} check --project ${CMAKE_SOURCE_DIR} --sdkconfig-json ${_sdkconfig_json})
execute_process(COMMAND ${_ca_check} RESULT_VARIABLE _ca_rc)
if(NOT _ca_rc EQUAL 0)
    message(FATAL_ERROR "The Mozilla root store (software/wifi/ca_store) failed its check; see README.md")
endif()
add_custom_target(ca_store_check ALL COMMAND ${_ca_check} COMMENT "Checking the Mozilla root store" VERBATIM)
# The bundle (and so the mbedtls library and the image) waits for it
if(TARGET custom_bundle)
    add_dependencies(custom_bundle ca_store_check)
endif()
