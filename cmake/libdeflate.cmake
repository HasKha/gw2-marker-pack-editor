include_guard()
include(FetchContent)

# Inflating archive entries, which miniz's own inflater does several times slower (pack/zip.cpp).
# miniz still reads the archive's directory and writes archives. Decompression only, since nothing
# here compresses; gzip support is on only because it is what builds libdeflate_crc32.
FetchContent_Declare(
    libdeflate
    GIT_REPOSITORY https://github.com/ebiggers/libdeflate.git
    GIT_TAG v1.26
)

set(LIBDEFLATE_BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_GZIP OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_COMPRESSION_SUPPORT OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_ZLIB_SUPPORT OFF CACHE BOOL "" FORCE)
set(LIBDEFLATE_GZIP_SUPPORT ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(libdeflate)
