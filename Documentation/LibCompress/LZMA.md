# LZMA Decompression

## Architecture

LZMA2 decompression is implemented via the `Compress::LZMA::LZMA2InputStream`.
The stream is responsible for decoding the chunk headers and directing the
`Compress::LZMA::LZMADecoder` on what chunk to read. The `LZMADecoder` contains
both and `LZDecoder` and `RangeDecoder`.

For decoding/decompression, the data flow is like this:

`InputStream` => `RangeDecoder` => `LZDecoder` => `LZMA2InputStream` => (output) 

There are two types of chunks, **uncompressed chunks** and **LZMA chunks**.
For uncompressed chunks, `LZMA2InputStream` calls `LZMADecoder::copy_uncompressed_from_input`, 