/*
 * Copyright (c) 2020, the SerenityOS developers.
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Span.h>
#include <AK/Types.h>
#include <LibCrypto/Checksum/ChecksumFunction.h>

namespace Crypto::Checksum {

struct CRC64Table {
    u64 data[256];

    constexpr CRC64Table()
    {
        for (auto i = 0; i < 256; i++) {
            u64 value = i;
            for (auto j = 0; j < 8; j++) {
                if ((value & 1) > 0)
                    value = 0xC96C5795D7870F42 ^ (value >> 1);
                else
                    value = value >> 1;
            }
            data[i] = value;
        }
    }

    constexpr u64 operator[](size_t index) const
    {
        return data[index];
    }
};

class CRC64 : public ChecksumFunction<u64> {
private:
    constexpr static auto table = CRC64Table();

public:
    CRC64() { }
    CRC64(ReadonlyBytes data)
    {
        update(data);
    }

    CRC64(u64 initial_state, ReadonlyBytes data)
        : m_state(initial_state)
    {
        update(data);
    }

    virtual void update(ReadonlyBytes data) override;
    virtual u64 digest() const override;

private:
    u64 m_state { ~0ul };
};

}
