// dreamlab-rt —— 逐梦创新实验室 · 数媒组
// 主程序：命令行解析 + 场景装配 + 渲染循环。
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "../core/font.h"
#include "../core/framebuffer.h"
#include "../core/material.h"
#include "../core/platform.h"
#include "../core/png_write.h"
#include "../core/raster.h"
#include "../core/texture.h"
#include "../engine/console.h"
#include "../engine/content.h"
#include "../engine/mesh.h"
#include "../engine/reload.h"
#include "../engine/save.h"
#include "../engine/world.h"
#include "player.h"
#include "workshop.h"

using namespace dlab;

namespace {

// 空场景的底色：不是纯黑，留一点冷色，让人一眼看出「这是没照到光的暗处」而不是「渲染坏了」
const Vec3 kClearColor{0.012f, 0.014f, 0.02f};

// 开窗模式的标题和常驻提示。提示直接写在画面里而不是只写在 README ——
// 第一次运行的人会先看画面，不会先看文档。
const char* kWindowTitle = "dreamlab-rt —— 数媒组工作室（WASD 走 · 鼠标看 · E 交互 · ~ 控制台 · F2 拍照）";
const char* kWindowHint = "WASD 走 · 鼠标看 · E 交互 · ~ 控制台 · F2 拍照 · Esc 退出";
const char* kAutopilotHint = "自动演示：--walk 正在接管输入（想自己走就别给 --walk）";

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
    int watchMs = 0;                     // >0 = 截图前先等 content 变化（离屏验证热重载）
    bool console = false;                // 显示控制台面板
    std::vector<std::string> cmds;       // --cmd：进游戏前注入的命令（可重复）
    bool hasShot = false;                // 给了 --shot 就是离屏模式；否则开窗打游戏
    std::string walk;                    // --walk "w:120,d:60,e"：脚本输入（自动演示/验收）
    bool trace = false;                  // 每帧打印玩家位置（离屏验收碰撞用）
    int scale = 1;                       // --scale N：按 1/N 分辨率渲染，窗口放大显示
    int fpsCap = 60;                     // --fpscap N：开窗锁多少帧；0 = 不锁（测性能用）
    bool noclip = false;
    bool reset = false;                  // --reset：删掉存档，从头开始
    bool help = false;
};

void printUsage() {
    std::printf(
        "dreamlab-rt —— 逐梦实验室数媒组示例项目\n"
        "\n"
        "不带参数直接运行 = 开窗打游戏：WASD 走，鼠标看，E 交互，~ 开控制台，F2 拍照，Esc 退出。\n"
        "给了 --shot 就是离屏模式：不开窗，渲完写一张 PNG（脚本、验收、无桌面环境都用这个）。\n"
        "\n"
        "用法: dreamlab [选项]\n"
        "  --shot <路径>      离屏截图输出路径（给了它就一定不开窗）\n"
        "  --width <像素>     渲染宽度 / 窗口客户区宽度（默认 480）\n"
        "  --height <像素>    渲染高度 / 窗口客户区高度（默认 270）\n"
        "  --scale <倍数>     按 1/N 分辨率渲染再放大铺满窗口（低配机器用 2 或 3）\n"
        "  --fpscap <帧率>    开窗锁帧（默认 60）；给 0 就不锁 —— 想看看自己机器能跑多快用它\n"
        "  --frames <帧数>    离屏：跑多少帧后出图；开窗：跑够多少帧自动退出（默认 1 / 一直跑）\n"
        "  --threads <数量>   渲染线程数（默认自动）\n"
        "  --exposure <倍数>  曝光（默认 1.0）\n"
        "  --fov <角度>       竖直视场角（默认用场景的 50°）\n"
        "  --cam x,y,z        相机（眼睛）位置\n"
        "  --look x,y,z       相机看向的点\n"
        "  --walk \"w:120,a:60\" 脚本输入：按 w 走 120 帧、再按 a 走 60 帧，e 是「按一下」\n"
        "                     键名 w a s d e，冒号后是帧数（60 帧 = 1 秒）。给了它就忽略键盘\n"
        "  --trace            每帧打印位置 / 朝向（开窗还带 work= 一帧干活的毫秒数、period= 帧间隔）\n"
        "  --noclip           穿墙（调试和拍图用）\n"
        "  --reset            删掉存档，从出生点从头开始（存档在 saved/save.bin）\n"
        "  --preview [文本]   把中文字模画成终端 ASCII 图（检查字模是否完好）\n"
        "  --hud              在画面上叠一层文字（验证中文渲染进 PNG）\n"
        "  --watch <毫秒>     先应用 content/，再等文件变化并重新应用，然后才截图\n"
        "                     （没有窗口也能验证「改数据 → 按 R → 世界变化」）\n"
        "  --console          显示游戏内控制台面板\n"
        "  --cmd \"命令\"       进游戏前注入一条控制台命令（可重复，隐含 --console）\n"
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
            a.hasShot = true;
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
        } else if (s == "--watch") {
            a.watchMs = std::atoi(takeValue(argc, argv, i, "--watch"));
        } else if (s == "--console") {
            a.console = true;
        } else if (s == "--cmd") {
            a.cmds.push_back(takeValue(argc, argv, i, "--cmd"));
            a.console = true;
        } else if (s == "--walk") {
            a.walk = takeValue(argc, argv, i, "--walk");
        } else if (s == "--trace") {
            a.trace = true;
        } else if (s == "--scale") {
            a.scale = std::atoi(takeValue(argc, argv, i, "--scale"));
        } else if (s == "--fpscap") {
            a.fpsCap = std::atoi(takeValue(argc, argv, i, "--fpscap"));
        } else if (s == "--noclip") {
            a.noclip = true;
        } else if (s == "--reset") {
            a.reset = true;
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
    if (a.scale < 1) a.scale = 1;
    if (a.scale > 8) a.scale = 8;
    if (a.fpsCap > 1000) a.fpsCap = 1000;
    if (a.fpsCap < 0) a.fpsCap = 0;
    return a;
}

// ---------------------------------------------------------------- 场景

struct Scene {
    World world;
    Workshop workshop;
    Camera camera;
    Player player;  // 玩家才是"人在哪"的真相，camera 每帧从它算出来
};

void buildScene(Scene& s) {
    s.workshop = buildWorkshop(s.world);
    // 出生点：站在屋子中间偏前，面朝桌子和终端（-z）。手感和"推门进来"一致。
    // 低头 0.06 弧度是 M3.1 就定下的取景：桌面、终端、地上的棋盘格都在画面里 ——
    // 视角换成第一人称之后这一点也不能漂，不然"和上一版的图逐像素对得上"就没了。
    s.player.feet = Vec3{0.0f, 0.0f, 4.5f};
    s.player.yaw = 0.0f;
    s.player.pitch = -0.06f;
}

// 眼睛在哪、往哪看 —— 渲染只认 camera，所以走路之后必须同步一次。
// 单独抽出来是为了让"忘了同步"这件事有个唯一的、容易被看见的地方。
void syncCamera(Scene& s) {
    s.camera.position = s.player.eye();
    s.camera.yaw = s.player.yaw;
    s.camera.pitch = s.player.pitch;
    // fovY 不动：它是镜头参数，不是人的姿态
}

// 每帧把整场景重新提交一次（P0 不做场景图缓存，先把管线跑通）
void renderFrame(Rasterizer& rz, const Scene& s, float timeSeconds) {
    (void)timeSeconds;
    s.world.render(rz, s.camera);
}

// ---------------------------------------------------------------- 屏幕上的小提示
// 「按一下 E 没反应」是最容易让人以为程序坏了的事，所以任何一次按键都要有回声。
struct Toast {
    std::string text;
    double until = -1.0;  // 到点自动消失（秒，懒得引第二个时钟，用窗口的）
    void show(const std::string& s, double now, double seconds = 1.8) {
        text = s;
        until = now + seconds;
    }
    bool alive(double now) const { return !text.empty() && now < until; }
};

// ---------------------------------------------------------------- HUD
// 文字直接叠进颜色缓冲，不参与深度测试。颜色是线性 HDR 且最后统一过 ACES，
// 所以"纯白文字"要给 2.2 左右 —— 给 1.0 出来是灰的（渲染顺序决定的，不是 bug）。
// bottomInset：控制台面板占掉的高度。贴底的状态栏得往上让开，不然会被半透明
// 面板盖成一层灰影 —— 两样东西叠在一起，比哪一样单独显示都难读。

// 半透明底板：白墙前面的白字根本读不了，垫一层深色是唯一办法
void blendRect(Framebuffer& fb, int x, int y, int w, int h, Vec3 color, float alpha) {
    for (int gy = 0; gy < h; ++gy)
        for (int gx = 0; gx < w; ++gx) fb.blendPixel(x + gx, y + gy, color, alpha);
}

void drawHud(Framebuffer& fb, const Font& font, float fps, int bottomInset = 0,
             const std::string& hint = "") {
    const Vec3 white{2.2f, 2.2f, 2.25f};
    const Vec3 dim{1.5f, 1.5f, 1.55f};
    const Vec3 panel{0.02f, 0.025f, 0.04f};
    const int pad = 6;

    // 标题：2 倍字号，验证中文放大后依然是干净的像素字
    const std::string title = "数媒组工作室";
    const int titleW = font.measureLine(title) * 2;
    blendRect(fb, 8, 8, titleW + pad * 2, font.glyphH() * 2 + pad * 2, panel, 0.68f);
    font.drawLine(fb, 8 + pad, 8 + pad, title, white, 1.0f, 2);

    // 状态行：1 倍字号，中文 + 拉丁 + 数字混排，顺便验证比例字距
    char stats[96];
    std::snprintf(stats, sizeof(stats), "DreamLab 2026 · %.0f FPS · %dx%d", double(fps), fb.width, fb.height);
    const int statsW = font.measureLine(stats);
    const int statsY = fb.height - bottomInset - font.glyphH() - pad - 8;
    blendRect(fb, 8, statsY - pad, statsW + pad * 2, font.glyphH() + pad * 2, panel, 0.68f);
    font.drawLine(fb, 8 + pad, statsY, stats, dim, 1.0f, 1);

    // 操作提示（只有开窗模式才给）：告诉人这台机器能按哪些键。
    // 不写这一行，第一次运行的人只会盯着画面发呆 —— 按键是唯一需要"教"的东西。
    if (!hint.empty()) {
        const int hintW = font.measureLine(hint);
        const int hintY = statsY - font.glyphH() - pad * 2 - 4;
        blendRect(fb, 8, hintY - pad, hintW + pad * 2, font.glyphH() + pad * 2, panel, 0.68f);
        font.drawLine(fb, 8 + pad, hintY, hint, white, 1.0f, 1);
    }
}

// 准星 + 「你正看着什么」+ 一闪而过的提示。只在开窗模式画：
// --shot 出的图是拿去当素材/验收的，不该在上面烧一个十字。
void drawCrosshair(Framebuffer& fb, const Font& font, const std::string& prompt,
                   const std::string& toast) {
    const Vec3 ink{2.10f, 2.12f, 2.16f};
    const int cx = fb.width / 2;
    const int cy = fb.height / 2;
    const int arm = 6, gap = 3;
    for (int i = gap; i <= gap + arm; ++i) {
        fb.blendPixel(cx - i, cy, ink, 0.70f);
        fb.blendPixel(cx + i, cy, ink, 0.70f);
        fb.blendPixel(cx, cy - i, ink, 0.70f);
        fb.blendPixel(cx, cy + i, ink, 0.70f);
    }
    fb.blendPixel(cx, cy, ink, 0.90f);

    // 有可交互的东西就显示它的 prompt（文字来自实体本身，不是这里写死的）；
    // 没有就显示临时消息（「按了没反应」的那些回声）。
    const std::string text = !prompt.empty() ? prompt : toast;
    if (text.empty()) return;
    const Vec3 color = !prompt.empty() ? Vec3{2.35f, 2.32f, 2.30f} : Vec3{2.60f, 2.00f, 0.75f};
    const int w = font.measureLine(text);
    const int x = cx - w / 2;
    const int y = cy + 24;
    blendRect(fb, x - 6, y - 4, w + 12, font.glyphH() + 8, Vec3{0.014f, 0.017f, 0.026f}, 0.72f);
    font.drawLine(fb, x, y, text, color, 1.0f, 1);
}

// ---------------------------------------------------------------- 控制台命令
// 命令放这里而不是 console.h：知道"世界"的东西不该塞进"终端"里。
// 每条命令只做一件小事、回一行中文 —— 命令和结果之间没有黑箱，这是给学生看的。
// 命令本身全是 ASCII，不用切输入法。

std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string joinFloats(const std::vector<float>& v) {
    char buf[64];
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        std::snprintf(buf, sizeof(buf), "%s%.4g", i ? " " : "", double(v[i]));
        out += buf;
    }
    return out;
}

std::string joinNames(const std::vector<std::string>& v) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) out += (i ? " " : "") + v[i];
    return out;
}

void runCommand(const std::string& line, Console& con, World& world, ContentWatcher& watcher,
                const std::function<void()>& takeShot) {
    const std::vector<std::string> t = splitTokens(line);
    if (t.empty()) return;
    const std::string& cmd = t[0];

    if (cmd == "help") {
        con.print("命令（都是 ASCII，不用切输入法）：");
        con.print("  help                 这份帮助");
        con.print("  cls                  清屏");
        con.print("  ambient <亮度|r g b> 环境光，例如 ambient 0.1 或 ambient 0.3 0.4 0.6");
        con.print("  light <强度>         主光，例如 light 60");
        con.print("  light <灯号> <强度>  指定某一盏，例如 light 0 60 / light 1 30");
        con.print("                       （灯号就是 content/lighting.txt 里的编号）");
        con.print("  inspect [材质|物体]  看材质参数，例如 inspect plastic / inspect 塑料球");
        con.print("                       （不给名字就列出全部材质）");
        con.print("  reload               重新读 content/ 的数据文件（= 按 R）");
        con.print("  shot                 现在存一张干净的 PNG（不含面板）到 shots/");
        con.print("小提示：改 content/*.txt 再敲 reload，比敲命令更接近「做美术」这件事。");
        return;
    }

    if (cmd == "cls") {
        con.clear();
        return;
    }

    if (cmd == "ambient") {
        std::vector<float> v;
        for (size_t i = 1; i < t.size(); ++i) {
            float f = 0.0f;
            if (detail::parseNumber(t[i], f)) v.push_back(f);
        }
        if (v.size() == 1) v = {v[0], v[0], v[0]};
        if (v.size() != 3) {
            con.printError("用法：ambient <亮度> 或 ambient <r> <g> <b>，例如 ambient 0.1");
            return;
        }
        world.ambient = Vec3{v[0], v[1], v[2]};
        con.printOk("环境光 = " + joinFloats(v) + "（按 R 或用文件里的值会把它盖回去）");
        return;
    }

    if (cmd == "light") {
        int idx = 0;
        float value = 0.0f;
        if (t.size() == 2 && detail::parseNumber(t[1], value)) {
            // light 60 —— 只有一盏灯的场景里不该逼学生先记住灯号
        } else if (t.size() == 3 && detail::parseInt(t[1], idx) && detail::parseNumber(t[2], value)) {
            // light 1 30
        } else {
            con.printError("用法：light <强度> 或 light <灯号> <强度>，例如 light 60 / light 1 30");
            return;
        }
        if (idx >= world.lightCount) {
            con.printError("场景里只有 " + std::to_string(world.lightCount) + " 盏灯，没有第 " +
                           std::to_string(idx) + " 盏");
            return;
        }
        world.lights[idx].intensity = value;
        con.printOk("第 " + std::to_string(idx) + " 盏灯强度 = " + joinFloats({value}));
        return;
    }

    if (cmd == "inspect") {
        if (t.size() == 1) {
            con.print("场景里的材质（" + std::to_string(world.materialNames.size()) + " 个）：" +
                      joinNames(world.materialNames));
            return;
        }
        int idx = world.findMaterial(t[1]);
        std::string title = t[1];
        if (idx < 0) {
            // 也认实体名 —— 学生面前只有一个「塑料球」，没有 "plastic"。
            // 这条别名让按 E 观察材质成为一条能走通的链，而不是一句报错。
            const int ent = world.findEntity(t[1]);
            if (ent >= 0) {
                const Entity& e = world.entities[size_t(ent)];
                if (e.material >= 0 && e.material < int(world.materials.size())) {
                    idx = e.material;
                    title = t[1] + " 用的是「" + world.materialNames[size_t(idx)] + "」";
                }
            }
        }
        if (idx < 0) {
            con.printError("没有叫 \"" + t[1] + "\" 的材质或物体。材质有：" + joinNames(world.materialNames));
            return;
        }
        const Material& m = world.materials[size_t(idx)];
        const std::string matName = world.materialNames[size_t(idx)];
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s：albedo %.3f %.3f %.3f · 粗糙度 %.2f · 金属度 %.2f", title.c_str(),
                      double(m.albedo.x), double(m.albedo.y), double(m.albedo.z), double(m.roughness),
                      double(m.metallic));
        con.print(buf);
        if (m.albedoTexture != nullptr) {
            std::snprintf(buf, sizeof(buf), "  带贴图，uv 平铺 %.2g x %.2g", double(m.uvScale.x),
                          double(m.uvScale.y));
            con.print(buf);
        }
        if (m.emissive.x > 0.0f || m.emissive.y > 0.0f || m.emissive.z > 0.0f) {
            std::snprintf(buf, sizeof(buf), "  自发光 %.2f %.2f %.2f", double(m.emissive.x),
                          double(m.emissive.y), double(m.emissive.z));
            con.print(buf);
        }
        con.print("  想看它变样：改 content/materials.txt 里 material " + matName + " 那一段，存盘后敲 reload");
        return;
    }

    if (cmd == "reload") {
        std::vector<std::string> log;
        const std::vector<std::string> changed = watcher.forceReload(world, &log);
        bool hadError = false;
        for (const std::string& l : log) {
            // 解析错误长这样：「content/materials.txt:70: 未知字段 ...」，
            // 汇总行以「  →」开头 —— 这两种要红着显示，否则学生划过去就当没看见
            const bool looksLikeError = l.rfind("content/", 0) == 0 || l.rfind("  →", 0) == 0;
            hadError = hadError || looksLikeError;
            if (looksLikeError) {
                con.printError(l);
            } else {
                con.print(l);
            }
        }
        // 「读了几个文件」和「有几个文件生效」不是一回事：写坏的那个整份不生效，
        // 所以有错的时候不能说「重新应用了 N 个文件」—— 学生会以为改动进去了。
        if (changed.empty()) {
            con.print("content/ 没有变化（和上次读进来的一模一样）");
        } else if (hadError) {
            con.print("重新读了 " + std::to_string(changed.size()) +
                      " 个文件；报错的那几个整份没生效，其它照常");
        } else {
            con.printOk("重新应用了 " + std::to_string(changed.size()) + " 个文件");
        }
        return;
    }

    if (cmd == "shot") {
        takeShot();
        return;
    }

    con.printError("没有这个命令：" + cmd + "（敲 help 看全部命令）");
}

// ---------------------------------------------------------------- 输入 → 意图
// 键盘、脚本、以后的手柄，最后都变成同一个 InputState 交给 updatePlayer。
// 走路的正确性（碰撞、滑墙）因此可以在离屏下自动化验证 —— 见 testPlayer()。

// 窗口这一帧的键鼠 → 意图
InputState fromWindowInput(const FrameInput& pi) {
    InputState in;
    if (pi.down[int(Key::W)]) in.moveForward += 1.0f;
    if (pi.down[int(Key::S)]) in.moveForward -= 1.0f;
    if (pi.down[int(Key::D)]) in.moveRight += 1.0f;
    if (pi.down[int(Key::A)]) in.moveRight -= 1.0f;
    in.lookDX = pi.mouseDX;
    in.lookDY = pi.mouseDY;
    in.interact = pi.pressed[int(Key::E)];
    return in;
}

// --walk "w:120,w+d:60,e" → 一串「按住哪些键、按多少帧」。
// 帧数是绝对的，所以同一串脚本在哪台机器上跑出来都一样 —— 验收要的就是这个。
struct WalkStep {
    std::vector<Key> keys;
    int frames = 1;
};

std::vector<WalkStep> parseWalk(const std::string& text) {
    std::vector<WalkStep> steps;
    std::string seg;
    auto flush = [&]() {
        if (seg.empty()) return;
        const size_t colon = seg.find(':');
        const std::string names = colon == std::string::npos ? seg : seg.substr(0, colon);
        int frames = colon == std::string::npos ? 1 : std::atoi(seg.c_str() + colon + 1);
        if (frames < 1) frames = 1;

        WalkStep step;
        step.frames = frames;
        std::string name;
        for (size_t i = 0; i <= names.size(); ++i) {
            if (i == names.size() || names[i] == '+') {
                if (name == "w") step.keys.push_back(Key::W);
                else if (name == "s") step.keys.push_back(Key::S);
                else if (name == "a") step.keys.push_back(Key::A);
                else if (name == "d") step.keys.push_back(Key::D);
                else if (name == "e") step.keys.push_back(Key::E);
                else if (name == "left") step.keys.push_back(Key::Left);
                else if (name == "right") step.keys.push_back(Key::Right);
                else if (name == "up") step.keys.push_back(Key::Up);
                else if (name == "down") step.keys.push_back(Key::Down);
                else {
                    std::fprintf(stderr, "[错误] --walk 不认识 \"%s\"（可用：w a s d e left right up down，"
                                         "多个键用 + 连，:后面是帧数，例如 w:120,w+d:60,e）\n",
                                 name.c_str());
                    std::exit(2);
                }
                name.clear();
            } else {
                name += names[i];
            }
        }
        steps.push_back(step);
        seg.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t') flush();
        else seg += c;
    }
    flush();
    return steps;
}

InputState walkInputAt(const std::vector<WalkStep>& steps, int frame) {
    InputState in;
    int t = 0;
    for (const WalkStep& s : steps) {
        if (frame < t + s.frames) {
            for (Key k : s.keys) {
                switch (k) {
                    case Key::W: in.moveForward += 1.0f; break;
                    case Key::S: in.moveForward -= 1.0f; break;
                    case Key::D: in.moveRight += 1.0f; break;
                    case Key::A: in.moveRight -= 1.0f; break;
                    case Key::Left: in.lookDX -= 8.0f; break;
                    case Key::Right: in.lookDX += 8.0f; break;
                    case Key::Up: in.lookDY -= 8.0f; break;
                    case Key::Down: in.lookDY += 8.0f; break;
                    case Key::E: in.interact = true; break;
                    default: break;
                }
            }
            return in;
        }
        t += s.frames;
    }
    return in;
}

// 看着某个东西按 E：执行它自己带的命令，并且走控制台那条路。
// 「实体 → 命令 → 控制台」这条链让交互、打字、--cmd 共用同一个解释器：
// 关卡作者只要给实体写一句 command，不用碰 C++。
void interact(Scene& s, Console& con, Toast& toast, double now) {
    const World::RayHit hit = s.world.castRay(s.player.eye(), s.player.forward(), Player::kReach);
    if (hit.entity < 0) {
        toast.show("准星前面没有能互动的东西（走近点，或者低头看看）", now);
        return;
    }
    const Entity& e = s.world.entities[size_t(hit.entity)];
    if (e.command.empty()) {
        toast.show("「" + e.name + "」现在还没接上动作", now);
        return;
    }
    // 把控制台翻开：命令的来龙去脉（执行了哪一句、结果是什么）就在眼前，
    // 而不是"按了 E 之后画面悄悄变了，不知道发生了什么"。
    con.setVisible(true);
    con.print("（看着「" + e.name + "」按下 E）");
    con.run(e.command);
}

// ---------------------------------------------------------------- 开窗模式
int runWindow(const Args& args, Window& win, Scene& scene, Rasterizer& rz, Console& con,
              const std::function<void()>& takeShot, Toast& toast) {
    Font font;
    font.loadFromFile("assets/font/pixel12.bin");  // 失败会画红块占位，绝不白屏

    const std::vector<WalkStep> walk = parseWalk(args.walk);
    const bool autopilot = !args.walk.empty();
    if (autopilot) std::printf("[dreamlab-rt] --walk 接管输入（%s），键盘这局不生效\n", args.walk.c_str());

    double last = win.time();
    float fps = 60.0f;
    int frame = 0;

    // 限帧：开窗是靠 present() 直接贴位图，没有垂直同步管着 —— 不锁的话这个循环
    // 会往上百帧跑，把每个核都吃满。宣讲一两个小时，风扇狂转、笔记本掉电都很难看，
    // 所以要锁。锁得住的前提是"睡得准"：系统定时器粒度默认 15.6ms 一档，睡 5ms
    // 会睡到 15.6ms、60 帧直接掉成 30 帧 —— platform_win32.cpp 开窗时把它调到 1ms 了。
    // 提前干完就睡到下一帧的点上；干不完就别睡，让帧率自己掉下去。
    // --fpscap 0 = 不锁（量性能用），--fpscap 30 = 低配机器省电用。
    const double kFrameBudget = args.fpsCap > 0 ? 1.0 / double(args.fpsCap) : 0.0;
    auto prevFrameStart = std::chrono::steady_clock::now();

    while (win.pump()) {
        const auto frameStart = std::chrono::steady_clock::now();
        // 上一帧到这一帧的间隔（帧率就是它的倒数）。和 work 一起看，
        // 就知道时间是花在渲染上还是花在等上。
        const double periodMs =
            std::chrono::duration<double, std::milli>(frameStart - prevFrameStart).count();
        prevFrameStart = frameStart;
        const double now = win.time();
        float dt = float(now - last);
        last = now;
        // 卡一下（拖动窗口、切出去回来）不要变成一大步 —— dt 大了会一步穿过墙
        dt = clampf(dt, 0.0005f, 0.05f);

        // HUD 上的 FPS 是「玩家眼里的一秒多少帧」（帧间隔的倒数，含限帧等待），
        // 不是「渲染一帧要多久」—— 限了帧以后这两个能差三倍，写哪个都得说清楚。
        // 渲染本身多快，看 --trace 里的 work=。离屏出图没有"帧率"可言，
        // 那边显示的仍是渲染耗时换算出来的吞吐，两个数各有各的用处。
        if (periodMs > 0.0) fps = fps * 0.9f + float(1000.0 / periodMs) * 0.1f;

        const FrameInput& pi = win.input();

        // ---- 全局按键
        if (pi.pressed[int(Key::Esc)]) {
            if (con.visible()) {
                con.setVisible(false);
                win.setMouseCaptured(true);  // 刚按 Esc 的人一定在窗口里，直接回到"鼠标看视角"
            } else {
                break;  // 控制台没开 → Esc 退出
            }
        }
        if (pi.pressed[int(Key::Tilde)]) {
            con.toggle();
            win.setMouseCaptured(!con.visible());
        }
        if (pi.pressed[int(Key::F2)]) takeShot();

        // ---- 控制台开着的时候，键盘全给它。
        // 不然敲 ambient 里的 a/w/d 会顺手把人挪走 —— 那是第一次用就会骂人的 bug。
        if (con.visible()) {
            for (char c : pi.typed) con.typeChar(c);
            if (pi.pressed[int(Key::Backspace)]) con.backspace();
            if (pi.pressed[int(Key::Enter)]) con.submit();
            win.setMouseCaptured(false);  // 打字要看得见鼠标，也不能让视角跟着甩
        } else if (!autopilot) {
            if (pi.pressed[int(Key::R)]) con.run("reload");  // R = 重读 content/（和 --cmd reload 同一条路）
            if (!pi.typed.empty()) {
                // 二话不说直接开打 = 想敲命令（不用先知道 ~ 在哪），第一个字符得留住
                con.setVisible(true);
                for (char c : pi.typed) con.typeChar(c);
                win.setMouseCaptured(false);
            }
        }

        // ---- 走一步
        const InputState in = autopilot ? walkInputAt(walk, frame)
                                        : (con.visible() ? InputState{} : fromWindowInput(pi));
        updatePlayer(scene.player, in, scene.world, dt);
        syncCamera(scene);
        if (in.interact) interact(scene, con, toast, now);

        // ---- 画一帧
        rz.framebuffer().clear(kClearColor);
        renderFrame(rz, scene, float(now));

        // 叠字层：HUD → 准星 → 控制台面板（后画的盖住先画的）
        const int inset = con.panelHeight(font, rz.framebuffer().width);
        drawHud(rz.framebuffer(), font, fps, inset, autopilot ? kAutopilotHint : kWindowHint);
        const World::RayHit look = scene.world.castRay(scene.player.eye(), scene.player.forward(), Player::kReach);
        drawCrosshair(rz.framebuffer(), font,
                      look.entity >= 0 ? scene.world.entities[size_t(look.entity)].prompt : std::string(),
                      toast.alive(now) ? toast.text : std::string());
        if (con.visible()) con.draw(rz.framebuffer(), font);

        win.present(rz.framebuffer().toRGB8(args.exposure, true), rz.framebuffer().width,
                    rz.framebuffer().height);
        win.endFrame();

        // 这一帧从起床到贴完屏幕一共花了多少毫秒。放在 --trace 里是因为
        // "卡在哪一段"这种事，没有数字就只能猜；真要调性能时也不用另加计时器。
        const double spent = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - frameStart)
                                 .count();

        if (args.trace) {
            std::printf("[trace] f=%d pos=(%.3f,%.3f,%.3f) yaw=%.3f pitch=%.3f work=%.2fms period=%.2fms\n",
                        frame, double(scene.player.feet.x), double(scene.player.feet.y),
                        double(scene.player.feet.z), double(scene.player.yaw),
                        double(scene.player.pitch), spent, periodMs);
        }
        ++frame;
        if (args.frames > 1 && frame >= args.frames) break;  // 冒烟测试：跑够帧数自己退

        // 提前干完就睡到下一帧的点上 —— 别把 CPU 空转掉。留 1ms 余量：
        // 睡过了头会白等一整格，宁可早醒一丝（63 帧和 60 帧肉眼没区别）
        const double rest = kFrameBudget * 1000.0 - spent - 1.0;
        if (rest > 0.0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(rest));
    }

    win.close();
    std::printf("[dreamlab-rt] 窗口关了（跑了 %d 帧）\n", frame);
    return 0;
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

bool sameVec3(Vec3 a, Vec3 b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

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

// 走路和碰撞。这一节的存在理由：碰撞错了（差一厘米就穿模）靠肉眼看窗口是看不出来的，
// 只有能离屏跑、能断言"停在哪一米"才敢说它是对的。所有期望值都从世界的包围盒算出来，
// 不写死数字 —— 关卡改尺寸，测试跟着走。
void testPlayer() {
    World w;
    buildWorkshop(w);
    const float dt = 1.0f / 60.0f;

    auto run = [&](Player& p, const InputState& in, int frames) {
        bool bumped = false;
        for (int i = 0; i < frames; ++i) bumped = updatePlayer(p, in, w, dt) || bumped;
        return bumped;
    };

    // ① 不动就不该挪
    {
        Player p;
        run(p, InputState{}, 120);
        checkClose(p.feet.x, 0.0f, 1e-5f, "玩家：不按键就不动（x）");
        checkClose(p.feet.z, 4.5f, 1e-5f, "玩家：不按键就不动（z）");
    }

    // ② 按 W 一秒的位移：起步有加速段，所以是 2.9~3.2 米之间，但绝不该超过满速。
    //    这条同时钉住两件事：人真的在走（不是没动），以及没有"加速反而更快"的鬼故事。
    {
        Player p;
        InputState in;
        in.moveForward = 1.0f;
        run(p, in, 60);
        const float travelled = 4.5f - p.feet.z;
        check(travelled > 2.9f && travelled < Player::kWalkSpeed,
              "玩家：按 W 一秒走 2.9~3.2 米（起步加速，不会超速）");
        checkClose(p.feet.x, 0.0f, 1e-4f, "玩家：直着走不会横飘");
    }

    // ③ 往桌子走会停在外沿（正是"没穿模"的定义），再顶两秒也不会往里挤
    const Entity* desk = w.entity("桌面");
    check(desk != nullptr, "玩家：找得到「桌面」");
    if (desk != nullptr) {
        Player p;
        InputState in;
        in.moveForward = 1.0f;
        const bool bumped = run(p, in, 300);
        check(bumped, "玩家：撞到桌子会报「撞了」");
        checkClose(p.feet.z, desk->aabbMax.z + Player::kRadius, 0.01f, "玩家：停在桌子外沿，没穿模");
        const float stopped = p.feet.z;
        run(p, in, 120);
        checkClose(p.feet.z, stopped, 1e-4f, "玩家：顶着桌子不会一点一点往里挤");
    }

    // ④ 往左走会停在左墙内表面
    const Entity* leftWall = w.entity("左墙");
    check(leftWall != nullptr, "玩家：找得到「左墙」");
    if (leftWall != nullptr) {
        Player p;
        InputState in;
        in.moveRight = -1.0f;
        run(p, in, 400);
        checkClose(p.feet.x, leftWall->aabbMax.x + Player::kRadius, 0.01f, "玩家：停在左墙内表面");

        // ⑤ 贴着墙斜着走要"滑过去"而不是"卡死"：x 被墙挡住，z 照走不误。
        //    第一人称最基本的一条手感，也是最容易写成"撞上就完全不动"的地方。
        Player q;
        InputState diag;
        diag.moveForward = 1.0f;
        diag.moveRight = -1.0f;
        run(q, diag, 400);
        checkClose(q.feet.x, leftWall->aabbMax.x + Player::kRadius, 0.02f, "玩家：斜着顶墙时 x 被挡住");
        check(q.feet.z < 0.0f, "玩家：斜着顶墙时 z 照样前进（贴墙滑行）");
    }

    // ⑥ 斜着按两个键不能更快 —— 少了归一化这一步，斜走会快 41%，而人在窗口里
    //    只会觉得"这游戏有点飘"，根本查不出来
    {
        Player a, b;
        InputState straight;
        straight.moveForward = 1.0f;
        InputState diagonal;
        diagonal.moveForward = 1.0f;
        diagonal.moveRight = 1.0f;
        run(a, straight, 60);
        run(b, diagonal, 60);
        // 出生点在 (0,0,4.5)：位移就是 (x, 0, z-4.5)
        const float dStraight = 4.5f - a.feet.z;
        const float dDiagonal = length(Vec3{b.feet.x, 0.0f, b.feet.z - 4.5f});
        checkClose(dDiagonal, dStraight, dStraight * 0.02f, "玩家：斜着走不快 1.41 倍（速度归一化）");
    }

    // ⑦ 鼠标视角：右移 = 右转、下移 = 低头，俯仰有上限，yaw 收在 (-pi, pi]
    {
        Player p;
        InputState in;
        in.lookDX = 100.0f;
        in.lookDY = 50.0f;
        updatePlayer(p, in, w, dt);
        checkClose(p.yaw, -100.0f * Player::kLookSpeed, 1e-6f, "玩家：鼠标右移 = 向右转");
        checkClose(p.pitch, -0.04f - 50.0f * Player::kLookSpeed, 1e-6f, "玩家：鼠标下移 = 低头");

        Player up;
        InputState ceiling;
        ceiling.lookDY = -1.0e6f;
        updatePlayer(up, ceiling, w, dt);
        checkClose(up.pitch, 1.5533f, 1e-3f, "玩家：抬头有上限（不会从头顶翻过去）");

        Player spin;
        InputState around;
        around.lookDX = 1.0e9f;
        updatePlayer(spin, around, w, dt);
        check(spin.yaw >= -kPi && spin.yaw <= kPi, "玩家：一直往一个方向转，yaw 也不会涨到溢出");
    }

    // ⑧ --noclip：调试和拍图要能穿墙
    {
        Player p;
        InputState in;
        in.moveForward = 1.0f;
        run(p, in, 300);
        const float stopped = p.feet.z;
        p.noclip = true;
        run(p, in, 60);
        check(p.feet.z < stopped - 0.5f, "玩家：--noclip 下能穿过桌子");
    }

    // ⑨ dt = 0（时钟没走）不该动，也不该除以零
    {
        Player p;
        InputState in;
        in.moveForward = 1.0f;
        updatePlayer(p, in, w, 0.0f);
        checkClose(p.feet.z, 4.5f, 1e-6f, "玩家：dt=0 时原地不动");
    }
}

// 视线射线：按 E 能不能选中东西，全看这里。写得宽松（kPad）是为了让 4cm 厚的
// 展板、8mm 厚的屏幕也能被选中；但"宽松"不能变成"隔墙也能拿东西"。
void testRaycast() {
    World w;
    buildWorkshop(w);

    // ① 真的"走"到桌前（不是瞬移），再看显示器 → 选中「终端」。
    //    先用物理走到够得着的地方，再验证视线 —— 于是"桌边停得住"和"看得到终端"
    //    变成一条链上的两件事，桌子往前挪 10cm 这条就会红。
    {
        Player p;
        InputState in;
        in.moveForward = 1.0f;
        for (int i = 0; i < 300; ++i) updatePlayer(p, in, w, 1.0f / 60.0f);
        p.setLookAt(Vec3{0.0f, 1.035f, -5.28f});
        const World::RayHit hit = w.castRay(p.eye(), p.forward(), Player::kReach);
        check(hit.entity == w.findEntity("终端"), "交互：走到桌前看显示器，选中「终端」");
        check(!hit.blocked, "交互：选中终端时不算被挡住");
    }

    // ② 出生点直视前方：3.2 米内什么都没有，就该什么都没有（不能乱报）
    {
        Player p;
        p.pitch = 0.0f;
        const World::RayHit hit = w.castRay(p.eye(), p.forward(), Player::kReach);
        check(hit.entity < 0 && !hit.blocked, "交互：够不到就返回「什么都没有」");
    }

    // ③ 隔着墙够不到展板：实心体先挡住射线
    {
        Player p;
        p.feet = Vec3{-8.0f, 0.0f, 1.2f};  // 屋子外面（左墙以西）
        p.setLookAt(Vec3{-5.94f, 1.9f, 1.2f});
        const World::RayHit hit = w.castRay(p.eye(), p.forward(), Player::kReach);
        check(hit.entity < 0 && hit.blocked, "交互：隔着墙够不到东西（先被实心体挡住）");
    }

    // ④ 走近了就能选中 4cm 厚的展板 —— 就是 kPad 存在的理由
    {
        Player p;
        p.feet = Vec3{-5.0f, 0.0f, 1.2f};
        p.setLookAt(Vec3{-5.94f, 1.9f, 1.2f});
        const World::RayHit hit = w.castRay(p.eye(), p.forward(), Player::kReach);
        check(hit.entity == w.findEntity("展板"), "交互：4cm 厚的展板也能被选中");
    }

    // ⑤ M3.4 的验收主线：走到展台前看着球按 E。球带的命令用的是 M3.3 就有的
    //    inspect，所以"交互 → 命令 → 控制台"这条链现在就能端到端验证。
    {
        Player p;
        p.feet = Vec3{2.4f, 0.0f, -1.2f};
        p.setLookAt(Vec3{3.6f, 1.4f, -1.2f});
        const World::RayHit hit = w.castRay(p.eye(), p.forward(), Player::kReach);
        check(hit.entity == w.findEntity("塑料球"), "交互：看着展台上的塑料球能选中它");
        if (hit.entity >= 0) {
            check(w.entities[size_t(hit.entity)].command == "inspect 塑料球",
                  "交互：塑料球带的命令是 inspect 塑料球");
        }

        // ⑥ 同一个位置，把手伸短到 0.2 米就够不着 —— 确认距离真的在起作用
        const World::RayHit tooFar = w.castRay(p.eye(), p.forward(), 0.2f);
        check(tooFar.entity < 0, "交互：超出 reach 就够不到（距离上限有效）");
    }

    // ⑦ 视线为零（不该发生，但不能崩、不能瞎报）
    {
        Player p;
        const World::RayHit hit = w.castRay(p.eye(), Vec3{0.0f, 0.0f, 0.0f}, Player::kReach);
        check(hit.entity < 0 && !hit.blocked, "交互：视线为零向量时安全返回");
    }
}

// content/*.txt 的自检。这里的重点是「报错质量」和「数据↔场景对得上号」——
// 改数据的同学没有调试器，他唯一的反馈就是这些报错文字。
void testContent() {
    // ① 正常解析：注释、逗号当空格、同一行写完一个块
    const ContentPatch good = parseContent(
        "# 注释\n"
        "material chrome { albedo 0.95, 0.93, 0.90  roughness 0.06  metallic 1.0 }\n"
        "ambient 0.4 0.4 0.5\n",
        "t.txt");
    check(good.ok(), "content：合法输入不报错");
    check(good.materials.size() == 1 && good.lights.empty(), "content：解析出一个材质块");
    check(good.hasAmbient, "content：解析出 ambient");
    if (good.materials.size() == 1) {
        const MaterialPatch& m = good.materials[0];
        check(m.name == "chrome", "content：材质名解析正确");
        check(m.hasAlbedo && m.hasRoughness && m.hasMetallic && !m.hasEmissive,
              "content：只记下文件里写了的那几项（没写的保持原值）");
        checkClose(m.roughness, 0.06f, 1e-6f, "content：roughness 数值正确");
        checkClose(m.albedo.z, 0.90f, 1e-6f, "content：逗号分隔的 albedo 也认");
    }

    // ② 报错必须带「文件名:行号」，且拼错的键名要给出建议
    const ContentPatch typoField = parseContent("material x {\n    roudhness 0.5\n}\n", "content/materials.txt");
    check(!typoField.ok(), "content：拼错字段名会报错");
    if (!typoField.ok()) {
        check(typoField.errors[0].find("content/materials.txt:2:") == 0, "content：报错带文件名和行号（第 2 行）");
        check(typoField.errors[0].find("roughness") != std::string::npos, "content：报错给出正确拼写建议");
    }

    // ③ 各种写坏的写法：都要报错，且不能崩
    check(!parseContent("material a { albedo 1 1 }\n", "t").ok(), "content：数字个数不对会报错");
    check(!parseContent("material a { metallic nan }\n", "t").ok(), "content：nan 被拒绝（否则渲染出满屏雪花）");
    check(!parseContent("material a {\n albedo 1 1 1\n", "t").ok(), "content：块没闭合会报错");
    check(!parseContent("}\n", "t").ok(), "content：多出来的 } 会报错");
    check(!parseContent("metarial a { }\n", "t").ok(), "content：顶层关键字拼错会报错");
    check(!parseContent("material {\n}\n", "t").ok(), "content：material 后面缺名字会报错");
    check(!parseContent("material a albedo 1 1 1\n", "t").ok(), "content：缺 { 会报错");

    // 少写一个数字时，报错要「少而准」——不能把下一行的 roughness 也当成 albedo 的数
    const ContentPatch shortNums = parseContent("material a {\n albedo 0.5 0.5\n roughness 0.3\n}\n", "t");
    check(shortNums.errors.size() == 1, "content：少写一个数只报一处错（没连累下一行）");
    if (!shortNums.ok()) check(shortNums.errors[0].find("albedo") != std::string::npos, "content：报错指到出问题的那个字段");

    // ④ 应用到世界：值真的改了
    World w;
    buildWorkshop(w);
    const int chrome = w.findMaterial("chrome");
    const ContentPatch edit = parseContent("material chrome { roughness 0.99 }\nlight 0 { intensity 7 }\n", "t");
    std::vector<std::string> log;
    ApplyStats st = applyContent(w, edit, &log);
    check(st.materials == 1 && st.lights == 1, "content：应用了一项材质和一项灯");
    check(log.empty(), "content：合法数据应用时没有任何警告");
    checkClose(w.materials[size_t(chrome)].roughness, 0.99f, 1e-6f, "content：改材质真的落到世界上");
    checkClose(w.lights[0].intensity, 7.0f, 1e-6f, "content：改灯真的落到世界上");

    // ⑤ 名字写错：不能崩、不能误改，还要把可用的名字列出来
    const ContentPatch typoName = parseContent("material 镜面球 { roughness 0.5 }\n", "t");
    std::vector<std::string> log2;
    st = applyContent(w, typoName, &log2);
    check(st.missing == 1 && st.materials == 0, "content：世界里没有的材质名记为「没对上号」");
    check(log2.size() == 1 && log2[0].find("chrome") != std::string::npos, "content：名字写错时列出可用的材质名");
    checkClose(w.materials[size_t(chrome)].roughness, 0.99f, 1e-6f, "content：名字写错不会误改到别的材质");

    // ⑥ 仓库里真正的那两个数据文件：语法必须干净，而且每一项都要在场景里对得上号。
    //    否则学生看到的就是「改了没反应」，而原因只是一条没人看的警告。
    World w2;
    buildWorkshop(w2);
    std::vector<std::string> log3;
    const Material before = w2.materials[size_t(chrome)];
    for (const std::string& file : contentFileList()) {
        const ContentPatch p = loadContentFile(file);
        check(p.ok(), (std::string("content：仓库里的 ") + file + " 没有语法错误").c_str());
        if (!p.ok()) {
            std::printf("[selftest]   %s\n", p.errors[0].c_str());
            continue;
        }
        applyContent(w2, p, &log3);
    }
    for (const std::string& line : log3) std::printf("[selftest]   %s\n", line.c_str());
    check(log3.empty(), "content：仓库里的数据和场景完全对得上（没有哪一项被跳过）");

    // ⑦ 数据文件必须和代码里的默认值一致：改代码忘了改数据（或反过来），
    //    症状是「按 R 前后画面突然跳一下」，极难排查，所以在自检里直接盯死。
    const Material after = w2.materials[size_t(w2.findMaterial("chrome"))];
    check(sameVec3(before.albedo, after.albedo) && sameVec3(before.emissive, after.emissive) &&
              before.roughness == after.roughness && before.metallic == after.metallic,
          "content：content/materials.txt 与代码默认值一致（chrome）");
    checkClose(w2.ambient.x, 0.42f, 1e-5f, "content：content/lighting.txt 与代码默认值一致（ambient）");
    checkClose(w2.lights[0].intensity, 48.0f, 1e-5f, "content：content/lighting.txt 与代码默认值一致（主光强度）");
}

// 控制台自检。控制台是「学生唯一能对着画面打字的地方」，它的每一条交互都是承诺：
// 字符进得去、退格删得掉、回车一定有回音、面板遮不住世界、缺字模也不白屏。
void testConsole() {
    // ① 输入编辑：只收 ASCII 可见字符（见 console.h 文件头 ①）
    Console con;
    con.typeChar('a');
    con.typeChar(' ');
    con.typeChar('1');
    check(con.input() == "a 1", "控制台：ASCII 字符能进输入行");
    con.typeChar('\n');
    con.typeChar('\t');
    con.typeChar(char(0x80));
    check(con.input() == "a 1", "控制台：换行/制表/高位字节被挡在键盘外（不碰输入法也打不乱）");
    con.backspace();
    check(con.input() == "a ", "控制台：退格删掉最后一个字符");
    con.backspace();
    con.backspace();
    con.backspace();
    check(con.input().empty(), "控制台：连按退格删空之后不崩");

    // ② 回车：先把「> 你敲的那行」写进历史，再交给命令处理器
    Console c2;
    std::string got;
    int calls = 0;
    c2.setHandler([&](const std::string& line) {
        got = line;
        ++calls;
    });
    for (char ch : std::string("ambient 0.5")) c2.typeChar(ch);
    c2.submit();
    check(calls == 1 && got == "ambient 0.5", "控制台：回车把整行交给命令处理器");
    check(c2.lineCount() == 1 && c2.lineAt(0) == "> ambient 0.5", "控制台：回车先把「> 你敲的那行」写进历史");
    check(c2.input().empty(), "控制台：回车后输入行清空");
    c2.submit();
    check(calls == 1 && c2.lineCount() == 1, "控制台：空行回车什么都不做（不报错、不回声）");

    // ③ 命令的输出排在回声下面 —— 顺序就是因果顺序，学生一眼看清「我做了什么 / 发生了什么」
    Console c3;
    c3.setHandler([&](const std::string&) { c3.printOk("环境光 = 0.5 0.5 0.5"); });
    c3.run("ambient 0.5");
    check(c3.lineCount() == 2 && c3.lineAt(1).rfind("环境光", 0) == 0, "控制台：命令的输出排在「> 命令」下面");

    // ④ run() == 敲完回车：--cmd 注入和真人打字必须走同一条路（截图里看得到的就是现场按得出来的）
    Console c4;
    int calls4 = 0;
    c4.setHandler([&](const std::string&) { ++calls4; });
    for (char ch : std::string("light 60")) c4.typeChar(ch);
    c4.submit();
    Console c5;
    c5.setHandler([&](const std::string&) { ++calls4; });
    c5.run("light 60");
    check(calls4 == 2 && c4.lineCount() == c5.lineCount() && c4.lineAt(0) == c5.lineAt(0),
          "控制台：--cmd 的 run() 和真人敲回车产生完全一样的历史");

    // ⑤ cls 清屏
    Console c6;
    c6.print("一");
    c6.print("二");
    check(c6.lineCount() == 2, "控制台：输出进历史");
    c6.clear();
    check(c6.lineCount() == 0, "控制台：cls 清空历史");

    // ⑥ 滚动缓冲封顶：玩一晚上也不能无限吃内存
    Console c7;
    for (int i = 0; i < Console::kMaxScrollback + 50; ++i) c7.print("第 " + std::to_string(i) + " 行");
    check(c7.lineCount() == Console::kMaxScrollback, "控制台：历史行数封顶");
    check(c7.lineAt(0).find("50 行") != std::string::npos, "控制台：封顶后丢的是最老的行");

    // ⑦ 折行：中文没有空格可断，只能按「字」断
    Font font;
    const bool fontOk = font.loadFromFile("assets/font/pixel12.bin");
    if (fontOk) {
        const std::string longZh = "环境光太暗的话整个房间都看不清";  // 纯汉字，没有空格可断
        const int maxW = 100;
        const std::vector<std::string> rows = font.wrap(longZh, maxW);
        check(rows.size() >= 2, "控制台：长中文行会折成多行");
        bool allFit = true;
        for (const std::string& r : rows)
            if (font.measureLine(r) > maxW) allFit = false;
        check(allFit, "控制台：折行后每一行都不超过面板宽度");
        std::string joined;
        for (const std::string& r : rows) joined += r;
        check(joined == longZh, "控制台：折行不丢字、不切坏 UTF-8（拼回去和原文一模一样）");

        // 面板窄到放不下一个字：也必须出得来（否则 draw 会原地打转）
        size_t cpCount = 0;
        for (size_t i = 0; i < longZh.size(); ++cpCount) utf8Next(longZh, i);
        check(font.wrap(longZh, 3).size() == cpCount, "控制台：面板窄到放不下一个字时，一行一个字（不死循环）");
        const std::vector<std::string> twoLn = font.wrap("第一行\n第二行", maxW);
        check(twoLn.size() == 2 && twoLn[0] == "第一行", "控制台：原文里的换行会被保留");
        check(font.wrap("", maxW).size() == 1, "控制台：空串折行后仍有一行（光标得有地方待）");
    }

    // ⑧ 面板：隐藏 = 一个像素都不碰；显示 = 只压暗底部，上半屏照常看得见世界
    Framebuffer fb;
    fb.resize(200, 200);
    fb.clear(Vec3{1.0f, 1.0f, 1.0f});
    const std::vector<Vec3> before = fb.color;
    Console hidden;
    hidden.print("测试");
    hidden.draw(fb, font);
    bool untouched = true;
    for (size_t i = 0; i < fb.color.size(); ++i)
        if (fb.color[i].x != before[i].x) untouched = false;
    check(untouched, "控制台：隐藏时一个像素都不碰（不影响离屏逐像素比对）");

    Console shown;
    shown.setVisible(true);
    shown.print("环境光 = 0.5 0.5 0.5 —— 面板只占下半屏，世界还在上面");
    shown.draw(fb, font);
    // 面板最高 = 7 行字 + 上下留白，所以这条线以上绝不该被动过
    const int worstPanelTop = fb.height - (font.lineHeight() * Console::kVisibleRows + Console::kPadY * 2);
    bool upperUntouched = true;
    for (int y = 0; y < worstPanelTop; ++y)
        for (int x = 0; x < fb.width; ++x)
            if (fb.color[size_t(y) * size_t(fb.width) + size_t(x)].x != 1.0f) upperUntouched = false;
    check(upperUntouched, "控制台：面板只压暗底部，上半屏的世界一点没动");
    bool bottomLit = false;
    bool bottomDarker = false;
    for (int y = fb.height - 20; y < fb.height; ++y)
        for (int x = 0; x < fb.width; ++x) {
            const float v = fb.color[size_t(y) * size_t(fb.width) + size_t(x)].x;
            if (v != 1.0f) bottomLit = true;
            if (v < 0.5f) bottomDarker = true;  // 背景板 alpha 0.72 压过白色 → 0.29 左右
        }
    check(bottomLit, "控制台：面板真的画在画面底部");
    check(bottomDarker, "控制台：面板底下是半透明的（世界被压暗，不是糊一块不透明色块）");

    // ⑨ panelHeight() 是给 HUD 让位用的，必须和 draw() 真画出来的高度一模一样 ——
    //    这里用「最高被改动的行」反推实际高度来对账（曾经真的叠在一起过）
    auto topChangedRow = [](const Framebuffer& f) {
        for (int y = 0; y < f.height; ++y)
            for (int x = 0; x < f.width; ++x)
                if (f.color[size_t(y) * size_t(f.width) + size_t(x)].x != 1.0f) return y;
        return -1;
    };
    Console shortCon;
    shortCon.setVisible(true);
    shortCon.print("一行");
    Framebuffer fb3;
    fb3.resize(240, 200);
    fb3.clear(Vec3{1.0f, 1.0f, 1.0f});
    shortCon.draw(fb3, font);
    check(topChangedRow(fb3) == fb3.height - shortCon.panelHeight(font, fb3.width),
          "控制台：panelHeight() 和实际画出来的高度一致（HUD 才能正确让位，短面板）");

    Console fullCon;
    fullCon.setVisible(true);
    for (int i = 0; i < 30; ++i) fullCon.print("第 " + std::to_string(i) + " 行");
    Framebuffer fb4;
    fb4.resize(240, 200);
    fb4.clear(Vec3{1.0f, 1.0f, 1.0f});
    fullCon.draw(fb4, font);
    check(topChangedRow(fb4) == fb4.height - fullCon.panelHeight(font, fb4.width),
          "控制台：panelHeight() 和实际画出来的高度一致（历史堆满、面板顶到上限）");
    check(fullCon.panelHeight(font, fb4.width) <= font.lineHeight() * Console::kVisibleRows + Console::kPadY * 2,
          "控制台：面板高度有上限（历史再多也最多占 kVisibleRows 行字）");

    // ⑩ 字模整个没加载成功时也要能画（红块占位），绝不崩 —— 招新现场少一个文件不能白屏
    Framebuffer fb5;
    fb5.resize(200, 200);
    fb5.clear(Vec3{1.0f, 1.0f, 1.0f});
    Font noFont;
    Console orphan;
    orphan.setVisible(true);
    orphan.print("字模没加载");
    orphan.draw(fb5, noFont);
    bool orphanPainted = false;
    for (const Vec3& c : fb5.color)
        if (c.x != 1.0f) orphanPainted = true;
    check(orphanPainted, "控制台：字模没加载时也画得出（红块占位），不是白屏也不是崩溃");
}

void testSave() {
    // 存到 build/ 里，绝不碰玩家真正的 saved/save.bin —— 跑一次自检把人家进度清了
    // 是最难被原谅的一种"测试"
    const std::string path = "build/selftest_save.bin";
    std::remove(path.c_str());

    // ① 全新的存档点：文件不存在 = 没有存档，不是错误
    check(!loadSave(path).loaded, "存档：文件不存在时安静地返回「没有存档」");

    // ② 存一轮读一轮：每个字段都得原样回来（定长二进制不走文本，浮点也不该掉精度）
    SaveData out;
    out.level = 3;
    out.feet = Vec3{1.5f, -0.25f, 7.25f};
    out.yaw = 0.75f;
    out.pitch = 1.0f;
    out.goals = 0b1011u;
    check(writeSave(out, path), "存档：写 build/selftest_save.bin 成功");
    const SaveData in = loadSave(path);
    check(in.loaded, "存档：刚写下的文件读得回来");
    check(in.level == 3, "存档：关卡号原样回来");
    check(sameVec3(in.feet, out.feet), "存档：脚底坐标原样回来");
    check(in.yaw == out.yaw && in.pitch == out.pitch, "存档：朝向原样回来（浮点按位存，没掉精度）");
    check(in.goals == out.goals, "存档：目标位掩码原样回来");

    // ③ 各种坏档：全都当"没有存档"，一个都不许崩、不许把 NaN 放进世界
    auto broken = [&](const char* what, const std::string& bytes) {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (f != nullptr) {
            std::fwrite(bytes.data(), 1, bytes.size(), f);
            std::fclose(f);
        }
        check(!loadSave(path).loaded, what);
    };

    std::string good;
    good.append(kSaveMagic, 4);
    detail::pushU32(good, kSaveVersion);
    detail::pushU32(good, 0);
    detail::pushF32(good, 0.0f);
    detail::pushF32(good, 0.0f);
    detail::pushF32(good, 4.5f);
    detail::pushF32(good, 0.0f);
    detail::pushF32(good, -0.06f);
    detail::pushU32(good, 0);

    broken("存档：magic 不对 = 没有存档", "XXXX" + good.substr(4));
    broken("存档：版本号不认识 = 没有存档", good.substr(0, 4) + std::string("\x09\x00\x00\x00", 4) + good.substr(8));
    broken("存档：少一个字节 = 没有存档", good.substr(0, good.size() - 1));

    // 坐标是 NaN 的档：不拦住的话人会瞬移到 NaN，然后整个世界从屏幕上消失
    std::string nanSave = good;
    nanSave.replace(12, 4, [] {
        std::string t;
        detail::pushF32(t, std::nanf(""));
        return t;
    }());
    broken("存档：脚底坐标是 NaN = 没有存档", nanSave);

    // 关卡号超范围：将来关卡数据换了，旧档不该把人塞进一个不存在的关
    std::string badLevel = good;
    badLevel.replace(8, 4, [] {
        std::string t;
        detail::pushU32(t, 999u);
        return t;
    }());
    broken("存档：关卡号超范围 = 没有存档", badLevel);

    // ④ 只有俯仰角越界这一种"半坏"要救得回来：夹到 ±89°，而不是整档作废
    std::string wildPitch = good;
    wildPitch.replace(28, 4, [] {
        std::string t;
        detail::pushF32(t, 3.0f);
        return t;
    }());
    std::FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(wildPitch.data(), 1, wildPitch.size(), f);
    std::fclose(f);
    const SaveData fixed = loadSave(path);
    check(fixed.loaded, "存档：俯仰角越界仍然读得出来（只夹角度，不作废整档）");
    checkClose(fixed.pitch, 1.5533f, 1e-4f, "存档：越界的俯仰角被夹到 ±89°");

    // ⑤ --reset 的底座：删掉之后必须真的当没存档
    check(clearSave(path), "存档：--reset 删档成功");
    check(!loadSave(path).loaded, "存档：删档之后读出来是「没有存档」");
}

int runSelfTest() {
    testMath();
    testRasterizer();
    testFont();
    testWorld();
    testPlayer();
    testRaycast();
    testContent();
    testConsole();
    testSave();
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
    // --cam 给的是"眼睛"的位置（人一米六二高），玩家存的是脚底 —— 差一个身高
    if (args.hasCam) scene.player.feet = args.cam - Vec3{0.0f, Player::kEyeHeight, 0.0f};
    if (args.hasLook) scene.player.setLookAt(args.look);
    if (args.fovDeg > 0.0f) scene.camera.fovY = radians(args.fovDeg);
    scene.player.noclip = args.noclip;
    syncCamera(scene);

    // content/ 是世界的「最后一句话」：先搭场景，再让数据覆盖上去。
    // 这样不管谁（关卡代码、玩家、上一局留下的状态）把值改成了什么，只要文件里写着，
    // 画面出来就一定是文件说的样子 —— 「改数据一定生效」是数据热重载的全部承诺。
    ContentWatcher watcher(contentFileList());
    watcher.prime(scene.world);

    // 控制台：面板本身完全不知道世界是什么，命令通过 handler 走出去（见 console.h 文件头的边界说明）。
    // --cmd 注入走 con.run()，和真人敲回车是同一条路径 —— 离屏截图里能看到的，宣讲现场一定按得出来。
    Console con;
    con.setVisible(args.console);
    con.print("数媒组工作室 · 控制台。敲 help 看全部命令，R 键重新读 content/。");
    con.print("这个世界由 content/*.txt 决定：改文件 → 敲 reload → 画面当场变。");

    // 存档：只有开窗模式（= 真的在玩）才读。离屏出图必须每次从同一个出生点开始，
    // 不然"昨天的图和今天逐像素对不上"，而逐像素比对正是本项目的验证主手段。
    const bool windowed = !args.hasShot;
    if (args.reset) {
        clearSave();
        std::printf("[存档] 已清空 %s\n", kSavePath);
    }
    if (windowed && !args.reset) {
        const SaveData sv = loadSave();
        if (sv.loaded) {
            // 出生点先留个底：存档里的位置要是站不住，得能退回这里
            const Player spawn = scene.player;
            scene.player.feet = sv.feet;
            scene.player.yaw = sv.yaw;
            scene.player.pitch = sv.pitch;
            // 读到的是"上次离开的地方"，但世界可能已经变了（改了 content/ 的家具位置，
            // 存档点就可能在墙里）。站不住就老老实实回出生点 —— 卡在实体里出不来
            // 是最让人以为"游戏坏了"的一种坏法。
            if (!playerFits(scene.world, scene.player)) {
                scene.player = spawn;
                std::printf("[存档] 存档里的位置已经被东西占了 → 回出生点（想彻底重来用 --reset）\n");
                con.print("上次离开的位置现在被东西占了，先回出生点。想彻底重来：加 --reset 启动。");
            } else {
                std::printf("[存档] 继续上次：站在 (%.2f, %.2f, %.2f)（想从头开始用 --reset）\n",
                            double(sv.feet.x), double(sv.feet.y), double(sv.feet.z));
                con.print("继续上次的位置。想从出生点重来：加 --reset 启动。");
            }
        }
    }

    // --watch：没有窗口也能验证热重载。先应用一次 content，然后最多等 N 毫秒，
    // 等到文件内容变化就重新应用，再走正常渲染出图 —— 和游戏里按 R 是同一条代码路径。
    if (args.watchMs > 0) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(args.watchMs);
        bool got = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!watcher.poll(scene.world).empty()) {
                got = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::printf("[reload] %s（轮询 %d 次）\n",
                    got ? "content 有变化，已重新应用" : "等待超时，content 没有变化", watcher.polls());
    }

    // --scale 只在开窗时有意义：离屏出图的像素数必须严格等于 --width/--height
    // （截图比对脚本都指着这个），所以渲染分辨率按模式定下来，不再变。
    const int renderW = windowed ? args.width / args.scale : args.width;
    const int renderH = windowed ? args.height / args.scale : args.height;
    Rasterizer rz(args.threads);
    rz.resize(renderW, renderH);

    Toast toast;
    int shotIndex = 0;
    auto takeShot = [&]() {
        // 先按「此刻的世界」重绘一帧再存。这样「刚改完数据就拍」拍到的一定是新样子，
        // 而不是上一帧的旧画面 —— 离屏模式（--cmd）里更必须：那时一帧都还没渲染过。
        rz.framebuffer().clear(kClearColor);
        renderFrame(rz, scene, 0.0f);
        // 故意画在面板/准星之前：存的是「干净的世界」，UI 挡住的底部也拍得全。
        char path[64];
        std::snprintf(path, sizeof(path), "shots/console_%02d.png", ++shotIndex);
        const std::vector<uint8_t> rgb = rz.framebuffer().toRGB8(args.exposure, true);
        if (writePNG(path, rz.framebuffer().width, rz.framebuffer().height, rgb.data())) {
            con.printOk(std::string("已存 ") + path);
            toast.show(std::string("已存 ") + path, 0.0, 1.6);
        } else {
            con.printError(std::string("写 PNG 失败：") + path + "（shots/ 目录在不在？）");
            toast.show("写 PNG 失败，看看 shots/ 目录在不在", 0.0, 2.4);
        }
    };
    con.setHandler([&](const std::string& line) { runCommand(line, con, scene.world, watcher, takeShot); });

    // 窗口模式：没有 --shot 就开窗。打不开（没桌面、远程会话…）就老老实实退回离屏，
    // 而不是报个错什么都看不着 —— 这个项目的第一课是"几条命令就能跑起来"。
    if (windowed) {
        Window win;
        if (win.open(args.width, args.height, kWindowTitle)) {
            const int rc = runWindow(args, win, scene, rz, con, takeShot, toast);
            // 人一按 ESC / 点叉就存一次档：不搞"找到存档点才能存"，那套仪式感是给
            // 长流程 RPG 的。这里存档的意义只有一条 —— 下次打开还站在昨天那个位置、
            // 昨天改过的 content/ 也还在。存的是「离开时那一刻」的玩家位姿。
            SaveData sv;
            sv.level = 0;  // 关卡系统 M4 才落地，现在记 0 = 自由参观
            sv.goals = 0;
            sv.feet = scene.player.feet;
            sv.yaw = scene.player.yaw;
            sv.pitch = scene.player.pitch;
            if (writeSave(sv)) {
                std::printf("[存档] 已存 %s：站在 (%.2f, %.2f, %.2f)\n", kSavePath,
                            double(sv.feet.x), double(sv.feet.y), double(sv.feet.z));
            } else {
                std::fprintf(stderr, "[存档] 写 %s 失败 —— 这次的位置没能记下（下次还是从老地方开始）\n",
                             kSavePath);
            }
            return rc;
        }
        std::fprintf(stderr, "[窗口] 开不了窗 —— 这次改用离屏出图（--shot %s）。\n",
                     args.shot.c_str());
    }

    for (const std::string& c : args.cmds) con.run(c);
    if (!args.cmds.empty()) {
        for (int i = 0; i < con.lineCount(); ++i) std::printf("[console] %s\n", con.lineAt(i).c_str());
    }

    std::printf("[dreamlab-rt] 渲染 %dx%d，%d 帧，线程 %s\n", renderW, renderH, args.frames,
                args.threads > 0 ? std::to_string(args.threads).c_str() : "自动");

    // 离屏的走动：固定 1/60 秒一步。验收要的是"每次都一样"，不是"这台机器多快"。
    const std::vector<WalkStep> walk = parseWalk(args.walk);
    const float fixedDt = 1.0f / 60.0f;

    double totalMs = 0.0;
    for (int f = 0; f < args.frames; ++f) {
        const float t = float(f) / 60.0f;
        const InputState in = walkInputAt(walk, f);
        updatePlayer(scene.player, in, scene.world, fixedDt);
        syncCamera(scene);
        if (in.interact) interact(scene, con, toast, 0.0);

        const auto t0 = std::chrono::steady_clock::now();
        rz.framebuffer().clear(kClearColor);
        renderFrame(rz, scene, t);
        const auto t1 = std::chrono::steady_clock::now();
        totalMs += std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (args.trace) {
            std::printf("[trace] f=%d pos=(%.3f,%.3f,%.3f) yaw=%.3f pitch=%.3f\n", f,
                        double(scene.player.feet.x), double(scene.player.feet.y),
                        double(scene.player.feet.z), double(scene.player.yaw),
                        double(scene.player.pitch));
        }
    }

    // 走动会往控制台里写东西（按 E 的交互），所以帧跑完再打一遍，离屏也能"看见"交互结果
    if (!walk.empty()) {
        for (int i = 0; i < con.lineCount(); ++i) std::printf("[console] %s\n", con.lineAt(i).c_str());
    }

    const double avgMs = totalMs / double(args.frames);
    std::printf("[dreamlab-rt] 三角形 %lld 个（剔除 %lld），着色 %lld 像素\n", rz.drawnTriangles(),
                rz.culledTriangles(), rz.shadedPixels());
    std::printf("[dreamlab-rt] 平均每帧 %.2f ms（%.0f FPS），光栅化 %.2f ms\n", avgMs,
                avgMs > 0.0 ? 1000.0 / avgMs : 0.0, rz.lastRasterMs());
    std::printf("[dreamlab-rt] 画面平均亮度 %.4f\n", rz.framebuffer().meanLuminance());

    // 文字层（HUD / 控制台）是最后一步叠上去的：不进深度测试、不参与光照，
    // 但和 3D 走同一条 ACES → sRGB 出口 —— 所以 UI 的颜色也得给线性 HDR 值。
    if (args.hud || con.visible()) {
        // 故意不检查返回值：字模加载失败时 Font 会画红块占位（绝不白屏），
        // 修复提示已经由 loadFromFile 打到 stderr 上了。
        Font font;
        font.loadFromFile("assets/font/pixel12.bin");
        // 先问面板要占多高，状态栏好让开；画面板本身放在最后（后画的盖住先画的）
        const int inset = con.panelHeight(font, renderW);
        if (args.hud) drawHud(rz.framebuffer(), font, float(avgMs > 0.0 ? 1000.0 / avgMs : 0.0), inset);
        if (con.visible()) con.draw(rz.framebuffer(), font);
        std::printf("[dreamlab-rt] 文字层已叠加（控制台 %d 行，面板 %d px），缺字 %d 个\n", con.lineCount(),
                    inset, font.missingGlyphs());
    }

    const std::vector<uint8_t> rgb = rz.framebuffer().toRGB8(args.exposure, true);
    if (!writePNG(args.shot.c_str(), renderW, renderH, rgb.data())) {
        std::fprintf(stderr, "[错误] 写 PNG 失败: %s\n", args.shot.c_str());
        return 1;
    }
    std::printf("[dreamlab-rt] 已写出 %s\n", args.shot.c_str());
    return 0;
}
