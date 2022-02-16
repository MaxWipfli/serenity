/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/ByteBuffer.h>
#include <AK/Debug.h>
#include <AK/Endian.h>
#include <AK/Format.h>
#include <AK/MemoryStream.h>
#include <AK/ProxiedStream.h>
#include <LibCompress/Xz.h>
#include <LibCrypto/Checksum/CRC32.h>

namespace Compress {

// 2.1.1.1. Header Magic Bytes
constexpr Array<u8, 6> header_magic = { 0xFD, 0x37, 0x7A, 0x58, 0x5A, 0x00 };
// 2.1.2.4. Footer Magic Bytes
constexpr Array<u8, 2> footer_magic = { 0x59, 0x5A };

void CRC32BlockChecksum::update(ReadonlyBytes data)
{
    m_impl.update(data);
}

ErrorOr<ByteBuffer> CRC32BlockChecksum::digest() const
{
    LittleEndian<u32> digest = m_impl.digest();
    return ByteBuffer::copy(digest.bytes());
}

// 1.2. Multibyte Integers
InputStream& operator>>(InputStream& stream, XzMultibyteInteger& value)
{
    u8 byte;

    // Handle first byte.
    stream >> byte;
    value.m_value = (byte & 0x7F);

    // Handle remaining bytes.
    for (size_t i = 1; i < 9; ++i) {
        if ((byte & 0x80) == 0)
            break;
        stream >> byte;
        if (stream.has_any_error())
            break;
        // NOTE: If the last byte is zero (could have encoded with one less byte),
        // or are already on the 9th byte and it still indicates there is more to
        // come (so the MSB is 1), this is invalid.
        if (byte == 0 || (i == 8 && (byte & 0x80) > 0)) {
            dbgln("XzMultibyteInteger: Invalid end byte 0x{:02x}", byte);
            stream.set_fatal_error();
            break;
        }
        value.m_value |= (u64)(byte & 0x7f) << (i * 7);
    }

    if ((byte & 0x80) > 0) {
        dbgln("XzMultibyteInteger: Invalid end byte 0x{:02x}", byte);
        stream.set_fatal_error();
    }

    return stream;
}

static inline bool verify_stream_flags(u8 first_byte, u8 second_byte)
{
    return (first_byte == 0x00)
        && (second_byte == 0x00 || second_byte == 0x01 || second_byte == 0x04 || second_byte == 0x0A);
}

bool StreamHeader::verify()
{
    // 1. Verify magic.
    if (header_magic.span() != Span<u8> { this->magic, sizeof(this->magic) })
        return false;

    // 2. Verify valid stream flags.
    if (!verify_stream_flags(flags_first_byte, flags_second_byte))
        return false;

    // 3. Verify CRC32. The CRC32 is calculated from the "Stream Flags" field.
    Crypto::Checksum::CRC32 checksum;
    checksum.update({ &this->flags_first_byte, 1 });
    checksum.update({ &this->flags_second_byte, 1 });
    return this->crc32 == checksum.digest();
}

bool StreamFooter::verify()
{
    // 1. Verify magic
    if (footer_magic.span() != Span<u8> { this->magic, sizeof(this->magic) })
        return false;

    // 2. Verify valid stream flags.
    if (!verify_stream_flags(flags_first_byte, flags_second_byte))
        return false;

    // 3. Verify CRC32. The CRC32 is calculated from the "Backward Size" and "Stream Flags" fields.
    Crypto::Checksum::CRC32 checksum { { reinterpret_cast<u8*>(&backward_size), 6 } };
    return this->crc32 == checksum.digest();

    // FIXME: 4. Verify Backward Size.
    //        "If the stored value does not match the real size of the Index field,
    //        the decoder MUST inidicate an error." -- 2.1.2.2. Backward Size
}

InputStream& operator>>(InputStream& stream, StreamHeader& stream_header)
{
    stream.read_or_error({ &stream_header, sizeof(stream_header) });
    return stream;
}

InputStream& operator>>(InputStream& stream, StreamFooter& stream_footer)
{
    stream.read_or_error({ &stream_footer, sizeof(stream_footer) });
    return stream;
}

// 3.1.1. Block Header Size
ErrorOr<size_t> BlockHeader::decode_size(u8 encoded_size)
{
    if (encoded_size == 0x00)
        return Error::from_string_literal("XZ Block Header Size is 0x00 (Index Indicator byte). This is the Index, not another block."sv);
    return ((u16)encoded_size + 1) * 4;
}

ErrorOr<BlockHeader> BlockHeader::read_from_buffer(ReadonlyBytes bytes)
{
    // Sanity checks
    if (bytes.size() < 8)
        return Error::from_string_literal("XZ Block Header must be at least 8 bytes large."sv);
    if (bytes.size() % 4 != 0)
        return Error::from_string_literal("XZ Block Header size must be a multiple of 4."sv);

    InputMemoryStream memory_stream(bytes);
    CountedInputStream stream(memory_stream);

    u8 encoded_size;
    stream >> encoded_size;

    // 3.1.1. Block Header Size (and 4.1. Index Indicator)
    if (encoded_size == 0)
        return Error::from_string_literal("XZ Block Header Size is 0x00 (Index Indicator byte). This is the Index, not another block."sv);

    u16 real_size = ((u16)encoded_size + 1) * 4;
    if (bytes.size() != real_size)
        return Error::from_string_literal("XZ Block Header: Buffer size does not match encoded size."sv);

    // 3.1.2. Block Flags
    u8 flags;
    stream >> flags;
    if ((0b0011100 & flags) != 0)
        return Error::from_string_literal("XZ Block Header has reserved flag bits set."sv);

    // 3.1.3. Compressed Size
    Optional<u64> compressed_size;
    if ((flags & 0x40) > 0) {
        XzMultibyteInteger value;
        stream >> value;
        compressed_size = value;
        // FIXME: If the Compressed Size doesn't match the size of the Compressed Data field, the decoder MUST indicate an error.
    }

    // 3.1.4. Uncompressed Size
    Optional<u64> uncompressed_size;
    if ((flags & 0x40) > 0) {
        XzMultibyteInteger value;
        stream >> value;
        uncompressed_size = value;
        // FIXME: If the Uncompressed Size does not match the real uncompressed size, the decoder MUST indicate an error.
    }

    // 3.1.5. List of Filter Flags
    u8 number_of_filters = (flags & 0x03) + 1;
    // Let's assume we only have a single LZMA2 filter (according to 5.3.1 LZMA2).
    if (number_of_filters != 1)
        return Error::from_string_literal("XZ Block Header: Number of filters different from 1 not supported."sv);
    VERIFY(number_of_filters == 1);
    u64 filter_id;
    {
        XzMultibyteInteger value;
        stream >> value;
        filter_id = value;
    }
    if (filter_id != 0x21) // LZMA2 filter ID = 0x21
        return Error::from_string_literal("XZ Block Header: Non-LZMA2 filter not supported."sv);
    VERIFY(filter_id == 0x21);
    u64 filter_properties_size;
    {
        XzMultibyteInteger value;
        stream >> value;
        filter_properties_size = value;
    }
    if (filter_properties_size != 1)
        return Error::from_string_literal("XZ Block Header: Incorrect filter properties size for LZMA2 filter (expected 1 byte)."sv);
    VERIFY(filter_properties_size == 1);
    u8 lzma_filter_properties;
    stream >> lzma_filter_properties;

    // 3.1.6 Header Padding
    // 0-3 null bytes of padding to re-align to 4 byte alignment.
    u8 padding_length = (4 - (stream.count() % 4)) % 4;
    for (size_t i = 0; i < padding_length; ++i) {
        u8 padding_byte;
        stream >> padding_byte;
        if (padding_byte != 0x00)
            return Error::from_string_literal("XZ Block Header: Padding bytes must be 0x00."sv);
    }
    VERIFY(stream.count() % 4 == 0);

    LittleEndian<u32> crc32;
    stream >> crc32;
    Crypto::Checksum::CRC32 checksum;
    // NOTE: Do not include the CRC32 value (last 4 bytes) itself.
    checksum.update(bytes.slice(0, bytes.size() - 4));
    if (checksum.digest() != crc32)
        return Error::from_string_literal("XZ Block Header: CRC32 does not match."sv);

    if (stream.has_any_error() || !stream.unreliable_eof() || stream.count() != real_size)
        Error::from_string_literal("XZ Block Header: Incorrect size."sv);

    return BlockHeader {
        .compressed_size = compressed_size,
        .uncompressed_size = uncompressed_size,
        .lzma_filter_properties = lzma_filter_properties
    };
}

size_t XzDecompressor::read(Bytes bytes)
{
    auto result = try_read(bytes);
    if (result.is_error()) {
        dbgln("XzDecompressor error: {}", result.error());
        set_fatal_error();
        return 0;
    }
    if (has_any_error()) {
        dbgln("XzDecompressor error: Uncaught stream error."sv);
        set_fatal_error();
        return 0;
    }
    return result.value();
}

ErrorOr<size_t> XzDecompressor::try_read(Bytes bytes)
{
    if (m_input_stream.has_any_error())
        return Error::from_string_literal("Stream error."sv);

    size_t total_read = 0;
    while (total_read < bytes.size() && m_state != State::EndOfFile) {
        if (m_input_stream.unreliable_eof())
            return Error::from_string_literal("Input stream has premature EOF."sv);

        if (m_state == State::Initial) {
            dbgln_if(XZ_DEBUG, "XzDecompressor: State::Initial");
            VERIFY(!m_stream_header.has_value());
            m_stream_header = StreamHeader {};
            m_input_stream >> *m_stream_header;
            if (m_input_stream.has_any_error())
                return Error::from_string_literal("Stream error."sv);
            if (!m_stream_header->verify())
                return Error::from_string_literal("Invalid Stream Header."sv);
            m_state = State::Idle;
        }

        if (m_state == State::Idle) {
            dbgln_if(XZ_DEBUG, "XzDecompressor: State::Idle");
            // This is either the start of a Block (and thus a Block Header) or of the Index.
            VERIFY(m_stream_header.has_value());
            VERIFY(!m_block_header.has_value());

            // We need to read the first byte to determine the type.
            u8 first_byte;
            m_input_stream >> first_byte;
            if (m_input_stream.has_any_error())
                return Error::from_string_literal("Stream error."sv);

            if (first_byte == 0x00) {
                // This is the Index.
                // FIXME: Read the Index. For now, we just EOF and ignore it.
                m_state = State::EndOfFile;
            } else {
                // This is a Block Header.
                auto real_size = MUST(BlockHeader::decode_size(first_byte));
                auto buffer = TRY(ByteBuffer::create_uninitialized(real_size));
                buffer[0] = first_byte;
                if (!m_input_stream.read_or_error(buffer.bytes().slice(1)))
                    return Error::from_string_literal("Stream error."sv);
                m_block_header = TRY(BlockHeader::read_from_buffer(buffer));
                m_state = State::InBlockData;
            }
        }

        if (m_state == State::InBlockData) {
            dbgln_if(XZ_DEBUG, "XzDecompressor: State::InBlockData");
            // We are in a block body.
            VERIFY(m_stream_header.has_value());
            VERIFY(m_block_header.has_value());

            if (!m_block_state.has_value())
                m_block_state.emplace(m_input_stream, TRY(create_block_checksum()));

            TODO();
            // FIXME: Update the checksum with the bytes we actually read.

            m_state = State::AfterBlockData;
        }

        if (m_state == State::AfterBlockData) {
            dbgln_if(XZ_DEBUG, "XzDecompressor: State::AfterBlockData");
            VERIFY(m_stream_header.has_value());
            VERIFY(m_block_header.has_value());
            VERIFY(m_block_state.has_value());
            // The full compressed data has been read and output. Now, read the Block Padding and Check.
            // 0-3 null bytes of padding to re-align to 4 byte alignment.
            u8 padding_length = (4 - (m_block_state->counted_input_stream.count() % 4)) % 4;
            for (u8 i = 0; i < padding_length; ++i) {
                u8 padding_byte;
                m_input_stream >> padding_byte;
                if (padding_byte != 0x00)
                    return Error::from_string_literal("XZ Block Padding: Padding bytes must be 0x00."sv);
            }
            VERIFY(m_block_state->counted_input_stream.count() % 4 == 0);

            auto computed_checksum = TRY(m_block_state->checksum->digest());
            if (computed_checksum.size() > 0) {
                auto stored_checksum = TRY(ByteBuffer::create_uninitialized(computed_checksum.size()));
                if (!m_input_stream.read_or_error(stored_checksum))
                    return Error::from_string_literal("Stream error."sv);
                if (computed_checksum != stored_checksum)
                    return Error::from_string_literal("XZ Block Check: Checksum of uncompressed data does not match stored checksum."sv);
            }

            m_block_header.clear();
            m_block_state.clear();
            m_state = State::Idle;
        }

        if (m_state == State::AfterIndex) {
            dbgln_if(XZ_DEBUG, "XzDecompressor: State::AfterIndex");
            // FIXME: Read Stream Footer, verify it and compare it to the Stream Header.
            TODO();
        }
    }

    if (m_state == State::EndOfFile)
        dbgln_if(XZ_DEBUG, "XzDecompressor: State::EndOfFile");
    VERIFY(total_read > 0 || m_state == State::EndOfFile);
    // NOTE: This needs to return how many decompressed bytes we output into the argument buffer,
    //       not how many bytes we read from m_input_stream.
    return total_read;
}

ErrorOr<NonnullOwnPtr<BlockChecksum>> XzDecompressor::create_block_checksum() const
{
    VERIFY(m_stream_header.has_value());
    u8 check_type = m_stream_header->flags_second_byte & 0x0F;
    switch (check_type) {
    case 0x00:
        // None. 0 bytes.
        return TRY(try_make<DummyBlockChecksum>());
    case 0x01:
        // CRC32. 4 bytes.
        return TRY(try_make<CRC32BlockChecksum>());
    case 0x04:
        // CRC64. 8 bytes.
        return Error::from_string_literal("CRC64 Block Checksum not supported."sv);
        break;
    case 0x0A:
        // SHA256. 32 bytes.
        return Error::from_string_literal("SHA256 Block Checksum not supported."sv);
        break;
    default:
        return Error::from_string_literal("Invalid Block Checksum type."sv);
    }
}

XzDecompressor::BlockState::BlockState(InputStream& stream, NonnullOwnPtr<BlockChecksum> checksum)
    : counted_input_stream(CountedInputStream(stream))
    , checksum(move(checksum))
{
}

}
