set(ZLIB_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../third_party/zlib")
idf_component_register(
    SRCS "${ZLIB_DIR}/adler32.c" "${ZLIB_DIR}/crc32.c" "${ZLIB_DIR}/inflate.c"
         "${ZLIB_DIR}/inftrees.c" "${ZLIB_DIR}/inffast.c" "${ZLIB_DIR}/zutil.c"
    INCLUDE_DIRS "${ZLIB_DIR}"
)
