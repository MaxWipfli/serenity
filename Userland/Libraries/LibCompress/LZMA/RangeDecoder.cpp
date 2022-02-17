/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Endian.h>
#include <AK/Stream.h>
#include <LibCompress/LZMA/RangeDecoder.h>

namespace Compress::LZMA {

inline Error stream_error()
{
    return Error::from_string_literal("Compress::LZMABlock::RangeDecoder: stream error"sv);
}

u8 RangeDecoder::decode_bit(ProbabilityVariable& prob)
{
    // 1. If range is less than 2^24, perform normalization.
    if (m_range < 0x1000000)
        normalize();

    // 2. Set bound to floor(range / 2^11) * prob
    u32 bound = (m_range / 0x800) * prob;

    // 3. If code is less than bound:
    //    Return bit 0
    if (m_code < bound) {
        // 1. Set range to bound
        m_range = bound;
        // 2. Set prob to prob + floor((2^11 - prob) / 2^5)
        prob += (0x800 - prob) / 0x20;
        // 3. Return bit 0
        return 0;
    }

    // 4. Otherwise (if code is greater than or equal to the bound):
    // 4.1. Set range to range - bound
    m_range -= bound;
    // 4.2. Set code to code - bound
    m_code -= bound;
    // 4.3. Set prob to prob - floor(prob / 2^5)
    prob -= prob / 0x20;
    // 4.4. Return bit 1
    return 1;
}

u8 RangeDecoder::decode_bit()
{
    // 1. If range is less than 2^24, perform normalization
    if (m_range < 0x1000000)
        normalize();
    // 2. Set range to floor(range / 2)
    m_range /= 2;
    // 3. If code is less than range: Return bit 0
    if (m_code < m_range)
        return 0;
    // 4. Otherwise (if code is greater or equal than range):
    // 4.1. Set code to code - range
    m_code -= m_range;
    // 4.2. Return bit 1
    return 1;
}

ErrorOr<void> RangeDecoder::prepare(InputStream& stream, size_t compressed_size)
{
    // First, read the 5 initialization bytes. The first byte is ignored,
    // and the next 4 bytes are the starting value for m_code in big-endian format.
    u8 discarded_byte;
    stream >> discarded_byte;
    BigEndian<u32> code_init_value;
    stream >> code_init_value;
    if (stream.has_any_error())
        return Error::from_string_literal("LZMA::RangeDecoder: Stream error."sv);

    m_code = code_init_value;
    m_range = 0xFFFFFFFF;

    // Now, read the rest of the data into a ByteBuffer.
    m_input_buffer = TRY(ByteBuffer::create_uninitialized(compressed_size - 5));
    if (!stream.read_or_error(m_input_buffer.bytes()))
        Error::from_string_literal("LZMA::RangeDecoder: Could not read compressed data from input stream."sv);
    m_input_position = 0;
    return {};
}

void RangeDecoder::normalize()
{
    // 1. Shift both range and code left by 8 bits.
    m_range <<= 8;
    m_code <<= 8;

    // 2. Read a byte from the compressed stream.
    // 3. Set the least significant 8 bits of code to the byte value read.
    VERIFY(m_input_position < m_input_buffer.size());
    u8 byte = m_input_buffer[m_input_position++];
    m_code |= byte;
}

}
