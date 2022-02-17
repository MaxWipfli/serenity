/*
 * Copyright (c) 2020, the SerenityOS developers.
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Span.h>
#include <AK/Types.h>
#include <LibCrypto/Checksum/CRC64.h>

namespace Crypto::Checksum {

void CRC64::update(ReadonlyBytes data)
{
    for (auto const& byte : data)
        m_state = table[(m_state ^ byte) & 0xFF] ^ (m_state >> 8);
};

u64 CRC64::digest() const
{
    return ~m_state;
}

}
