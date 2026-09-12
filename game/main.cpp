// dreamlab-rt —— 逐梦创新实验室 · 数媒组
// 主程序：命令行解析 + 场景装配 + 渲染循环。
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

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

int runSelfTest() {
    testMath();
    testRasterizer();
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

    const std::vector<uint8_t> rgb = rz.framebuffer().toRGB8(args.exposure, true);
    if (!writePNG(args.shot.c_str(), args.width, args.height, rgb.data())) {
        std::fprintf(stderr, "[错误] 写 PNG 失败: %s\n", args.shot.c_str());
        return 1;
    }
    std::printf("[dreamlab-rt] 已写出 %s\n", args.shot.c_str());
    return 0;
}
