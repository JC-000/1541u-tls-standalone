# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.16)

# The engine is compiled from a git checkout of ENGINE_SHA (engine.env), in ./engine (tools/fetch_engine.sh) or in
# $ENV{ENGINE_DIR}. tools/check_engine.cmake refuses any other commit or a changed or added file, at configure and
# at every build.
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/engine.env ENGINE_SHA REGEX "^ENGINE_SHA=")
string(REPLACE "ENGINE_SHA=" "" ENGINE_SHA "${ENGINE_SHA}")
if(DEFINED ENV{ENGINE_DIR})
    set(ENGINE_DIR $ENV{ENGINE_DIR})
else()
    set(ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/engine)
endif()
set(ENGINE_CHECK ${CMAKE_COMMAND} -DENGINE_DIR=${ENGINE_DIR} -DENGINE_SHA=${ENGINE_SHA}
    -DENGINE_MUTANT=$ENV{ENGINE_MUTANT} -P ${CMAKE_CURRENT_LIST_DIR}/tools/check_engine.cmake)
execute_process(COMMAND ${ENGINE_CHECK} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "The engine check failed")
endif()

# The app version is fixed, not the repo's git describe, so the image depends only on the sources (reproducible build)
set(PROJECT_VER "4")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(tls_standalone)

idf_build_get_property(_python PYTHON)
# The app's hand copies of firmware code (engine_task.c, take_step() in app.c) against this engine commit
set(VERIFY ${_python} -B ${CMAKE_CURRENT_LIST_DIR}/tools/verify_transcriptions.py ${ENGINE_DIR})
execute_process(COMMAND ${VERIFY} RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "The transcriptions do not match this engine commit (tools/verify_transcriptions.py)")
endif()
add_custom_target(engine_check ALL COMMAND ${ENGINE_CHECK} COMMAND ${VERIFY} VERBATIM)
add_dependencies(__idf_main engine_check)

# The bundle is the engine tree's vendored Mozilla store (sdkconfig names engine/software/wifi/ca_store/cacert.pem);
# its own ca_store.py checks it against CA_PINS, as the firmware's build does
execute_process(COMMAND ${_python} -B ${ENGINE_DIR}/tools/ca_store/ca_store.py check --repo-root ${ENGINE_DIR}
                RESULT_VARIABLE _ca_rc)
if(NOT _ca_rc EQUAL 0)
    message(FATAL_ERROR "The engine tree's Mozilla root store failed its check")
endif()
