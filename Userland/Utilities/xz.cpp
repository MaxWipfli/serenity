/*
 * Copyright (c) 2022, Max Wipfli <max.wipfli@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibCompress/Xz.h>
#include <LibCore/FileStream.h>
#include <LibMain/Main.h>

ErrorOr<int> serenity_main(Main::Arguments args)
{
    VERIFY(args.strings.size() == 2);
    auto filename = args.strings[1];
    auto file_stream = TRY(Core::InputFileStream::open_buffered(filename));
    auto xz_stream = Compress::XzDecompressor { file_stream };
    auto stdout_stream = Core::OutputFileStream::stdout_buffered();
    u8 buffer[4096];
    while (!xz_stream.has_any_error() && !xz_stream.unreliable_eof()) {
        const auto nread = xz_stream.read({ buffer, sizeof(buffer) });
        stdout_stream.write_or_error({ buffer, nread });
    }
    return !xz_stream.handle_any_error();
}
