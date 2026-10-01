# tls_engine.c is compiled unmodified from the pinned checkout (see ../CMakeLists.txt). The headers it includes
# come from there too; stubs.c supplies the two functions it calls outside mbedTLS and IDF.
get_filename_component(ENGINE_DIR ${CMAKE_CURRENT_LIST_DIR}/../engine REALPATH)
set(BRIDGE ${ENGINE_DIR}/software/wifi/raw_u64/main)
idf_component_register(SRCS "app.c" "engine_task.c" "net.c" "stubs.c" "${BRIDGE}/tls_engine.c"
                       INCLUDE_DIRS "." "${BRIDGE}" "${ENGINE_DIR}/software/io/uart" "../certs"
                       EMBED_FILES "../certs/ca.der")
