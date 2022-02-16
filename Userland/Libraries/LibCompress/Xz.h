/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>
#include <AK/Endian.h>
#include <AK/OwnPtr.h>
#include <AK/ProxiedStream.h>
#include <AK/Stream.h>
#include <LibCompress/LZMA/LZMA2InputStream.h>
#include <LibCrypto/Checksum/CRC32.h>

// NOTE: The xz file format specification is avaliable at: https://tukaani.org/xz/xz-file-format.txt

namespace Compress {

class BlockChecksum {
public:
    virtual ~BlockChecksum() { }
    virtual void update(ReadonlyBytes) = 0;
    virtual ErrorOr<ByteBuffer> digest() const = 0;
};

class DummyBlockChecksum : public BlockChecksum {
public:
    void update(ReadonlyBytes) override { }
    ErrorOr<ByteBuffer> digest() const override { return ByteBuffer {}; }
};

class CRC32BlockChecksum : public BlockChecksum {
    void update(ReadonlyBytes) override;
    ErrorOr<ByteBuffer> digest() const override;

private:
    Crypto::Checksum::CRC32 m_impl;
};

// 1.2. Multibyte Integers
class XzMultibyteInteger {
public:
    friend InputStream& operator>>(InputStream&, XzMultibyteInteger&);

    constexpr XzMultibyteInteger() = default;

    constexpr operator u64() const { return m_value; }

private:
    u64 m_value { 0 };
};

InputStream& operator>>(InputStream&, XzMultibyteInteger&);

// 2.1.1. Stream Header
struct [[gnu::packed]] StreamHeader {
    u8 magic[6] { 0 };
    u8 flags_first_byte { 0 };
    u8 flags_second_byte { 0 };
    LittleEndian<u32> crc32 { 0 };

    bool verify();
};

// 2.1.2. Stream Footer
struct [[gnu::packed]] StreamFooter {
    LittleEndian<u32> crc32 { 0 };
    LittleEndian<u32> backward_size { 0 };
    u8 flags_first_byte { 0 };
    u8 flags_second_byte { 0 };
    u8 magic[2] { 0 };

    bool verify();
};

InputStream& operator>>(InputStream&, StreamHeader&);
InputStream& operator>>(InputStream&, StreamFooter&);

static_assert(sizeof(StreamHeader) == 12);
static_assert(sizeof(StreamFooter) == 12);

// 3.1. Block Header
struct BlockHeader {
    Optional<u64> compressed_size;
    Optional<u64> uncompressed_size;
    u8 lzma_filter_properties;

    static ErrorOr<size_t> decode_size(u8 encoded_size);
    static ErrorOr<BlockHeader> read_from_buffer(ReadonlyBytes);
};

class XzDecompressor final : public InputStream {
public:
    XzDecompressor(InputStream& stream)
        : m_input_stream(stream)
    {
    }
    ~XzDecompressor() { }

    size_t read(Bytes) override;
    bool read_or_error(Bytes) override { TODO(); }
    bool discard_or_error(size_t) override { TODO(); }

    bool unreliable_eof() const override { return m_state == State::EndOfFile; }
    bool handle_any_error() override { return false; }

private:
    // This is being wrapped by size_t read(Bytes), and exists so that
    // we can freely use ErrorOr<T> inside.
    ErrorOr<size_t> try_read(Bytes);

    ErrorOr<NonnullOwnPtr<BlockChecksum>> create_block_checksum() const;

    enum class State {
        Initial,
        Idle,
        InBlockData,
        AfterBlockData,
        AfterIndex,
        EndOfFile
    };

    struct BlockState {
        CountedInputStream counted_input_stream;
        LZMA::LZMA2InputStream lzma2_input_stream;
        NonnullOwnPtr<BlockChecksum> checksum;

        BlockState(InputStream&, u8 lzma_properties_byte, NonnullOwnPtr<BlockChecksum>);
    };

    InputStream& m_input_stream;

    State m_state { State::Initial };

    Optional<StreamHeader> m_stream_header;
    Optional<StreamFooter> m_stream_footer;

    Optional<BlockHeader> m_block_header;
    Optional<BlockState> m_block_state;
};

}
