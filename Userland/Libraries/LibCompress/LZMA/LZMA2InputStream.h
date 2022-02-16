/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Stream.h>

namespace Compress::LZMA {

// This class decompresses a raw LZMA2 stream (without any XZ headers).
class LZMA2InputStream : public InputStream {
public:
    LZMA2InputStream(InputStream& stream, u8 properties_byte);

    size_t read(Bytes bytes) override;
    bool read_or_error(Bytes) override { TODO(); };
    bool discard_or_error(size_t) override { TODO(); }

    bool unreliable_eof() const override { return eof(); }
    bool eof() const { return m_state == State::EndOfFile; }

private:
    // This is being wrapped by size_t read(Bytes), and exists so that
    // we can freely use ErrorOr<T> inside.
    ErrorOr<size_t> try_read(Bytes);

    enum class State {
        Idle,
        UncompressedBlock,
        LZMABlock,
        EndOfFile
    };

    InputStream& m_input_stream;

    State m_state { State::Idle };

    Optional<u8> m_lzma_chunk_control_byte;
    Optional<u16> m_compressed_data_size;
    Optional<u32> m_uncompressed_data_size;

    u32 m_dict_size { 0 };
};

}
