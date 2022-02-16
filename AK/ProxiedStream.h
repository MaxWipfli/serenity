/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Checked.h>
#include <AK/Function.h>
#include <AK/Stream.h>

namespace AK {

class ProxiedInputStream : public InputStream {
public:
    explicit ProxiedInputStream(InputStream& stream, Function<void(ReadonlyBytes)> callback)
        : m_stream(stream)
        , m_callback(move(callback))
    {
    }

    // NOTE: We need to override the default InputStream destructor since it
    //       asserts !has_any_error().
    virtual ~ProxiedInputStream()
    {
    }

    bool has_recoverable_error() const override { return m_stream.has_recoverable_error(); }
    bool has_fatal_error() const override { return m_stream.has_fatal_error(); }
    bool has_any_error() const override { return m_stream.has_any_error(); }

    bool handle_recoverable_error() override { return m_stream.handle_recoverable_error(); }
    bool handle_fatal_error() override { return m_stream.handle_fatal_error(); }
    bool handle_any_error() override { return m_stream.handle_any_error(); }

    void set_recoverable_error() const override { return m_stream.set_recoverable_error(); }
    void set_fatal_error() const override { return m_stream.set_fatal_error(); }

    bool unreliable_eof() const override { return m_stream.unreliable_eof(); }

    size_t read(Bytes bytes) override
    {
        auto nread = m_stream.read(bytes);
        if (nread > 0)
            m_callback(bytes.slice(0, nread));
        return nread;
    }

    bool read_or_error(Bytes bytes) override
    {
        auto result = m_stream.read_or_error(bytes);
        if (result)
            m_callback(bytes);
        return result;
    }

    bool discard_or_error(size_t count) override
    {
        u8 buffer[4096];
        Bytes bytes { buffer, sizeof(buffer) };
        while (count > 0) {
            auto bytes_to_read = min(bytes.size(), count);
            auto nread = read(bytes.slice(bytes_to_read));
            if (nread == 0 || has_any_error())
                return false;
            count -= nread;
        }
        return true;
    }

    // Reading from the stream returned here will most definitely brick the counting behavior of CountedInputStream.
    InputStream& underlying_stream() { return m_stream; }

private:
    InputStream& m_stream;
    Function<void(ReadonlyBytes)> m_callback;
};

class CountedInputStream : public ProxiedInputStream {
public:
    explicit CountedInputStream(InputStream& stream)
        : ProxiedInputStream(stream, [this](ReadonlyBytes bytes) {
            m_count += bytes.size();
            VERIFY(!m_count.has_overflow());
        })
    {
    }

    size_t count() const { return m_count.value(); }

private:
    Checked<size_t> m_count { 0 };
};

}

using AK::CountedInputStream;
using AK::ProxiedInputStream;
