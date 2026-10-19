// SPDX-License-Identifier: GPL-3.0-or-later
// Software renderer for the launcher: a 1920x1080 RGBA canvas in ordinary memory (VideoOut
// buffers are write-combined, so blending is done here and the result copied once per frame).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Ui {

/// 0xRRGGBB colour (the canvas stores 0xAABBGGRR, VideoOut's A8B8G8R8 byte order).
struct Color {
    uint32_t rgb;
};
inline uint32_t Pack(Color c) {
    return 0xff000000u | ((c.rgb & 0xff) << 16) | (c.rgb & 0xff00) | ((c.rgb >> 16) & 0xff);
}

/// An opaque picture (game icon, user photo), stored in canvas byte order.
struct Image {
    int width{};
    int height{};
    std::vector<uint32_t> pixels;
    bool Empty() const { return pixels.empty(); }
};

enum class Align { Left, Center, Right };

class Canvas {
public:
    static constexpr int Width = 1920;
    static constexpr int Height = 1080;

    Canvas();
    const uint32_t* Pixels() const { return pixels.data(); }

    void Clear(Color color);
    void FillRect(float x, float y, float w, float h, Color color, float alpha = 1.0f);
    void FillRoundRect(float x, float y, float w, float h, float radius, Color color, float alpha = 1.0f);
    /// A border of `thickness` inside the box (x, y, w, h).
    void StrokeRoundRect(float x, float y, float w, float h, float radius, float thickness, Color color,
                         float alpha = 1.0f);
    void FillCircle(float cx, float cy, float r, Color color, float alpha = 1.0f);
    void StrokeCircle(float cx, float cy, float r, float thickness, Color color, float alpha = 1.0f);
    void HLine(float x0, float x1, float y, float thickness, Color color, float alpha = 1.0f);
    void VLine(float x, float y0, float y1, float thickness, Color color, float alpha = 1.0f);

    /// A generated mask (ps4_ui_assets.h) by name, top-left at (x, y), tinted.
    void Mask(std::string_view name, float x, float y, Color color, float alpha = 1.0f);
    void MaskCentered(std::string_view name, float cx, float cy, Color color, float alpha = 1.0f);
    /// A mask scaled to `size` pixels wide, centred (filtered; used for avatar symbols).
    void MaskScaled(std::string_view name, float cx, float cy, float size, Color color, float alpha = 1.0f);
    int MaskWidth(std::string_view name) const;
    /// Soft shadow around a box / circle.
    void Shadow(float x, float y, float w, float h, float alpha = 0.4f);
    void CircleShadow(float cx, float cy, float r, float alpha = 0.35f);

    void DrawImage(const Image& image, float x, float y, float alpha = 1.0f);
    /// Nearest-neighbour scaled draw (animations only; static sizes are pre-scaled).
    void DrawImageScaled(const Image& image, float x, float y, float w, float h, float alpha = 1.0f);
    /// Image clipped to a circle (user photos).
    void DrawImageCircle(const Image& image, float cx, float cy, float r);

    /// UTF-8 text with its baseline at y. Text wider than max_width ends in "...".
    void Text(std::string_view text, float x, float y, int size, Color color, Align align = Align::Left,
              float max_width = 0, float alpha = 1.0f);
    float Measure(std::string_view text, int size) const;
    /// Splits text into lines no wider than width.
    std::vector<std::string> Wrap(std::string_view text, int size, float width) const;

    void PushClip(float x, float y, float w, float h);
    void PopClip();
    void PushOpacity(float alpha);
    void PopOpacity();

private:
    struct Rect {
        int x0, y0, x1, y1;
    };
    void Blend(int x, int y, uint32_t color, int alpha);
    void Span(int x0, int x1, int y, uint32_t color, int alpha);
    int Alpha(float alpha) const;
    std::vector<uint32_t> pixels;
    std::vector<Rect> clips;
    std::vector<float> opacities;
    Rect clip;
    float opacity{1.0f};
};

/// Decodes a JPEG and scales it to w x h (empty Image on failure).
Image DecodeImage(const std::vector<uint8_t>& data, int w, int h);
/// High-quality resize of an Image.
Image Resize(const Image& image, int w, int h);

} // namespace Ui
