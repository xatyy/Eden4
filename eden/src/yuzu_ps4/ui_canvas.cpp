// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include "common/stb.h"
#include "ps4_ui_assets.h"
#include "yuzu_ps4/ui_canvas.h"

namespace Ui {
namespace {

const UiAssets::Font& FontFor(int size) {
    const UiAssets::Font* best = &UiAssets::fonts[0];
    for (const auto& font : UiAssets::fonts)
        if (std::abs(font.size - size) < std::abs(best->size - size)) best = &font;
    return *best;
}

const UiAssets::Glyph* FindGlyph(const UiAssets::Font& font, uint32_t cp) {
    const auto* begin = font.glyphs;
    const auto* end = font.glyphs + font.count;
    const auto* it = std::lower_bound(begin, end, cp, [](const UiAssets::Glyph& g, uint32_t v) { return g.cp < v; });
    if (it != end && it->cp == cp) return it;
    if (cp != '?') return FindGlyph(font, '?');
    return nullptr;
}

const UiAssets::Mask* FindMask(std::string_view name) {
    static const auto table = [] {
        std::unordered_map<std::string_view, const UiAssets::Mask*> map;
        for (const auto& mask : UiAssets::masks) map.emplace(mask.name, &mask);
        return map;
    }();
    const auto it = table.find(name);
    return it == table.end() ? nullptr : it->second;
}

// Next code point of UTF-8 text (invalid bytes come out as '?').
uint32_t NextCodepoint(std::string_view text, size_t& i) {
    const auto byte = [&](size_t k) { return static_cast<uint8_t>(text[k]); };
    const uint8_t c = byte(i++);
    if (c < 0x80) return c;
    int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : -1;
    if (extra < 0 || i + extra > text.size()) return '?';
    uint32_t cp = c & (0x3f >> extra);
    for (int k = 0; k < extra; ++k) cp = (cp << 6) | (byte(i++) & 0x3f);
    return cp;
}

inline uint32_t Mix(uint32_t dst, uint32_t src, int a) {
    // a: 0..256
    const uint32_t rb = ((((src & 0xff00ff) * a) + ((dst & 0xff00ff) * (256 - a))) >> 8) & 0xff00ff;
    const uint32_t g = ((((src & 0x00ff00) * a) + ((dst & 0x00ff00) * (256 - a))) >> 8) & 0x00ff00;
    return 0xff000000u | rb | g;
}

inline float Clamp01(float v) {
    return v < 0 ? 0 : v > 1 ? 1 : v;
}

// Signed distance from a pixel centre to a rounded box (negative inside).
inline float RoundBoxDistance(float px, float py, float cx, float cy, float hw, float hh, float r) {
    const float qx = std::fabs(px - cx) - (hw - r);
    const float qy = std::fabs(py - cy) - (hh - r);
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

} // namespace

Canvas::Canvas() : pixels(size_t(Width) * Height, 0xff000000u), clip{0, 0, Width, Height} {}

int Canvas::Alpha(float alpha) const {
    return static_cast<int>(Clamp01(alpha * opacity) * 256.0f + 0.5f);
}

void Canvas::Blend(int x, int y, uint32_t color, int alpha) {
    if (x < clip.x0 || x >= clip.x1 || y < clip.y0 || y >= clip.y1 || alpha <= 0) return;
    uint32_t& dst = pixels[size_t(y) * Width + x];
    dst = alpha >= 256 ? color : Mix(dst, color, alpha);
}

void Canvas::Span(int x0, int x1, int y, uint32_t color, int alpha) {
    if (y < clip.y0 || y >= clip.y1 || alpha <= 0) return;
    x0 = std::max(x0, clip.x0);
    x1 = std::min(x1, clip.x1);
    if (x0 >= x1) return;
    uint32_t* row = &pixels[size_t(y) * Width];
    if (alpha >= 256) {
        std::fill(row + x0, row + x1, color);
    } else {
        for (int x = x0; x < x1; ++x) row[x] = Mix(row[x], color, alpha);
    }
}

void Canvas::Clear(Color color) {
    std::fill(pixels.begin(), pixels.end(), Pack(color));
}

void Canvas::FillRect(float x, float y, float w, float h, Color color, float alpha) {
    const int a = Alpha(alpha);
    const int x0 = int(std::lround(x)), x1 = int(std::lround(x + w));
    const int y0 = std::max(int(std::lround(y)), clip.y0), y1 = std::min(int(std::lround(y + h)), clip.y1);
    const uint32_t packed = Pack(color);
    for (int row = y0; row < y1; ++row) Span(x0, x1, row, packed, a);
}

void Canvas::FillRoundRect(float x, float y, float w, float h, float radius, Color color, float alpha) {
    if (radius < 0.75f) return FillRect(x, y, w, h, color, alpha);
    radius = std::min({radius, w / 2, h / 2});
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const float cx = x + w / 2, cy = y + h / 2;
    const int y0 = std::max(int(std::floor(y)), clip.y0), y1 = std::min(int(std::ceil(y + h)), clip.y1);
    const int x0 = int(std::floor(x)), x1 = int(std::ceil(x + w));
    const int edge = int(std::ceil(radius)) + 2;
    const auto pixel = [&](int col, int row) {
        Blend(col, row, packed,
              int(a * Clamp01(0.5f - RoundBoxDistance(col + 0.5f, row + 0.5f, cx, cy, w / 2, h / 2, radius))));
    };
    for (int row = y0; row < y1; ++row) {
        const float py = row + 0.5f;
        if (py < y + radius + 1 || py > y + h - radius - 1) {
            // Rows through the corners: coverage only near the ends, a span between.
            for (int col = x0; col < std::min(x0 + edge, x1); ++col) pixel(col, row);
            for (int col = std::max(x1 - edge, x0 + edge); col < x1; ++col) pixel(col, row);
            if (py > y + 1 && py < y + h - 1) {
                Span(x0 + edge, x1 - edge, row, packed, a);
            } else {
                for (int col = x0 + edge; col < x1 - edge; ++col) pixel(col, row);
            }
        } else {
            for (int col = x0; col < std::min(x0 + 2, x1); ++col) pixel(col, row);
            Span(x0 + 2, x1 - 2, row, packed, a);
            for (int col = std::max(x1 - 2, x0 + 2); col < x1; ++col) pixel(col, row);
        }
    }
}

void Canvas::StrokeRoundRect(float x, float y, float w, float h, float radius, float thickness, Color color,
                             float alpha) {
    radius = std::min({radius, w / 2, h / 2});
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const float cx = x + w / 2, cy = y + h / 2;
    const float inner_r = std::max(0.0f, radius - thickness);
    const auto coverage = [&](int col, int row) {
        const float px = col + 0.5f, py = row + 0.5f;
        const float outer = Clamp01(0.5f - RoundBoxDistance(px, py, cx, cy, w / 2, h / 2, std::max(radius, 0.01f)));
        const float inner = Clamp01(0.5f - RoundBoxDistance(px, py, cx, cy, w / 2 - thickness, h / 2 - thickness,
                                                             std::max(inner_r, 0.01f)));
        return outer - inner;
    };
    const int y0 = std::max(int(std::floor(y)), clip.y0), y1 = std::min(int(std::ceil(y + h)), clip.y1);
    const int x0 = int(std::floor(x)), x1 = int(std::ceil(x + w));
    const int band = int(std::ceil(std::max(radius, thickness))) + 2;
    for (int row = y0; row < y1; ++row) {
        const bool full_row = row < int(y) + band || row >= int(y + h) - band;
        if (full_row) {
            for (int col = x0; col < x1; ++col) Blend(col, row, packed, int(a * coverage(col, row)));
        } else {
            for (int col = x0; col < std::min(x0 + band, x1); ++col) Blend(col, row, packed, int(a * coverage(col, row)));
            for (int col = std::max(x1 - band, x0 + band); col < x1; ++col)
                Blend(col, row, packed, int(a * coverage(col, row)));
        }
    }
}

void Canvas::FillCircle(float cx, float cy, float r, Color color, float alpha) {
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const int y0 = std::max(int(std::floor(cy - r)), clip.y0), y1 = std::min(int(std::ceil(cy + r)), clip.y1);
    for (int row = y0; row < y1; ++row) {
        const float dy = row + 0.5f - cy;
        if (std::fabs(dy) > r + 0.5f) continue;
        const float half = std::sqrt(std::max(0.0f, r * r - dy * dy));
        const int inner0 = int(std::ceil(cx - half + 1.0f)), inner1 = int(std::floor(cx + half - 1.0f));
        const int outer0 = int(std::floor(cx - half - 1.0f)), outer1 = int(std::ceil(cx + half + 1.0f));
        const auto edge = [&](int col) {
            const float dx = col + 0.5f - cx;
            Blend(col, row, packed, int(a * Clamp01(r - std::sqrt(dx * dx + dy * dy) + 0.5f)));
        };
        if (inner0 < inner1) {
            for (int col = outer0; col < inner0; ++col) edge(col);
            Span(inner0, inner1, row, packed, a);
            for (int col = inner1; col < outer1; ++col) edge(col);
        } else {
            for (int col = outer0; col < outer1; ++col) edge(col);
        }
    }
}

void Canvas::StrokeCircle(float cx, float cy, float r, float thickness, Color color, float alpha) {
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const float mid = r - thickness / 2;
    const int y0 = std::max(int(std::floor(cy - r - 1)), clip.y0), y1 = std::min(int(std::ceil(cy + r + 1)), clip.y1);
    const int x0 = int(std::floor(cx - r - 1)), x1 = int(std::ceil(cx + r + 1));
    for (int row = y0; row < y1; ++row) {
        const float dy = row + 0.5f - cy;
        for (int col = x0; col < x1; ++col) {
            const float dx = col + 0.5f - cx;
            const float d = std::fabs(std::sqrt(dx * dx + dy * dy) - mid);
            const float cov = Clamp01(thickness / 2 - d + 0.5f);
            if (cov > 0) Blend(col, row, packed, int(a * cov));
        }
    }
}

void Canvas::HLine(float x0, float x1, float y, float thickness, Color color, float alpha) {
    FillRect(x0, y - thickness / 2, x1 - x0, std::max(1.0f, thickness), color, alpha);
}

void Canvas::VLine(float x, float y0, float y1, float thickness, Color color, float alpha) {
    FillRect(x - thickness / 2, y0, std::max(1.0f, thickness), y1 - y0, color, alpha);
}

void Canvas::Mask(std::string_view name, float x, float y, Color color, float alpha) {
    const auto* mask = FindMask(name);
    if (!mask) return;
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const int ox = int(std::lround(x)), oy = int(std::lround(y));
    for (int row = 0; row < mask->h; ++row) {
        const int py = oy + row;
        if (py < clip.y0 || py >= clip.y1) continue;
        const uint8_t* src = mask->alpha + size_t(row) * mask->w;
        for (int col = 0; col < mask->w; ++col)
            if (src[col]) Blend(ox + col, py, packed, (a * (src[col] + 1)) >> 8);
    }
}

void Canvas::MaskCentered(std::string_view name, float cx, float cy, Color color, float alpha) {
    const auto* mask = FindMask(name);
    if (mask) Mask(name, cx - mask->w / 2.0f, cy - mask->h / 2.0f, color, alpha);
}

void Canvas::MaskScaled(std::string_view name, float cx, float cy, float size, Color color, float alpha) {
    const auto* mask = FindMask(name);
    if (!mask || size < 1) return;
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const float scale = float(mask->w) / size; // source pixels per output pixel
    const int w = int(std::lround(size)), h = int(std::lround(mask->h / scale));
    const int ox = int(std::lround(cx - w / 2.0f)), oy = int(std::lround(cy - h / 2.0f));
    const auto sample = [&](float sx, float sy) {
        // Bilinear tap of the mask.
        sx = std::clamp(sx - 0.5f, 0.0f, float(mask->w - 1));
        sy = std::clamp(sy - 0.5f, 0.0f, float(mask->h - 1));
        const int x0 = int(sx), y0 = int(sy), x1 = std::min(x0 + 1, mask->w - 1), y1 = std::min(y0 + 1, mask->h - 1);
        const float fx = sx - x0, fy = sy - y0;
        const auto at = [&](int xx, int yy) { return float(mask->alpha[size_t(yy) * mask->w + xx]); };
        return (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
    };
    // Four taps per output pixel keep downscaled edges smooth.
    const float q = scale * 0.25f;
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            const float sx = (col + 0.5f) * scale, sy = (row + 0.5f) * scale;
            const float v = (sample(sx - q, sy - q) + sample(sx + q, sy - q) + sample(sx - q, sy + q) +
                             sample(sx + q, sy + q)) * 0.25f;
            if (v >= 1) Blend(ox + col, oy + row, packed, int(a * v / 255.0f));
        }
    }
}

int Canvas::MaskWidth(std::string_view name) const {
    const auto* mask = FindMask(name);
    return mask ? mask->w : 0;
}

void Canvas::Shadow(float x, float y, float w, float h, float alpha) {
    const auto* mask = FindMask("shadow_square");
    if (!mask) return;
    const int a = Alpha(alpha);
    const int m = mask->w, half = m / 2, pad = 12;
    const int ox = int(std::lround(x)) - pad, oy = int(std::lround(y)) - pad;
    const int W = int(std::lround(w)) + pad * 2, H = int(std::lround(h)) + pad * 2;
    const uint32_t black = 0xff000000u;
    for (int row = 0; row < H; ++row) {
        const int py = oy + row;
        if (py < clip.y0 || py >= clip.y1) continue;
        const int sy = row < half ? row : row >= H - half ? m - (H - row) : half;
        const bool inner_row = row >= pad * 2 && row < H - pad * 2;
        for (int col = 0; col < W; ++col) {
            // The shadow only shows around the box: skip its covered middle.
            if (inner_row && col >= pad * 2 && col < W - pad * 2) { col = W - pad * 2 - 1; continue; }
            const int sx = col < half ? col : col >= W - half ? m - (W - col) : half;
            const uint8_t v = mask->alpha[size_t(sy) * m + sx];
            if (v) Blend(ox + col, py, black, (a * v) >> 8);
        }
    }
}

void Canvas::CircleShadow(float cx, float cy, float r, float alpha) {
    const auto* mask = FindMask("shadow_circle");
    if (!mask) return;
    const int a = Alpha(alpha);
    // The mask's circle has radius 68 inside a 164 square.
    const float scale = r / 68.0f;
    const int size = int(std::lround(mask->w * scale));
    const int ox = int(std::lround(cx - size / 2.0f)), oy = int(std::lround(cy - size / 2.0f + r * 0.04f));
    for (int row = 0; row < size; ++row) {
        const int py = oy + row;
        if (py < clip.y0 || py >= clip.y1) continue;
        const int sy = std::min(mask->h - 1, int(row / scale));
        for (int col = 0; col < size; ++col) {
            const uint8_t v = mask->alpha[size_t(sy) * mask->w + std::min(mask->w - 1, int(col / scale))];
            if (v) Blend(ox + col, py, 0xff000000u, (a * v) >> 8);
        }
    }
}

void Canvas::DrawImage(const Image& image, float x, float y, float alpha) {
    if (image.Empty()) return;
    const int a = Alpha(alpha);
    const int ox = int(std::lround(x)), oy = int(std::lround(y));
    const int c0 = std::max(0, clip.x0 - ox), c1 = std::min(image.width, clip.x1 - ox);
    if (c0 >= c1) return;
    for (int row = 0; row < image.height; ++row) {
        const int py = oy + row;
        if (py < clip.y0 || py >= clip.y1) continue;
        const uint32_t* src = &image.pixels[size_t(row) * image.width];
        uint32_t* dst = &pixels[size_t(py) * Width + ox];
        if (a >= 256) {
            std::memcpy(dst + c0, src + c0, size_t(c1 - c0) * 4);
        } else {
            for (int col = c0; col < c1; ++col) dst[col] = Mix(dst[col], src[col] | 0xff000000u, a);
        }
    }
}

void Canvas::DrawImageScaled(const Image& image, float x, float y, float w, float h, float alpha) {
    if (image.Empty() || w < 1 || h < 1) return;
    const int a = Alpha(alpha);
    const int ox = int(std::lround(x)), oy = int(std::lround(y));
    const int W = int(std::lround(w)), H = int(std::lround(h));
    for (int row = 0; row < H; ++row) {
        const int py = oy + row;
        if (py < clip.y0 || py >= clip.y1) continue;
        const uint32_t* src = &image.pixels[size_t(std::min(image.height - 1, row * image.height / H)) * image.width];
        for (int col = 0; col < W; ++col) {
            const int px = ox + col;
            if (px < clip.x0 || px >= clip.x1) continue;
            Blend(px, py, src[std::min(image.width - 1, col * image.width / W)] | 0xff000000u, a);
        }
    }
}

void Canvas::DrawImageCircle(const Image& image, float cx, float cy, float r) {
    if (image.Empty()) return;
    const int a = Alpha(1.0f);
    const int size = int(std::lround(r * 2));
    const int ox = int(std::lround(cx - r)), oy = int(std::lround(cy - r));
    for (int row = 0; row < size; ++row) {
        const float dy = row + 0.5f - r;
        const uint32_t* src = &image.pixels[size_t(std::min(image.height - 1, row * image.height / size)) * image.width];
        for (int col = 0; col < size; ++col) {
            const float dx = col + 0.5f - r;
            const float cov = Clamp01(r - std::sqrt(dx * dx + dy * dy) + 0.5f);
            if (cov > 0)
                Blend(ox + col, oy + row, src[std::min(image.width - 1, col * image.width / size)] | 0xff000000u,
                      int(a * cov));
        }
    }
}

float Canvas::Measure(std::string_view text, int size) const {
    const auto& font = FontFor(size);
    float width = 0;
    for (size_t i = 0; i < text.size();) {
        const auto* glyph = FindGlyph(font, NextCodepoint(text, i));
        if (glyph) width += glyph->advance;
    }
    return width;
}

void Canvas::Text(std::string_view text, float x, float y, int size, Color color, Align align, float max_width,
                  float alpha) {
    std::string clipped;
    if (max_width > 0 && Measure(text, size) > max_width) {
        // Cut whole code points until the text and "..." fit.
        const float dots = Measure("...", size);
        size_t end = text.size();
        while (end > 0) {
            do { --end; } while (end > 0 && (static_cast<uint8_t>(text[end]) & 0xc0) == 0x80);
            if (Measure(text.substr(0, end), size) + dots <= max_width) break;
        }
        clipped = std::string{text.substr(0, end)} + "...";
        text = clipped;
    }
    const auto& font = FontFor(size);
    const float width = Measure(text, size);
    float pen = align == Align::Left ? x : align == Align::Center ? x - width / 2 : x - width;
    const int a = Alpha(alpha);
    const uint32_t packed = Pack(color);
    const int baseline = int(std::lround(y));
    for (size_t i = 0; i < text.size();) {
        const auto* glyph = FindGlyph(font, NextCodepoint(text, i));
        if (!glyph) continue;
        const int gx = int(std::lround(pen)) + glyph->left, gy = baseline + glyph->top;
        for (int row = 0; row < glyph->h; ++row) {
            const int py = gy + row;
            if (py < clip.y0 || py >= clip.y1) continue;
            const uint8_t* src = font.atlas + size_t(glyph->y + row) * font.atlas_w + glyph->x;
            for (int col = 0; col < glyph->w; ++col)
                if (src[col]) Blend(gx + col, py, packed, (a * (src[col] + 1)) >> 8);
        }
        pen += glyph->advance;
    }
}

std::vector<std::string> Canvas::Wrap(std::string_view text, int size, float width) const {
    std::vector<std::string> lines;
    std::string line;
    size_t start = 0;
    while (start <= text.size()) {
        size_t space = text.find_first_of(" \n", start);
        if (space == std::string_view::npos) space = text.size();
        const std::string word{text.substr(start, space - start)};
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && Measure(candidate, size) > width) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        if (space < text.size() && text[space] == '\n') {
            lines.push_back(line);
            line.clear();
        }
        start = space + 1;
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void Canvas::PushClip(float x, float y, float w, float h) {
    clips.push_back(clip);
    clip = {std::max(clip.x0, int(std::floor(x))), std::max(clip.y0, int(std::floor(y))),
            std::min(clip.x1, int(std::ceil(x + w))), std::min(clip.y1, int(std::ceil(y + h)))};
}

void Canvas::PopClip() {
    if (clips.empty()) return;
    clip = clips.back();
    clips.pop_back();
}

void Canvas::PushOpacity(float alpha) {
    opacities.push_back(opacity);
    opacity *= Clamp01(alpha);
}

void Canvas::PopOpacity() {
    if (opacities.empty()) return;
    opacity = opacities.back();
    opacities.pop_back();
}

Image DecodeImage(const std::vector<uint8_t>& data, int w, int h) {
    int sw = 0, sh = 0, channels = 0;
    uint8_t* decoded = stbi_load_from_memory(data.data(), int(data.size()), &sw, &sh, &channels, 4);
    if (!decoded) return {};
    Image source{sw, sh, std::vector<uint32_t>(size_t(sw) * sh)};
    std::memcpy(source.pixels.data(), decoded, source.pixels.size() * 4);
    stbi_image_free(decoded);
    return (sw == w && sh == h) ? source : Resize(source, w, h);
}

Image Resize(const Image& image, int w, int h) {
    if (image.Empty()) return {};
    Image out{w, h, std::vector<uint32_t>(size_t(w) * h)};
    stbir_resize_uint8(reinterpret_cast<const unsigned char*>(image.pixels.data()), image.width, image.height, 0,
                       reinterpret_cast<unsigned char*>(out.pixels.data()), w, h, 0, 4);
    return out;
}

} // namespace Ui
