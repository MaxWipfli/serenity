set(SOURCES
    Deflate.cpp
    Gzip.cpp
    LZMA/LZMA2InputStream.cpp
    LZMA/LZMADecoder.cpp
    LZMA/RangeDecoder.cpp
    Xz.cpp
    Zlib.cpp
)

serenity_lib(LibCompress compress)
target_link_libraries(LibCompress LibC LibCrypto)
