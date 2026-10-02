# check_engine.cmake: cmake -DENGINE_DIR=... -DENGINE_SHA=... [-DENGINE_MUTANT=name] -P check_engine.cmake
# The engine tree must be a git checkout of ENGINE_SHA with no file changed or added (an untracked header next to
# tls_engine.c would shadow IDF's). A mutant build (tools/mutants.sh)
# may change tls_engine.c and nothing else. Runs at configure and again at every build, before main is compiled.
execute_process(COMMAND git -C ${ENGINE_DIR} rev-parse HEAD OUTPUT_VARIABLE head OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT head STREQUAL ENGINE_SHA)
    message(FATAL_ERROR "engine: ${ENGINE_DIR} is at '${head}', not ENGINE_SHA ${ENGINE_SHA} (tools/fetch_engine.sh)")
endif()
execute_process(COMMAND git -C ${ENGINE_DIR} status --porcelain --untracked-files=all OUTPUT_VARIABLE dirty
                OUTPUT_STRIP_TRAILING_WHITESPACE)
set(main ${ENGINE_DIR}/software/wifi/raw_u64/main)
file(SHA256 ${main}/tls_engine.c c_sha)
file(SHA256 ${main}/tls_engine.h h_sha)
if(ENGINE_MUTANT)
    if(NOT dirty MATCHES "^ M software/wifi/raw_u64/main/tls_engine.c$")
        message(FATAL_ERROR "engine: a mutant may change tls_engine.c only; changed: ${dirty}")
    endif()
    message(STATUS "engine: MUTANT ${ENGINE_MUTANT} of ${ENGINE_SHA}: tls_engine.c ${c_sha}")
elseif(dirty)
    message(FATAL_ERROR "engine: files differ from or are added to ${ENGINE_SHA}: ${dirty}")
else()
    message(STATUS "engine: ${ENGINE_SHA}, unmodified; tls_engine.c ${c_sha} tls_engine.h ${h_sha}")
endif()
