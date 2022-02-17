/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteBuffer.h>

namespace Compress::LZMA {

using ProbabilityVariable = u16;

// NOTE: This is based on https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm#Range_coding_of_bits
//       and the relevant classes from https://tukaani.org/xz/java.html.

class RangeDecoder {
public:
    static constexpr ProbabilityVariable InitialProbabilityValue = 0x400;

    RangeDecoder() = default;

    ErrorOr<void> prepare(InputStream&, size_t compressed_size);
    bool is_finished() const { return m_input_position == m_input_buffer.size() && m_code == 0; }

    // Context-based decoding.
    u8 decode_bit(ProbabilityVariable&);
    // Fixed-probability decoding.
    u8 decode_bit();

private:
    void normalize();

    ByteBuffer m_input_buffer;
    size_t m_input_position { 0 };

    // This represents the range size.
    u32 m_range { 0 };
    // This represents the encoded point within the range.
    u32 m_code { 0 };
};

}
