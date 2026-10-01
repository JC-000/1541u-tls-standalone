cmake_minimum_required(VERSION 3.16)

# The engine is compiled from a checkout of GideonZ/1541ultimate at ENGINE_SHA, reached through the symlink
# "engine". Configuring fails unless every file taken from it has the bytes pinned here.
set(ENGINE_SHA f2e46946af6f091586a24cb4c2243ef8c1b4de76)
get_filename_component(ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/engine REALPATH)
set(ENGINE_PINS
    "software/wifi/raw_u64/main/tls_engine.c=5ee04a59562245dd79bf5e7f7ee5cf4e30abb76bbfc4dc2d5e9b56d680da5361"
    "software/wifi/raw_u64/main/tls_engine.h=efabf8a2450c5f4eb7ca7a2549ef71a75e58aa191a6c41b536ea648ef333b109"
    "software/wifi/raw_u64/main/rpc_calls.h=0a3a20a2708e72a335a1d9f27d900ddb2e972b42f59d734faa2e8f231ced8acd"
    "software/wifi/raw_u64/main/wifi_modem.h=45bfc46e2c43749c76c285f271e50dc307604378ddccbfcf80919bf27e49ed0d"
    "software/wifi/raw_u64/main/sntp.h=83be2b71a543b93416fd30ba31a9ee7416b7a0bb4d46c428fa4e1469e80a3c04"
    "software/io/uart/cmd_buffer.h=5bb28c6c071bf2ad440f0edca786163c303ae8a0fed003b5411c09f61dba5ea7"
    "software/wifi/ca_store/cacert.pem=a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505"
    "software/wifi/raw_u64/sdkconfig=3c732947cfae12714863e5643aeae41d7d83ad88b306267b270849e6dd9089c2")
foreach(pin ${ENGINE_PINS})
    string(REPLACE "=" ";" pin ${pin})
    list(GET pin 0 file)
    list(GET pin 1 want)
    if(NOT EXISTS ${ENGINE_DIR}/${file})
        message(FATAL_ERROR "${ENGINE_DIR}/${file} is missing: point ENGINE_DIR at a checkout of ${ENGINE_SHA}")
    endif()
    file(SHA256 ${ENGINE_DIR}/${file} have)
    if(NOT have STREQUAL want)
        message(FATAL_ERROR "${file} is not the one at ${ENGINE_SHA} (sha256 ${have})")
    endif()
endforeach()
execute_process(COMMAND git -C ${ENGINE_DIR} rev-parse HEAD OUTPUT_VARIABLE _head
                OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
message(STATUS "Engine: ${ENGINE_DIR} (git HEAD ${_head}); pinned files match ${ENGINE_SHA}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tls_standalone)

# The bundle is the engine tree's vendored Mozilla store: sdkconfig names engine/software/wifi/ca_store/cacert.pem
# (pinned above). ca_store.py checks that store against its CA_PINS, as the firmware's build does.
idf_build_get_property(_python PYTHON)
execute_process(COMMAND ${_python} ${ENGINE_DIR}/tools/ca_store/ca_store.py check --repo-root ${ENGINE_DIR}
                RESULT_VARIABLE _ca_rc)
if(NOT _ca_rc EQUAL 0)
    message(FATAL_ERROR "The engine tree's Mozilla root store failed its check")
endif()
