// 颜色缓冲 + 深度缓冲。
// 颜色存"线性空间浮点"（HDR），最后统一做曝光 + 色调映射再转 8 位 —— 这是现代渲染管线的标准做法，
// 也是为什么"直接把亮度值当像素颜色"会在亮部糊成一片白。
#pragma once

#include <cstdint>
#include <vector>

#include "vec.h"

namespace dlab {

struct Framebuffer {
    int width = 0;
    int height = 0;
    std::vector<Vec3> color;   // 线性 RGB
    std::vector<float> depth;  // NDC z（-1 近 ~ 1 远），越小越近

    void resize(int w, int h) {
        width = w > 0 ? w : 1;
        height = h > 0 ? h : 1;
        color.assign(size_t(width) * size_t(height), Vec3{0.0f, 0.0f, 0.0f});
        depth.assign(size_t(width) * size_t(height), 1.0f);
    }

    void clear(Vec3 c = Vec3{0.0f, 0.0f, 0.0f}) {
        for (Vec3& px : color) px = c;
        for (float& d : depth) d = 1.0f;
    }

    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width && y < height; }

    // 直接写入（用于 UI 叠加层，不参与深度测试）
    void setPixel(int x, int y, Vec3 c) {
        if (inBounds(x, y)) color[size_t(y) * size_t(width) + size_t(x)] = c;
    }

    // alpha 混合写入（控制台面板、文字都要用）
    void blendPixel(int x, int y, Vec3 c, float alpha) {
        if (!inBounds(x, y)) return;
        Vec3& dst = color[size_t(y) * size_t(width) + size_t(x)];
        dst = lerp(dst, c, alpha);
    }

    // 截图里的"画面平均亮度"：关卡判定直接用它（例如"把这间屋子点亮"）
    float meanLuminance() const {
        if (color.empty()) return 0.0f;
        double sum = 0.0;
        for (const Vec3& c : color) sum += double(luminance(c));
        return float(sum / double(color.size()));
    }

    // ACES 近似曲线（Narkowicz）：把 HDR 亮度压进 [0,1] 且保留高光层次
    static float tonemapACES(float x) {
        const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        const float num = x * (a * x + b);
        const float den = x * (c * x + d) + e;
        return clampf(num / den, 0.0f, 1.0f);
    }

    // 线性 → sRGB 传输曲线
    static float linearToSRGB(float v) {
        const float c = clampf(v, 0.0f, 1.0f);
        return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    }

    std::vector<uint8_t> toRGB8(float exposure = 1.0f, bool filmic = true) const {
        std::vector<uint8_t> out(size_t(width) * size_t(height) * 3);
        for (size_t i = 0; i < color.size(); ++i) {
            Vec3 c = color[i] * exposure;
            if (filmic) c = Vec3{tonemapACES(c.x), tonemapACES(c.y), tonemapACES(c.z)};
            out[i * 3 + 0] = uint8_t(linearToSRGB(c.x) * 255.0f + 0.5f);
            out[i * 3 + 1] = uint8_t(linearToSRGB(c.y) * 255.0f + 0.5f);
            out[i * 3 + 2] = uint8_t(linearToSRGB(c.z) * 255.0f + 0.5f);
        }
        return out;
    }
};

}  // namespace dlab
