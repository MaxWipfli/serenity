/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "AK/Vector.h"
#include <AK/ByteBuffer.h>
#include <AK/ProxiedStream.h>
#include <LibCompress/LZMA/RangeDecoder.h>

namespace Compress::LZMA {

// This class decompresses a raw LZMA2 stream, given the dict size and the lc, lp and pb values.
class LZMADecoder {
public:
    ErrorOr<void> set_lclppb_properties(u8 lclppb_byte);
    void reset_state();
    void reset_dict();

    size_t pending_data_size() const { return (m_buffer_position - m_buffer_start) % m_buffer.size(); }
    void set_pending_limit(size_t);
    size_t flush_pending_data(Bytes);

    ErrorOr<void> copy_uncompressed_from_input(size_t size);
    ErrorOr<void> decode(size_t compressed_size, size_t uncompressed_size);

    size_t dict_size() const { return m_buffer.size(); }

    static ErrorOr<LZMADecoder> create(InputStream& stream, u32 dict_size)
    {
        auto buffer = TRY(ByteBuffer::create_uninitialized(dict_size));
        return LZMADecoder(stream, move(buffer));
    }

private:
    class State {
    public:
        State() = default;

        u8 get() const { return m_state; }
        bool last_was_lit() const { return m_state <= 6; }

        void update_lit();
        void update_match();
        void update_long_rep();
        void update_short_rep();
        void reset() { m_state = 0; }

    private:
        u8 m_state { 0 };
    };

    struct ProbabilityVariableGroups {
        static constexpr u8 state_count = 12;
        static constexpr u8 pos_state_count = 1 << 4;

        ProbabilityVariable is_match[state_count][pos_state_count];
        ProbabilityVariable is_rep[state_count];
        ProbabilityVariable is_rep0[state_count];
        ProbabilityVariable is_rep0_long[state_count][pos_state_count];
        ProbabilityVariable is_rep1[state_count];
        ProbabilityVariable is_rep2[state_count];

        void initialize();
    };

    struct Match {
        u16 length { 0 };
        u32 distance { 0 };
    };

    LZMADecoder(InputStream& stream, ByteBuffer buffer)
        : m_input_stream(stream)
        , m_buffer(move(buffer))
    {
        m_probability_variable_groups.initialize();
    }

    ErrorOr<void> put_byte_to_buffer(u8);

    // The m_pb least significant digits of m_buffer_position.
    u8 pos_state() const { return m_buffer_position & ((1 << m_pb) - 1); }
    // The m_lp least significant digits of m_buffer_position.
    u8 literal_pos_state() const { return m_buffer_position & ((1 << m_lp) - 1); }
    u8 prev_byte_lc_msbs() const { return m_buffer_position & (~((1 << (8 - m_lc)) - 1)); }

    void decode_literal();
    Match decode_match();
    Match decode_rep_match();

    InputStream& m_input_stream;

    // The number of high bits of the previous byte to use as a context for literal encoding.
    u8 m_lc { 0 };
    // The number of low bits of the dictionary position to include in literal_pos_state.
    u8 m_lp { 0 };
    // The number of low bits of the dictionary position to include in pos_state.
    u8 m_pb { 0 };

    // This is the dictionary.
    ByteBuffer m_buffer;
    size_t m_buffer_position { 0 };
    // This points to the first byte that hasn't been flushed to the output yet.
    size_t m_buffer_start { 0 };
    size_t m_buffer_end { 0 };

    // The number of bytes the user wants to read. This has to be strictly less
    // than the buffer/dict size.
    size_t m_pending_limit { 0 };

    State m_state;
    ProbabilityVariableGroups m_probability_variable_groups;

    RangeDecoder m_range_decoder;
};

}
