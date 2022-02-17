/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Memory.h>
#include <LibCompress/LZMA/LZMADecoder.h>

namespace Compress::LZMA {

ErrorOr<void> LZMADecoder::set_lclppb_properties(u8 lclppb_byte)
{
    // The value of the lclppb byte is lc + lp * 9 + pb * 9 * 5.
    m_lc = lclppb_byte % 9;
    u8 quotient = lclppb_byte / 9;
    m_lp = quotient % 5;
    m_pb = quotient / 5;
    // FIXME: Remove this VERIFY once I am confident the code above actually does what it's supposed to do.
    VERIFY(m_lc + m_lp * 9 + m_pb * 9 * 5 == lclppb_byte);
    // In LZMA2 streams, (lc + lp) and pb must not be greater than 4.
    if ((m_lc + m_lp) > 4 || m_pb > 4) {
        return Error::from_string_literal("LZMA2InputStream: In LZMA2 streams, (lc + lp) and pb must not be greater than 4."sv);
    }
    return {};
}

void LZMADecoder::reset_state()
{
    // FIXME: Reset reps.
    m_state.reset();
    m_probability_variable_groups.initialize();
    // FIXME: Reset literal decoder, matchLen decoder and repLen decoder.
}

void LZMADecoder::reset_dict()
{
    m_buffer_start = 0;
    m_buffer_position = 0;
    // FIXME: m_buffer_fill = m_buffer_limit = 0
    // FIXME: m_buffer[m_buffer.size() - 1] = 0x00; Why???
}

void LZMADecoder::set_pending_limit(size_t limit)
{
    VERIFY(limit < m_buffer.size() - 1);
    m_pending_limit = limit;
}

size_t LZMADecoder::flush_pending_data(Bytes bytes)
{
    auto size = min(bytes.size(), pending_data_size());
    if (size == 0)
        return 0;
    // Copy size bytes from m_buffer at m_buffer_start to bytes.
    size_t copied = 0;
    while (size > 0) {
        auto copy_size = min(size, m_buffer.size() - m_buffer_start);
        auto copy_bytes = m_buffer.bytes().slice(m_buffer_start, copy_size);
        copy_bytes.copy_to(bytes);
        size -= copy_size;
        m_buffer_start += copy_size;
        copied += copy_size;
    }
    return copied;
}

ErrorOr<void> LZMADecoder::copy_uncompressed_from_input(size_t size)
{
    // This needs to loop since we may need to copy once to the end and once to
    // the start of the buffer (due to the wrap-around).
    while (size > 0) {
        if ((m_buffer.size() - pending_data_size()) < size)
            return Error::from_string_literal("LZMADecoder: Not enough space in buffer to copy uncompressed data."sv);
        auto copy_size = min(m_buffer.size() - m_buffer_position, size);
        auto copy_bytes = m_buffer.bytes().slice(m_buffer_position, copy_size);
        if (!m_input_stream.read_or_error(copy_bytes))
            return Error::from_string_literal("LZMADecoder: Stream error."sv);
        m_buffer_position += copy_size % m_buffer.size();
        size -= copy_size;
    }
    return {};
}

ErrorOr<void> LZMADecoder::decode(size_t compressed_size, size_t uncompressed_size)
{
    // FIXME: Do something with this:
    (void)uncompressed_size;

    TRY(m_range_decoder.prepare(m_input_stream, compressed_size));
    while (pending_data_size() < m_pending_limit) {
        // FIXME: If m_pending_limit is close to the buffer size, and pending_data_size()
        //        is close to m_pending_limit, we could exceed the buffer size in this loop
        //        iteration.

        if (m_range_decoder.decode_bit(m_probability_variable_groups.is_match[m_state.get()][pos_state()]) == 0) {
            // LIT: 0 + literal byte
            // FIXME: Decode literal.
        } else {
            if (m_range_decoder.decode_bit(m_probability_variable_groups.is_rep[m_state.get()]) == 0) {
                // MATCH: 1 + 0 + length + distance
                // FIXME: Decode match.
            } else {
                auto length = decode_rep_match();
                (void)length;
            }
        }
    }

    return {};
}

ErrorOr<void> LZMADecoder::put_byte_to_buffer(u8 byte)
{
    if (m_buffer.size() - pending_data_size() == 0)
        return Error::from_string_literal("LZMADecoder: Not enough space in buffer to put byte."sv);
    m_buffer[m_buffer_position] = byte;
    m_buffer_position = (m_buffer_position + 1) % m_buffer.size();
    return {};
}

void LZMADecoder::decode_literal()
{
}

LZMADecoder::Match LZMADecoder::decode_match()
{
    return {};
}

LZMADecoder::Match LZMADecoder::decode_rep_match()
{
    return {};
}

// https://en.wikipedia.org/wiki/Lempel%E2%80%93Ziv%E2%80%93Markov_chain_algorithm#LZMA_coding_contexts
void LZMADecoder::State::update_lit()
{
    if (m_state <= 3)
        m_state = 0;
    else if (m_state <= 8)
        m_state -= 3;
    else
        m_state -= 6;
}

void LZMADecoder::State::update_match()
{
    if (m_state <= 6)
        m_state = 7;
    else
        m_state = 10;
}

void LZMADecoder::State::update_long_rep()
{
    if (m_state <= 6)
        m_state = 8;
    else
        m_state = 11;
}

void LZMADecoder::State::update_short_rep()
{
    if (m_state <= 6)
        m_state = 9;
    else
        m_state = 11;
}

// This fills the full struct with the initial value, assuming
// it only contains ProbabilityVariable[] and ProbabilityVariable[][].
void LZMADecoder::ProbabilityVariableGroups::initialize()
{
    Span<ProbabilityVariable> values { (ProbabilityVariable*)this, sizeof(*this) / sizeof(ProbabilityVariable) };
    values.fill(RangeDecoder::InitialProbabilityValue);
}

}
