# tls_engine.c is compiled from the engine checkout (see ../CMakeLists.txt). The headers it includes come from
# there too; stubs.c supplies the two functions it calls outside mbedTLS and IDF.
if(DEFINED ENV{ENGINE_DIR})
    set(ENGINE_DIR $ENV{ENGINE_DIR})
else()
    set(ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/../engine)
endif()
set(BRIDGE ${ENGINE_DIR}/software/wifi/raw_u64/main)
idf_component_register(SRCS "app.c" "engine_task.c" "net.c" "stubs.c" "http.c" "${BRIDGE}/tls_engine.c"
                       INCLUDE_DIRS "." "${BRIDGE}" "${ENGINE_DIR}/software/io/uart" "../certs"
                       EMBED_FILES "../certs/ca.der")
if(DEFINED ENV{ENGINE_MUTANT})
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ENGINE_MUTANT="$ENV{ENGINE_MUTANT}")
endif()
