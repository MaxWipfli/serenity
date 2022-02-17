/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Debug.h>
#include <LibCompress/LZMA/LZMA2InputStream.h>

namespace Compress::LZMA {

LZMA2InputStream::LZMA2InputStream(InputStream& stream, u8 properties_byte)
    : m_input_stream(stream)
{
    // NOTE: This code is from https://tukaani.org/xz/xz-file-format.txt, section 5.3.1 LZMA2.
    if (properties_byte > 40) {
        dbgln("LZMA2InputStream: Invalid properties byte {}, must be <= 40.", properties_byte);
        VERIFY_NOT_REACHED();
    }

    if (properties_byte == 40) {
        m_dict_size = NumericLimits<u32>::max();
    } else {
        m_dict_size = 2 | (properties_byte & 1);
        m_dict_size <<= properties_byte / 2 + 11;
    }
}

size_t LZMA2InputStream::read(Bytes bytes)
{
    auto result = try_read(bytes);
    if (result.is_error()) {
        dbgln("LZMA2InputStream error: {}", result.error());
        set_fatal_error();
        return 0;
    }
    if (has_any_error()) {
        dbgln("LZMA2InputStream error: Uncaught stream error."sv);
        set_fatal_error();
        return 0;
    }
    return result.value();
}

ErrorOr<size_t> LZMA2InputStream::try_read(Bytes bytes)
{
    if (bytes.size() == 0)
        return 0;
    // FIXME: This is a quick hack to get everything to compile. Maybe create the lzma_decoder on construction.
    if (!m_lzma_decoder.has_value())
        m_lzma_decoder = TRY(LZMADecoder::create(m_input_stream, m_dict_size));

    size_t total_read = 0;
    while (total_read < bytes.size()) {
        if (has_any_error() || m_state == State::EndOfFile)
            break;

        if (m_state == State::Idle) {
            dbgln_if(XZ_DEBUG, "LZMA2InputStream: State::Idle");
            u8 packet_control_byte;
            m_input_stream >> packet_control_byte;
            if (m_input_stream.has_any_error())
                return Error::from_string_literal("Stream error."sv);

            if (packet_control_byte == 0x00) {
                // End of file.
                m_state = State::EndOfFile;
                break;
            } else if (packet_control_byte == 0x01) {
                // Dictionary reset followed by an uncompressed chunk.
                m_lzma_decoder->reset_dict();
                m_state = State::UncompressedBlock;
            } else if (packet_control_byte == 0x02) {
                // Uncompressed chunk without a dictionary reset.
                m_state = State::UncompressedBlock;
            } else if (0x03 <= packet_control_byte && packet_control_byte <= 0x7F) {
                // Invalid value.
                return Error::from_string_literal("Invalid packet control byte."sv);
            } else {
                // LZMA chunk, where the lowest 5 bits are used as bit 16-20 of the uncompressed
                // size minus one, and bit 5-6 indicates what should be reset.
                m_state = State::LZMABlock;
                VERIFY(!m_lzma_chunk_control_byte.has_value());
                m_lzma_chunk_control_byte = packet_control_byte;
            }
        }

        if (m_state == State::UncompressedBlock) {
            dbgln_if(XZ_DEBUG, "LZMA2InputStream: State::UncompressedBlock");
            if (!m_uncompressed_data_size.has_value()) {
                BigEndian<u16> uncompressed_data_size_minus_one;
                m_input_stream >> uncompressed_data_size_minus_one;
                if (m_input_stream.has_any_error())
                    return Error::from_string_literal("Stream error."sv);
                m_uncompressed_data_size = uncompressed_data_size_minus_one + 1;
            }

            size_t to_read = min(bytes.size() - total_read, *m_uncompressed_data_size);
            auto nread = m_input_stream.read(bytes.slice(total_read, to_read));
            total_read += nread;
            *m_uncompressed_data_size -= nread;
            if (*m_uncompressed_data_size == 0) {
                m_uncompressed_data_size = {};
                m_state = State::Idle;
            }
            // FIXME: Also "Copy data verbatim into dictionary".
        }

        if (m_state == State::LZMABlock) {
            dbgln_if(XZ_DEBUG, "LZMA2InputStream: State::LZMABlock");
            // 16-bit big-endian value encoding the low 16-bits of the uncompressed size minus one
            if (!m_uncompressed_data_size.has_value()) {
                BigEndian<u16> uncompressed_data_size_minus_one;
                m_input_stream >> uncompressed_data_size_minus_one;
                if (m_input_stream.has_any_error())
                    return Error::from_string_literal("Stream error."sv);
                u8 upper_bits_of_size = *m_lzma_chunk_control_byte & 0x1F;
                m_uncompressed_data_size = ((u32)uncompressed_data_size_minus_one | ((u32)upper_bits_of_size << 16)) + 1;
            }

            // 16-bit big-endian value encoding the compressed size minus one
            if (!m_compressed_data_size.has_value()) {
                BigEndian<u16> compressed_data_size_minus_one;
                m_input_stream >> compressed_data_size_minus_one;
                if (m_input_stream.has_any_error())
                    return Error::from_string_literal("Stream error."sv);
                m_compressed_data_size = compressed_data_size_minus_one + 1;
            }

            // Properties/lclppb byte if bit 6 in the control byte is set
            u8 properties_byte = 0;
            if ((*m_lzma_chunk_control_byte & 0x40) > 0) {
                m_input_stream >> properties_byte;
                if (m_input_stream.has_any_error())
                    return Error::from_string_literal("Stream error."sv);
            }

            u8 reset_bits = (*m_lzma_chunk_control_byte >> 5) & 0x03;
            if (reset_bits >= 1) {
                m_lzma_decoder->reset_state();
            }
            if (reset_bits >= 2) {
                TRY(m_lzma_decoder->set_lclppb_properties(properties_byte));
            }
            if (reset_bits == 3) {
                m_lzma_decoder->reset_dict();
            }

            TODO();
        }
    }
    return total_read;
}

}
