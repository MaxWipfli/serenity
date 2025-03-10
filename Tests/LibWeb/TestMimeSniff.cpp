/*
 * Copyright (c) 2023, Kemal Zebari <kemalzebra@gmail.com>.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>

#include <LibWeb/MimeSniff/Resource.h>

TEST_CASE(determine_computed_mime_type_given_no_sniff_is_set)
{
    auto mime_type = MUST(Web::MimeSniff::MimeType::create("text"_string, "html"_string));
    auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff("\x00"sv.bytes(), Web::MimeSniff::SniffingConfiguration { .supplied_type = mime_type, .no_sniff = true }));

    EXPECT_EQ("text/html"sv, MUST(computed_mime_type.serialized()));

    // Cover the edge case in the context-specific sniffing algorithm.
    computed_mime_type = MUST(Web::MimeSniff::Resource::sniff("\x00"sv.bytes(), Web::MimeSniff::SniffingConfiguration {
                                                                                    .sniffing_context = Web::MimeSniff::SniffingContext::Image,
                                                                                    .supplied_type = mime_type,
                                                                                    .no_sniff = true,
                                                                                }));

    EXPECT_EQ("text/html"sv, MUST(computed_mime_type.serialized()));
}

TEST_CASE(determine_computed_mime_type_given_no_sniff_is_unset)
{
    auto supplied_type = MUST(Web::MimeSniff::MimeType::create("text"_string, "html"_string));
    auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff("\x00"sv.bytes(), Web::MimeSniff::SniffingConfiguration { .supplied_type = supplied_type }));

    EXPECT_EQ("application/octet-stream"sv, MUST(computed_mime_type.serialized()));

    // Make sure we cover the XML code path in the mime type sniffing algorithm.
    auto xml_mime_type = "application/rss+xml"sv;
    supplied_type = MUST(Web::MimeSniff::MimeType::parse(xml_mime_type)).release_value();
    computed_mime_type = MUST(Web::MimeSniff::Resource::sniff("\x00"sv.bytes(), Web::MimeSniff::SniffingConfiguration { .supplied_type = supplied_type }));

    EXPECT_EQ(xml_mime_type, MUST(computed_mime_type.serialized()));
}

static void set_image_type_mappings(HashMap<StringView, Vector<StringView>>& mime_type_to_headers_map)
{
    mime_type_to_headers_map.set("image/x-icon"sv, { "\x00\x00\x01\x00"sv, "\x00\x00\x02\x00"sv });
    mime_type_to_headers_map.set("image/bmp"sv, { "BM"sv });
    mime_type_to_headers_map.set("image/gif"sv, { "GIF87a"sv, "GIF89a"sv });
    mime_type_to_headers_map.set("image/webp"sv, { "RIFF\x00\x00\x00\x00WEBPVP"sv });
    mime_type_to_headers_map.set("image/png"sv, { "\x89PNG\x0D\x0A\x1A\x0A"sv });
    mime_type_to_headers_map.set("image/jpeg"sv, { "\xFF\xD8\xFF"sv });
}

static void set_audio_or_video_type_mappings(HashMap<StringView, Vector<StringView>>& mime_type_to_headers_map)
{
    mime_type_to_headers_map.set("audio/aiff"sv, { "FORM\x00\x00\x00\x00\x41IFF"sv });
    mime_type_to_headers_map.set("audio/mpeg"sv, { "ID3"sv });
    mime_type_to_headers_map.set("application/ogg"sv, { "OggS\x00"sv });
    mime_type_to_headers_map.set("audio/midi"sv, { "MThd\x00\x00\x00\x06"sv });
    mime_type_to_headers_map.set("video/avi"sv, { "RIFF\x00\x00\x00\x00\x41\x56\x49\x20"sv });
    mime_type_to_headers_map.set("audio/wave"sv, { "RIFF\x00\x00\x00\x00WAVE"sv });
}

TEST_CASE(determine_computed_mime_type_in_both_none_and_browsing_sniffing_context)
{
    HashMap<StringView, Vector<StringView>> mime_type_to_headers_map;

    mime_type_to_headers_map.set("application/octet-stream"sv, { "\x00"sv });
    mime_type_to_headers_map.set("text/html"sv, {
                                                    "\x09\x09<!DOCTYPE HTML\x20"sv,
                                                    "\x0A<HTML\x3E"sv,
                                                    "\x0C<HEAD\x20"sv,
                                                    "\x0D<SCRIPT>"sv,
                                                    "\x20<IFRAME>"sv,
                                                    "<H1>"sv,
                                                    "<DIV>"sv,
                                                    "<FONT>"sv,
                                                    "<TABLE>"sv,
                                                    "<A>"sv,
                                                    "<STYLE>"sv,
                                                    "<TITLE>"sv,
                                                    "<B>"sv,
                                                    "<BODY>"sv,
                                                    "<BR>"sv,
                                                    "<P>"sv,
                                                    "<!-->"sv,
                                                });
    mime_type_to_headers_map.set("text/xml"sv, { "<?xml"sv });
    mime_type_to_headers_map.set("application/pdf"sv, { "%PDF-"sv });
    mime_type_to_headers_map.set("application/postscript"sv, { "%!PS-Adobe-"sv });
    mime_type_to_headers_map.set("text/plain"sv, {
                                                     "\xFE\xFF\x00\x00"sv,
                                                     "\xFF\xFE\x00\x00"sv,
                                                     "\xEF\xBB\xBF\x00"sv,
                                                     "Hello world!"sv,
                                                 });

    set_image_type_mappings(mime_type_to_headers_map);
    set_audio_or_video_type_mappings(mime_type_to_headers_map);

    mime_type_to_headers_map.set("application/x-gzip"sv, { "\x1F\x8B\x08"sv });
    mime_type_to_headers_map.set("application/zip"sv, { "PK\x03\x04"sv });
    mime_type_to_headers_map.set("application/x-rar-compressed"sv, { "Rar\x20\x1A\x07\x00"sv });

    for (auto const& mime_type_to_headers : mime_type_to_headers_map) {
        auto mime_type = mime_type_to_headers.key;

        for (auto const& header : mime_type_to_headers.value) {

            // Test in a non-specific sniffing context.
            auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(header.bytes()));
            EXPECT_EQ(mime_type, computed_mime_type.essence());

            // Test sniffing in a browsing context.
            computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(header.bytes(), Web::MimeSniff::SniffingConfiguration { .sniffing_context = Web::MimeSniff::SniffingContext::Browsing }));
            EXPECT_EQ(mime_type, computed_mime_type.essence());
        }
    }
}

TEST_CASE(compute_mime_type_given_unknown_supplied_type)
{
    Array<Web::MimeSniff::MimeType, 3> unknown_supplied_types = {
        MUST(Web::MimeSniff::MimeType::create("unknown"_string, "unknown"_string)),
        MUST(Web::MimeSniff::MimeType::create("application"_string, "unknown"_string)),
        MUST(Web::MimeSniff::MimeType::create("*"_string, "*"_string))
    };
    auto header_bytes = "<HTML>"sv.bytes();

    for (auto const& unknown_supplied_type : unknown_supplied_types) {
        auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(header_bytes, Web::MimeSniff::SniffingConfiguration { .supplied_type = unknown_supplied_type }));
        EXPECT_EQ("text/html"sv, computed_mime_type.essence());
    }
}

TEST_CASE(determine_computed_mime_type_in_image_sniffing_context)
{
    // Cover case where supplied type is an XML MIME type.
    auto mime_type = "application/rss+xml"sv;
    auto supplied_type = MUST(Web::MimeSniff::MimeType::parse(mime_type)).release_value();
    auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(""sv.bytes(), Web::MimeSniff::SniffingConfiguration { .sniffing_context = Web::MimeSniff::SniffingContext::Image, .supplied_type = supplied_type }));

    EXPECT_EQ(mime_type, MUST(computed_mime_type.serialized()));

    HashMap<StringView, Vector<StringView>> mime_type_to_headers_map;

    set_image_type_mappings(mime_type_to_headers_map);

    // Also consider a resource that is not an image.
    mime_type_to_headers_map.set("application/octet-stream"sv, { "\x00"sv });

    for (auto const& mime_type_to_headers : mime_type_to_headers_map) {
        mime_type = mime_type_to_headers.key;

        for (auto const& header : mime_type_to_headers.value) {
            computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(header.bytes(), Web::MimeSniff::SniffingConfiguration { .sniffing_context = Web::MimeSniff::SniffingContext::Image }));
            EXPECT_EQ(mime_type, computed_mime_type.essence());
        }
    }

    // Cover case where we aren't dealing with an image MIME type.
    mime_type = "text/html"sv;
    supplied_type = MUST(Web::MimeSniff::MimeType::parse("text/html"sv)).release_value();
    computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(""sv.bytes(), Web::MimeSniff::SniffingConfiguration { .sniffing_context = Web::MimeSniff::SniffingContext::Image, .supplied_type = supplied_type }));

    EXPECT_EQ(mime_type, computed_mime_type.essence());
}

TEST_CASE(determine_computed_mime_type_in_audio_or_video_sniffing_context)
{
    // Cover case where supplied type is an XML MIME type.
    auto mime_type = "application/rss+xml"sv;
    auto supplied_type = MUST(Web::MimeSniff::MimeType::parse(mime_type)).release_value();
    auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(""sv.bytes(), Web::MimeSniff::SniffingConfiguration {
                                                                                     .sniffing_context = Web::MimeSniff::SniffingContext::AudioOrVideo,
                                                                                     .supplied_type = supplied_type,
                                                                                 }));

    EXPECT_EQ(mime_type, MUST(computed_mime_type.serialized()));
    HashMap<StringView, Vector<StringView>> mime_type_to_headers_map;

    set_audio_or_video_type_mappings(mime_type_to_headers_map);

    // Also consider a resource that is not an audio or video.
    mime_type_to_headers_map.set("application/octet-stream"sv, { "\x00"sv });

    for (auto const& mime_type_to_headers : mime_type_to_headers_map) {
        auto mime_type = mime_type_to_headers.key;

        for (auto const& header : mime_type_to_headers.value) {
            auto computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(header.bytes(), Web::MimeSniff::SniffingConfiguration { .sniffing_context = Web::MimeSniff::SniffingContext::AudioOrVideo }));
            EXPECT_EQ(mime_type, computed_mime_type.essence());
        }
    }

    // Cover case where we aren't dealing with an audio or video MIME type.
    mime_type = "text/html"sv;
    supplied_type = MUST(Web::MimeSniff::MimeType::parse("text/html"sv)).release_value();
    computed_mime_type = MUST(Web::MimeSniff::Resource::sniff(""sv.bytes(), Web::MimeSniff::SniffingConfiguration {
                                                                                .sniffing_context = Web::MimeSniff::SniffingContext::AudioOrVideo,
                                                                                .supplied_type = supplied_type,
                                                                            }));

    EXPECT_EQ(mime_type, computed_mime_type.essence());
}
