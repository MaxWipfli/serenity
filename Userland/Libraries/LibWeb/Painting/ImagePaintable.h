/*
 * Copyright (c) 2022, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibWeb/Layout/ImageBox.h>
#include <LibWeb/Painting/PaintableBox.h>

namespace Web::Painting {

class ImagePaintable final
    : public PaintableBox
    , public DOM::Document::ViewportClient {
    JS_CELL(ImagePaintable, PaintableBox);

public:
    static JS::NonnullGCPtr<ImagePaintable> create(Layout::ImageBox const&);

    virtual void paint(PaintContext&, PaintPhase) const override;

    Layout::ImageBox const& layout_box() const;

private:
    // ^JS::Cell
    virtual void finalize() override;

    // ^Document::ViewportClient
    virtual void did_set_viewport_rect(CSSPixelRect const&) final;

    ImagePaintable(Layout::ImageBox const&);
};

}
