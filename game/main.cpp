// dreamlab-rt —— 逐梦创新实验室 · 数媒组
// 主程序：命令行解析 + 场景装配 + 渲染循环。
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../core/font.h"
#include "../core/framebuffer.h"
#include "../core/material.h"
#include "../core/png_write.h"
#include "../core/raster.h"
#include "../core/texture.h"
#include "../engine/mesh.h"
#include "../engine/world.h"
#include "workshop.h"

using namespace dlab;

namespace {

// ---------------------------------------------------------------- 命令行

struct Args {
    std::string shot = "build/out.png";  // 截图输出路径
    int width = 480;
    int height = 270;
    int frames = 1;                      // 离屏模式下运行的帧数
    int threads = 0;                     // 0 = 自动
    float exposure = 1.0f;
    float fovDeg = 0.0f;                 // 0 = 用场景默认
    Vec3 cam{0.0f, 0.0f, 0.0f};
    Vec3 look{0.0f, 0.0f, 0.0f};
    bool hasCam = false;
    bool hasLook = false;
    bool selftest = false;
    bool preview = false;                // 把字模画成终端 ASCII 图
    std::string previewText;
    bool hud = false;                    // 叠一层文字 HUD（验证中文字模进 PNG 的整条链）
    bool help = false;
};

void printUsage() {
    std::printf(
        "dreamlab-rt —— 逐梦实验室数媒组示例项目\n"
        "\n"
        "用法: dreamlab [选项]\n"
        "  --shot <路径>      截图输出路径（默认 build/out.png）\n"
        "  --width <像素>     渲染宽度（默认 480）\n"
        "  --height <像素>    渲染高度（默认 270）\n"
        "  --frames <帧数>    离屏模式运行的帧数（默认 1）\n"
        "  --threads <数量>   渲染线程数（默认自动）\n"
        "  --exposure <倍数>  曝光（默认 1.0）\n"
        "  --fov <角度>       竖直视场角（默认用场景的 50°）\n"
        "  --cam x,y,z        相机位置\n"
        "  --look x,y,z       相机看向的点\n"
        "  --preview [文本]   把中文字模画成终端 ASCII 图（检查字模是否完好）\n"
        "  --hud              在画面上叠一层文字（验证中文渲染进 PNG）\n"
        "  --selftest         运行内置自检\n"
        "  -h, --help         显示本帮助\n");
}

const char* takeValue(int argc, char** argv, int& i, const char* name) {
    if (i + 1 >= argc) {
        std::fprintf(stderr, "[错误] %s 需要一个参数值\n", name);
        std::exit(2);
    }
    return argv[++i];
}

// 解析 "1.5,-2,3" 这种三元组
bool parseVec3(const char* text, Vec3& out) {
    return std::sscanf(text, "%f,%f,%f", &out.x, &out.y, &out.z) == 3;
}

Args parseArgs(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        const std::string s = argv[i];
        if (s == "--shot") {
            a.shot = takeValue(argc, argv, i, "--shot");
        } else if (s == "--width") {
            a.width = std::atoi(takeValue(argc, argv, i, "--width"));
        } else if (s == "--height") {
            a.height = std::atoi(takeValue(argc, argv, i, "--height"));
        } else if (s == "--frames") {
            a.frames = std::atoi(takeValue(argc, argv, i, "--frames"));
        } else if (s == "--threads") {
            a.threads = std::atoi(takeValue(argc, argv, i, "--threads"));
        } else if (s == "--exposure") {
            a.exposure = float(std::atof(takeValue(argc, argv, i, "--exposure")));
        } else if (s == "--fov") {
            a.fovDeg = float(std::atof(takeValue(argc, argv, i, "--fov")));
        } else if (s == "--cam") {
            a.hasCam = parseVec3(takeValue(argc, argv, i, "--cam"), a.cam);
        } else if (s == "--look") {
            a.hasLook = parseVec3(takeValue(argc, argv, i, "--look"), a.look);
        } else if (s == "--preview") {
            a.preview = true;
            // 文本是可选的；后面跟着的不是选项就当作要预览的文本
            if (i + 1 < argc && argv[i + 1][0] != '-') a.previewText = argv[++i];
        } else if (s == "--hud") {
            a.hud = true;
        } else if (s == "--selftest") {
            a.selftest = true;
        } else if (s == "-h" || s == "--help") {
            a.help = true;
        } else {
            std::fprintf(stderr, "[错误] 未知参数: %s（用 --help 查看用法）\n", s.c_str());
            std::exit(2);
        }
    }
    if (a.width < 1) a.width = 1;
    if (a.height < 1) a.height = 1;
    if (a.frames < 1) a.frames = 1;
    if (a.exposure <= 0.0f) a.exposure = 1.0f;
    return a;
}

// ---------------------------------------------------------------- 场景

struct Scene {
    World world;
    Workshop workshop;
    Camera camera;
};

void buildScene(Scene& s) {
    s.workshop = buildWorkshop(s.world);
    s.camera.position = Vec3{0.0f, 1.62f, 4.5f};
    s.camera.yaw = 0.0f;
    s.camera.pitch = -0.06f;
}

// 把"看向某个点"换算成偏航/俯仰（相机用欧拉角存，玩家输入天然就是这两个角）
void lookAtPoint(Camera& cam, Vec3 target) {
    const Vec3 d = normalize(target - cam.position);
    cam.yaw = std::atan2(-d.x, -d.z);
    cam.pitch = std::asin(clampf(d.y, -1.0f, 1.0f));
}

// 每帧把整场景重新提交一次（P0 不做场景图缓存，先把管线跑通）
void renderFrame(Rasterizer& rz, const Scene& s, float timeSeconds) {
    (void)timeSeconds;
    s.world.render(rz, s.camera);
}

// ---------------------------------------------------------------- HUD
// 文字直接叠进颜色缓冲，不参与深度测试。颜色是线性 HDR 且最后统一过 ACES，
// 所以"纯白文字"要给 2.2 左右 —— 给 1.0 出来是灰的（渲染顺序决定的，不是 bug）。
void drawHud(Framebuffer& fb, const Font& font, float fps) {
    const Vec3 white{2.2f, 2.2f, 2.25f};
    const Vec3 dim{1.5f, 1.5f, 1.55f};
    const Vec3 panel{0.02f, 0.025f, 0.04f};
    const int pad = 6;

    auto panelRect = [&](int x, int y, int w, int h) {
        for (int gy = 0; gy < h; ++gy)
            for (int gx = 0; gx < w; ++gx) fb.blendPixel(x + gx, y + gy, panel, 0.68f);
    };

    // 标题：2 倍字号，验证中文放大后依然是干净的像素字
    const std::string title = "数媒组工作室";
    const int titleW = font.measureLine(title) * 2;
    panelRect(8, 8, titleW + pad * 2, font.glyphH() * 2 + pad * 2);
    font.drawLine(fb, 8 + pad, 8 + pad, title, white, 1.0f, 2);

    // 状态行：1 倍字号，中文 + 拉丁 + 数字混排，顺便验证比例字距
    char stats[96];
    std::snprintf(stats, sizeof(stats), "DreamLab 2026 · %.0f FPS · %dx%d", double(fps), fb.width, fb.height);
    const int statsW = font.measureLine(stats);
    const int statsY = fb.height - font.glyphH() - pad - 8;
    panelRect(8, statsY - pad, statsW + pad * 2, font.glyphH() + pad * 2);
    font.drawLine(fb, 8 + pad, statsY, stats, dim, 1.0f, 1);
}

// ---------------------------------------------------------------- 自检

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("[selftest] 失败: %s\n", what);
    }
}

void checkClose(float got, float want, float tol, const char* what) {
    ++g_checks;
    if (!(std::fabs(got - want) <= tol)) {
        ++g_failures;
        std::printf("[selftest] 失败: %s（期望 %.5f，实际 %.5f）\n", what, want, got);
    }
}

// 造一个覆盖整个 NDC 的大三角形，四个角全包住
Mesh makeFullScreenTriangle(float z) {
    Mesh m;
    m.vertices.push_back(Vertex{Vec3{-1.0f, -1.0f, z}, Vec3{0.0f, 0.0f, -1.0f}, Vec2{0.0f, 0.0f}, Vec3{1, 1, 1}});
    m.vertices.push_back(Vertex{Vec3{5.0f, -1.0f, z}, Vec3{0.0f, 0.0f, -1.0f}, Vec2{1.0f, 0.0f}, Vec3{1, 1, 1}});
    m.vertices.push_back(Vertex{Vec3{-1.0f, 5.0f, z}, Vec3{0.0f, 0.0f, -1.0f}, Vec2{0.0f, 1.0f}, Vec3{1, 1, 1}});
    m.indices = {0, 1, 2};
    return m;
}

void testMath() {
    const Vec3 x{1.0f, 0.0f, 0.0f};
    const Vec3 y{0.0f, 1.0f, 0.0f};
    const Vec3 z{0.0f, 0.0f, 1.0f};

    checkClose(dot(x, y), 0.0f, 1e-6f, "dot：正交基内积为 0");
    checkClose(cross(x, y).z, 1.0f, 1e-6f, "cross：x × y = z");
    checkClose(length(normalize(Vec3{3.0f, 4.0f, 0.0f})), 1.0f, 1e-6f, "normalize：长度为 1");

    // 绕 y 轴转 90°：(1,0,0) 应该转到 (0,0,-1)
    const Vec3 rotated = transformDir(rotationY(radians(90.0f)), x);
    checkClose(rotated.x, 0.0f, 1e-5f, "rotationY(90°) 的 x 分量");
    checkClose(rotated.z, -1.0f, 1e-5f, "rotationY(90°) 的 z 分量");

    // 相机看向原点：目标点应落在 NDC 正中心
    const Mat4 view = lookAt(Vec3{0.0f, 0.0f, 5.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    const Mat4 proj = perspective(radians(60.0f), 1.0f, 0.1f, 100.0f);
    const Vec4 clip = (proj * view) * Vec4(Vec3{0.0f, 0.0f, 0.0f}, 1.0f);
    check(clip.w > 0.0f, "投影：目标点在相机前方（w > 0）");
    checkClose(clip.x / clip.w, 0.0f, 1e-4f, "投影：目标点 NDC x = 0");
    checkClose(clip.y / clip.w, 0.0f, 1e-4f, "投影：目标点 NDC y = 0");

    // 相机在 z=5 看向 -z：前方 0.1 处正好是近裁剪面，前方 100 处是远裁剪面
    const Vec4 nearClip = (proj * view) * Vec4(Vec3{0.0f, 0.0f, 4.9f}, 1.0f);
    checkClose(nearClip.z / nearClip.w, -1.0f, 1e-3f, "投影：近裁剪面 → NDC z = -1");
    const Vec4 farClip = (proj * view) * Vec4(Vec3{0.0f, 0.0f, -95.0f}, 1.0f);
    checkClose(farClip.z / farClip.w, 1.0f, 1e-3f, "投影：远裁剪面 → NDC z = +1");

    // 矩阵求逆：M * M⁻¹ = I
    const Mat4 m = translation(Vec3{1.0f, 2.0f, 3.0f}) * rotationY(radians(37.0f)) * scaling(Vec3{2.0f, 0.5f, 1.3f});
    const Mat4 prod = m * inverse(m);
    float maxErr = 0.0f;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) maxErr = maxf(maxErr, std::fabs(prod.at(r, c) - (r == c ? 1.0f : 0.0f)));
    checkClose(maxErr, 0.0f, 1e-3f, "矩阵求逆：M × M⁻¹ = 单位阵");

    // 非等比缩放下，法线矩阵必须让法线与切向量保持垂直
    const Mat4 squash = scaling(Vec3{1.0f, 4.0f, 0.6f}) * rotationY(radians(30.0f));
    const Vec3 tWorld = transformDir(squash, x);
    const Vec3 nWorld = normalize(transformDir(normalMatrix(squash), y));
    checkClose(dot(tWorld, nWorld), 0.0f, 1e-3f, "法线矩阵：非等比缩放下仍与切向量垂直");

    checkClose(Framebuffer::tonemapACES(0.0f), 0.0f, 1e-6f, "色调映射：0 映射到 0");
    check(Framebuffer::tonemapACES(100.0f) > 0.95f, "色调映射：高光收敛到 1 附近");
    check(Framebuffer::tonemapACES(1.0f) > Framebuffer::tonemapACES(0.5f), "色调映射：单调递增");
}

void testRasterizer() {
    Rasterizer rz(2);
    rz.resize(64, 48);
    const Mat4 identity;
    ShadeEnv env;  // 无灯、无环境光：只留下自发光

    Material red;
    red.albedo = Vec3{0.0f, 0.0f, 0.0f};
    red.emissive = Vec3{1.0f, 0.0f, 0.0f};

    Material green;
    green.albedo = Vec3{0.0f, 0.0f, 0.0f};
    green.emissive = Vec3{0.0f, 1.0f, 0.0f};

    const Mesh nearTri = makeFullScreenTriangle(0.5f);   // 深度 0.75
    const Mesh farTri = makeFullScreenTriangle(0.8f);    // 深度 0.90

    const size_t centerIndex = size_t(24) * 64 + size_t(32);

    // ① 能不能画出一个像素
    rz.framebuffer().clear();
    rz.begin(identity, identity, Vec3{0.0f, 0.0f, 0.0f}, env);
    rz.draw(nearTri, identity, red);
    rz.flush();
    checkClose(rz.framebuffer().color[centerIndex].x, 1.0f, 1e-5f, "光栅化：屏幕中心被着色");
    checkClose(rz.framebuffer().color[centerIndex].y, 0.0f, 1e-5f, "光栅化：颜色来自着色器");
    checkClose(rz.framebuffer().depth[centerIndex], 0.75f, 1e-4f, "光栅化：深度正确插值");

    // ② 深度测试：远处的东西不能盖住近处的
    rz.framebuffer().clear();
    rz.begin(identity, identity, Vec3{0.0f, 0.0f, 0.0f}, env);
    rz.draw(nearTri, identity, red);
    rz.draw(farTri, identity, green);
    rz.flush();
    checkClose(rz.framebuffer().color[centerIndex].x, 1.0f, 1e-5f, "深度测试：近处的红留下");
    checkClose(rz.framebuffer().color[centerIndex].y, 0.0f, 1e-5f, "深度测试：远处的绿被挡住");

    // ③ 反过来的顺序也应该是红赢
    rz.framebuffer().clear();
    rz.begin(identity, identity, Vec3{0.0f, 0.0f, 0.0f}, env);
    rz.draw(farTri, identity, green);
    rz.draw(nearTri, identity, red);
    rz.flush();
    checkClose(rz.framebuffer().color[centerIndex].x, 1.0f, 1e-5f, "深度测试：换顺序仍是近处胜出");

    // ④ 近平面裁剪：有一个顶点跑到相机背后，三角形既不能崩，也不能整块丢掉
    const Mat4 proj = perspective(radians(60.0f), 1.0f, 0.1f, 100.0f);  // 相机在原点看向 -z
    Mesh straddling;
    straddling.vertices.push_back(Vertex{Vec3{-0.1f, -0.1f, -1.0f}, Vec3{0, 0, -1}, Vec2{0, 0}, Vec3{1, 1, 1}});
    straddling.vertices.push_back(Vertex{Vec3{0.1f, -0.1f, -1.0f}, Vec3{0, 0, -1}, Vec2{1, 0}, Vec3{1, 1, 1}});
    straddling.vertices.push_back(Vertex{Vec3{0.0f, 0.1f, 0.2f}, Vec3{0, 0, -1}, Vec2{0.5f, 1}, Vec3{1, 1, 1}});
    straddling.indices = {0, 1, 2};

    rz.framebuffer().clear();
    rz.begin(Mat4{}, proj, Vec3{0.0f, 0.0f, 0.0f}, env);
    rz.draw(straddling, Mat4{}, red);
    rz.flush();

    check(rz.drawnTriangles() > 0, "近平面裁剪：跨过相机的三角形被切成可见的部分");
    int redPixels = 0;
    for (const Vec3& c : rz.framebuffer().color)
        if (c.x > 0.5f) ++redPixels;
    const int totalPixels = rz.framebuffer().width * rz.framebuffer().height;
    check(redPixels > 0, "近平面裁剪：可见部分确实画出来了");
    check(redPixels < totalPixels / 2, "近平面裁剪：相机背后的部分被裁掉（没糊满全屏）");
}

// 把码点编成 UTF-8（自检里用来做往返测试）
std::string utf8Encode(uint32_t cp) {
    std::string s;
    if (cp < 0x80u) {
        s += char(cp);
    } else if (cp < 0x800u) {
        s += char(0xC0u | (cp >> 6));
        s += char(0x80u | (cp & 0x3Fu));
    } else if (cp < 0x10000u) {
        s += char(0xE0u | (cp >> 12));
        s += char(0x80u | ((cp >> 6) & 0x3Fu));
        s += char(0x80u | (cp & 0x3Fu));
    } else {
        s += char(0xF0u | (cp >> 18));
        s += char(0x80u | ((cp >> 12) & 0x3Fu));
        s += char(0x80u | ((cp >> 6) & 0x3Fu));
        s += char(0x80u | (cp & 0x3Fu));
    }
    return s;
}

// 字模自检。这里断言的具体数字都是照着 assets/font/pixel12.bin 实测的，
// 换字体时它们会第一时间炸，而不是等到游戏里文字变糊才发现。
void testFont() {
    Font font;
    const bool ok = font.loadFromFile("assets/font/pixel12.bin");
    check(ok, "字模：assets/font/pixel12.bin 能加载（缺了就重跑 tools/gen_font_atlas.py）");
    if (!ok) return;

    check(font.glyphW() == 12 && font.glyphH() == 12, "字模：格子 12x12");
    check(font.glyphCount() > 6000, "字模：字数覆盖 GB2312 常用字（>6000）");
    check(font.advanceOf(' ') > 0 && font.advanceOf(' ') < 12, "字模：半角空格是窄步进");

    // 二分查找的前提：码点表严格升序
    bool ascending = true;
    for (int i = 1; i < font.glyphCount(); ++i)
        if (font.codepointAt(i - 1) >= font.codepointAt(i)) ascending = false;
    check(ascending, "字模：码点表严格升序");

    // 取一个字的墨迹纵向范围
    int top = 0;
    int bottom = 0;
    auto inkRows = [&](uint32_t cp) {
        top = -1;
        bottom = -1;
        const int idx = font.indexOf(cp);
        if (idx < 0) return;
        for (int y = 0; y < font.glyphH(); ++y)
            for (int x = 0; x < font.glyphW(); ++x)
                if (font.inkAt(idx, x, y)) {
                    if (top < 0) top = y;
                    bottom = y;
                }
    };

    // 汉字要"顶天立地"：这曾是一个真实 bug —— 基线按 ascent 算，导致汉字顶部被切掉
    inkRows(0x4E2Du);  // 中
    check(top == 0, "字模：汉字顶到第 0 行（基线校准正确，没切头）");
    check(bottom == 10, "字模：汉字落在基线第 10 行");
    check(font.advanceOf(0x4E2Du) == 12, "字模：汉字占满全宽 12px");

    // ASCII 的 descender 不能被切：g 的尾巴要伸到基线以下
    inkRows('g');
    check(top >= 3 && bottom == 11, "字模：小写 g 的尾巴伸到第 11 行（descender 没被切）");
    check(font.advanceOf('A') < 12, "字模：ASCII 是比例宽度，不是全角");

    inkRows(' ');
    check(top < 0, "字模：空格不落墨");

    // 字集外的码点要明确报"没有"，而不是猜一个形状出来
    check(font.indexOf(0x2F00u) < 0, "字模：字集外的码点返回未命中");
    check(font.advanceOf(0x2F00u) == 12, "字模：未命中的字按一格宽度前进");

    // UTF-8 解码：1/2/3/4 字节都要解对，非法字节不能原地打转
    const std::string round = utf8Encode(0x4E2Du) + utf8Encode('A') + utf8Encode(0x1F600u);
    size_t i = 0;
    check(utf8Next(round, i) == 0x4E2Du, "UTF-8：解出 3 字节汉字");
    check(utf8Next(round, i) == 'A', "UTF-8：解出 1 字节 ASCII");
    check(utf8Next(round, i) == 0x1F600u, "UTF-8：解出 4 字节码点");
    check(i == round.size(), "UTF-8：刚好解完整串");
    const std::string bad = "\xFF\xE4";
    size_t j = 0;
    utf8Next(bad, j);
    check(j == 1, "UTF-8：非法首字节只前进 1 字节");

    // 排版宽度 = 逐字步进之和（比例字体，不是等宽）
    const std::string zh = utf8Encode(0x4E2Du) + utf8Encode(0x6587u);  // 中文
    check(font.measureLine(zh) == 24, "排版：两个汉字宽 24px");
    check(font.measureLine(utf8Encode('A') + utf8Encode('A')) == 2 * font.advanceOf('A'),
          "排版：ASCII 宽度按步进表累加");

    // 真的往帧缓冲画一遍：笔画要落下去
    Framebuffer fb;
    fb.resize(font.measureLine(zh) + 2, font.glyphH() + 2);
    fb.clear();
    const int drawnW = font.drawLine(fb, 0, 0, zh, Vec3{1.0f, 1.0f, 1.0f});
    check(drawnW == 24, "排版：drawLine 返回推进宽度 24");
    int lit = 0;
    for (const Vec3& c : fb.color)
        if (c.x > 0.5f) ++lit;
    check(lit > 60, "排版：汉字笔画确实画进了帧缓冲");

    // 多行：drawText 按 '\n' 换行，高度 = 两行字 + 一行行距
    const std::string twoLines = zh + "\n" + utf8Encode(0x5149u);  // 中文\n光
    check(font.measureText(twoLines) == 24, "排版：多行取最宽一行");
    Framebuffer fb2;
    fb2.resize(24 + 2, font.lineHeight() * 2 + 2);
    fb2.clear();
    check(font.drawText(fb2, 0, 0, twoLines, Vec3{1.0f, 1.0f, 1.0f}) == font.glyphH() * 2 + Font::kLineGap,
          "排版：drawText 返回两行高度（末行不留行距）");

    // 2 倍缩放：控制台面板大字号要用
    Framebuffer fb3;
    fb3.resize(48 + 4, 24 + 4);
    fb3.clear();
    check(font.drawLine(fb3, 0, 0, zh, Vec3{1.0f, 1.0f, 1.0f}, 1.0f, 2) == 48, "排版：2 倍缩放宽度翻倍");

    // 缺字：画空心框占位，既不崩也不白屏
    Framebuffer fb4;
    fb4.resize(font.glyphW() + 2, font.glyphH() + 2);
    fb4.clear();
    font.drawLine(fb4, 0, 0, utf8Encode(0x2F00u), Vec3{1.0f, 1.0f, 1.0f});
    int tofu = 0;
    for (const Vec3& c : fb4.color)
        if (c.x > 0.5f) ++tofu;
    check(tofu > 20 && tofu < 100, "字模：缺字画成空心框占位（不是实心块，也不是空白）");
    check(font.missingGlyphs() > 0, "字模：缺字会被计数（诊断用）");
}

// --preview：把字模画成终端里的 ASCII 图。
// 没有单元测试框架时，可视化就是最好的验证 —— 汉字结构、基线、字距一眼可查。
int runPreview(const std::string& textIn, int scale) {
    Font font;
    if (!font.loadFromFile("assets/font/pixel12.bin")) return 1;
    const std::string text = textIn.empty() ? std::string("黑光材质 DreamLab 2026") : textIn;

    std::printf("[font] %s：%dx%d，共 %d 字，行高 %d\n", font.path().c_str(), font.glyphW(), font.glyphH(),
                font.glyphCount(), font.lineHeight());
    std::printf("[font] 预览文本码点：");
    size_t i = 0;
    while (i < text.size()) {
        const uint32_t cp = utf8Next(text, i);
        if (cp == '\n') {
            std::printf("\\n ");
        } else {
            std::printf("U+%04X ", unsigned(cp));
        }
    }
    std::printf("\n");

    int lines = 1;
    for (char ch : text)
        if (ch == '\n') ++lines;
    const int w = font.measureText(text);
    const int h = font.glyphH() * lines + Font::kLineGap * (lines - 1);

    Framebuffer fb;
    fb.resize(w > 0 ? w : 1, h > 0 ? h : 1);
    fb.clear();
    font.drawText(fb, 0, 0, text, Vec3{1.0f, 1.0f, 1.0f}, 1.0f, scale);
    for (int y = 0; y < h; ++y) {
        std::string row = "  ";
        for (int x = 0; x < w; ++x) row += fb.color[size_t(y) * size_t(w) + size_t(x)].x > 0.5f ? '#' : '.';
        std::printf("%s\n", row.c_str());
    }
    std::printf("[font] 缺字 %d 个（空心框即缺字）\n", font.missingGlyphs());
    return 0;
}

void testWorld() {
    World w;
    const Workshop ws = buildWorkshop(w);
    (void)ws;

    // 房间尺寸：地板是 12x12 的平面，y 恰好贴地
    const Entity* floor = w.entity("地板");
    check(floor != nullptr, "世界：找得到「地板」");
    if (floor != nullptr) {
        checkClose(floor->aabbMin.x, -6.0f, 1e-3f, "世界：地板包围盒 x 下界");
        checkClose(floor->aabbMax.x, 6.0f, 1e-3f, "世界：地板包围盒 x 上界");
        checkClose(floor->aabbMax.y, 0.0f, 1e-3f, "世界：地板贴地（y 上界 = 0）");
    }

    // 墙的内表面要正好落在 ±6 上，否则玩家会卡在墙里或走进虚空
    const Entity* back = w.entity("后墙");
    const Entity* front = w.entity("前墙");
    check(back != nullptr && front != nullptr, "世界：找得到前后墙");
    if (back != nullptr) checkClose(back->aabbMax.z, -6.0f, 1e-3f, "世界：后墙内表面 z = -6");
    if (front != nullptr) checkClose(front->aabbMin.z, 6.0f, 1e-3f, "世界：前墙内表面 z = +6");

    // 斜着摆的木箱：8 角点变换后的 AABB 一定要比没转时更大（否则就是漏算了旋转）
    // 边长 0.8 的箱子绕 y 转 24°，AABB 的 x 方向尺寸应该 ≈ 0.8*(cos24+sin24) ≈ 1.06
    const Entity* crate = w.entity("木箱");
    check(crate != nullptr, "世界：找得到「木箱」");
    if (crate != nullptr) {
        const float sx = crate->aabbMax.x - crate->aabbMin.x;
        checkClose(sx, 1.056f, 0.01f, "世界：斜摆放的木箱 AABB 尺寸（旋转被算进去了）");
    }

    // 碰撞查询：出生点要空、展台里要有东西、墙里要有东西
    check(!w.overlapsSolid(Vec3{0.0f, 1.62f, 4.5f}, 0.3f), "碰撞：出生点没被堵住");
    check(w.overlapsSolid(Vec3{3.6f, 0.5f, -3.0f}, 0.3f), "碰撞：展台A 挡住人");
    check(w.overlapsSolid(Vec3{0.0f, 1.62f, 6.1f}, 0.3f), "碰撞：前墙挡住人");
    check(!w.overlapsSolid(Vec3{0.0f, 1.62f, -4.0f}, 0.3f), "碰撞：桌子前方可以站人");

    // 交互目标必须挂上命令，否则按 E 会没反应
    for (int i = 0; i < 3; ++i) {
        const Entity& orb = w.entities[size_t(ws.entOrbs[i])];
        check(!orb.prompt.empty() && !orb.command.empty(), "世界：展台上的球可以交互");
    }

    // 材质按名字能找回来 —— content/materials.txt 就靠这个对上号
    check(w.findMaterial("chrome") >= 0, "世界：材质表里有 chrome");
    check(w.findMaterial("不存在的材质") < 0, "世界：找不到的材质返回 -1");
}

int runSelfTest() {
    testMath();
    testRasterizer();
    testFont();
    testWorld();
    if (g_failures == 0) {
        std::printf("[selftest] %d 项检查全部通过\n", g_checks);
        return 0;
    }
    std::printf("[selftest] %d/%d 项失败\n", g_failures, g_checks);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const Args args = parseArgs(argc, argv);
    if (args.help) {
        printUsage();
        return 0;
    }

    if (args.selftest) return runSelfTest();
    if (args.preview) return runPreview(args.previewText, 1);

    Scene scene;
    buildScene(scene);
    if (args.hasCam) scene.camera.position = args.cam;
    if (args.hasLook) lookAtPoint(scene.camera, args.look);
    if (args.fovDeg > 0.0f) scene.camera.fovY = radians(args.fovDeg);

    Rasterizer rz(args.threads);
    rz.resize(args.width, args.height);

    std::printf("[dreamlab-rt] 渲染 %dx%d，%d 帧，线程 %s\n", args.width, args.height, args.frames,
                args.threads > 0 ? std::to_string(args.threads).c_str() : "自动");

    double totalMs = 0.0;
    for (int f = 0; f < args.frames; ++f) {
        const float t = float(f) / 60.0f;
        const auto t0 = std::chrono::steady_clock::now();
        rz.framebuffer().clear(Vec3{0.012f, 0.014f, 0.02f});
        renderFrame(rz, scene, t);
        const auto t1 = std::chrono::steady_clock::now();
        totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
    }

    const double avgMs = totalMs / double(args.frames);
    std::printf("[dreamlab-rt] 三角形 %lld 个（剔除 %lld），着色 %lld 像素\n", rz.drawnTriangles(),
                rz.culledTriangles(), rz.shadedPixels());
    std::printf("[dreamlab-rt] 平均每帧 %.2f ms（%.0f FPS），光栅化 %.2f ms\n", avgMs,
                avgMs > 0.0 ? 1000.0 / avgMs : 0.0, rz.lastRasterMs());
    std::printf("[dreamlab-rt] 画面平均亮度 %.4f\n", rz.framebuffer().meanLuminance());

    if (args.hud) {
        // 故意不检查返回值：字模加载失败时 Font 会画红块占位（绝不白屏），
        // 修复提示已经由 loadFromFile 打到 stderr 上了。
        Font font;
        font.loadFromFile("assets/font/pixel12.bin");
        drawHud(rz.framebuffer(), font, float(avgMs > 0.0 ? 1000.0 / avgMs : 0.0));
        std::printf("[dreamlab-rt] HUD 已叠加，缺字 %d 个\n", font.missingGlyphs());
    }

    const std::vector<uint8_t> rgb = rz.framebuffer().toRGB8(args.exposure, true);
    if (!writePNG(args.shot.c_str(), args.width, args.height, rgb.data())) {
        std::fprintf(stderr, "[错误] 写 PNG 失败: %s\n", args.shot.c_str());
        return 1;
    }
    std::printf("[dreamlab-rt] 已写出 %s\n", args.shot.c_str());
    return 0;
}
