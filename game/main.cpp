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
    Texture floorTex;
    std::vector<Material> materials;
    Light lights[2];
    int lightCount = 0;

    Mesh floorMesh;
    Mesh wallMesh;
    Mesh sphereMesh;
    Mesh crateMesh;
    Mesh columnMesh;
    Mesh lampMesh;

    Vec3 cameraPos{0.0f, 2.35f, 5.6f};
    Vec3 cameraTarget{0.0f, 1.05f, 0.0f};
    float fovY = radians(50.0f);

    // 环境光强度（0 = 全黑房间）。第 0 关就是把它从 0 一点点加回来。
    Vec3 ambient{0.42f, 0.44f, 0.50f};
    Vec3 skyColor{0.46f, 0.56f, 0.78f};
    Vec3 groundColor{0.24f, 0.20f, 0.17f};
    // 灯的位置（也是自发光灯罩的位置）
    Vec3 keyLightPos{3.1f, 4.1f, 2.3f};
};

// 材质槽位：用常量索引比数字好读
enum MaterialSlot {
    kFloor = 0,
    kChrome,
    kPlastic,
    kClay,
    kWall,
    kCrate,
    kColumn,
    kLamp,
    kMaterialCount
};

void buildScene(Scene& s) {
    // 棋盘格贴图 —— 最容易看出"透视对不对、滤波对不对"的图案
    s.floorTex = makeChecker(128, Vec3{0.055f, 0.062f, 0.075f}, Vec3{0.70f, 0.72f, 0.76f}, 4);

    s.materials.resize(kMaterialCount);

    s.materials[kFloor].albedo = Vec3{1.0f, 1.0f, 1.0f};
    s.materials[kFloor].roughness = 0.30f;
    s.materials[kFloor].albedoTexture = &s.floorTex;
    s.materials[kFloor].uvScale = Vec2{8.0f, 8.0f};

    s.materials[kChrome].albedo = Vec3{0.95f, 0.93f, 0.90f};
    s.materials[kChrome].roughness = 0.06f;
    s.materials[kChrome].metallic = 1.0f;

    s.materials[kPlastic].albedo = Vec3{0.82f, 0.13f, 0.11f};
    s.materials[kPlastic].roughness = 0.26f;

    s.materials[kClay].albedo = Vec3{0.74f, 0.53f, 0.32f};
    s.materials[kClay].roughness = 0.92f;

    s.materials[kWall].albedo = Vec3{0.28f, 0.30f, 0.34f};
    s.materials[kWall].roughness = 0.88f;

    s.materials[kCrate].albedo = Vec3{0.52f, 0.40f, 0.24f};
    s.materials[kCrate].roughness = 0.62f;

    s.materials[kColumn].albedo = Vec3{0.60f, 0.61f, 0.64f};
    s.materials[kColumn].roughness = 0.44f;

    // 灯罩：自发光。它同时是画面里的光源和一块"亮起来的东西"
    s.materials[kLamp].albedo = Vec3{0.0f, 0.0f, 0.0f};
    s.materials[kLamp].emissive = Vec3{4.2f, 3.8f, 3.0f};
    s.materials[kLamp].roughness = 0.5f;

    // 主光：暖白，从右上前方打过来
    s.lights[0].position = s.keyLightPos;
    s.lights[0].color = Vec3{1.0f, 0.93f, 0.82f};
    s.lights[0].intensity = 95.0f;
    s.lights[0].radius = 0.35f;

    // 补光：冷色，从左侧远处，用来把暗部从纯黑里拉回来一点
    s.lights[1].position = Vec3{-4.2f, 2.8f, 1.4f};
    s.lights[1].color = Vec3{0.45f, 0.62f, 1.0f};
    s.lights[1].intensity = 42.0f;
    s.lights[1].radius = 0.45f;
    s.lightCount = 2;

    s.floorMesh = makePlane(26.0f);
    s.wallMesh = makeBox(Vec3{9.0f, 3.4f, 0.25f});
    s.sphereMesh = makeSphere(0.75f, 40, 20);
    s.crateMesh = makeBox(Vec3{0.6f, 0.6f, 0.6f});
    s.columnMesh = makeCylinder(0.42f, 3.2f, 28);
    s.lampMesh = makeBox(Vec3{0.22f, 0.22f, 0.22f});
}

// 每帧把整场景重新提交一次（P0 不做场景图缓存，先把管线跑通）
void renderFrame(Rasterizer& rz, const Scene& s, float timeSeconds) {
    const int w = rz.framebuffer().width;
    const int h = rz.framebuffer().height;
    const Mat4 view = lookAt(s.cameraPos, s.cameraTarget, Vec3{0.0f, 1.0f, 0.0f});
    const Mat4 proj = perspective(s.fovY, float(w) / float(h), 0.08f, 120.0f);

    ShadeEnv env;
    env.ambient = s.ambient;
    env.skyColor = s.skyColor;
    env.groundColor = s.groundColor;
    env.lights = s.lights;
    env.lightCount = s.lightCount;

    rz.begin(view, proj, s.cameraPos, env);

    rz.draw(s.floorMesh, Mat4{}, s.materials[kFloor]);
    rz.draw(s.wallMesh, translation(Vec3{0.0f, 3.4f, -6.0f}), s.materials[kWall]);
    rz.draw(s.columnMesh, translation(Vec3{3.5f, 1.6f, -1.4f}), s.materials[kColumn]);

    // 三个球：只改 roughness / metallic，观感完全不同 —— 这就是材质参数的意义
    rz.draw(s.sphereMesh, translation(Vec3{-2.15f, 0.75f, 0.0f}), s.materials[kChrome]);
    rz.draw(s.sphereMesh, translation(Vec3{0.0f, 0.75f, 0.0f}), s.materials[kPlastic]);
    rz.draw(s.sphereMesh, translation(Vec3{2.15f, 0.75f, 0.0f}), s.materials[kClay]);

    // 木箱慢慢转 —— 用来确认动画与深度排序都正常
    const Mat4 crate = translation(Vec3{-3.7f, 0.6f, 1.5f}) * rotationY(timeSeconds * 0.6f);
    rz.draw(s.crateMesh, crate, s.materials[kCrate]);

    // 灯泡本体
    rz.draw(s.lampMesh, translation(s.keyLightPos), s.materials[kLamp]);

    rz.flush();
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

int runSelfTest() {
    testMath();
    testRasterizer();
    testFont();
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
    if (args.hasCam) scene.cameraPos = args.cam;
    if (args.hasLook) scene.cameraTarget = args.look;
    if (args.fovDeg > 0.0f) scene.fovY = radians(args.fovDeg);

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
