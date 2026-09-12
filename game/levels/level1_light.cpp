// 第 1 关「第一束光」—— 这一关教"灯光师"这份活儿的第一课。
//
//  第 0 关改的是 C++（着色器里的系数），要重编译才生效。
//  第 1 关改的全是 content/ 下的数据：存盘 → 按 R → 当场就变。同一件事的两种做法，
//  先后顺序是设计过的 —— 先体会到"改代码有多重"，才知道数据层为什么值得存在。
//
//  为什么过关要同时满足三件事？因为它们对应三个不同的知识点，少一个都学不到：
//    ① 灯罩自己得亮（emissive）—— 自发光只照亮自己，照不亮别人；
//    ② 灯得装回吊灯里（pos）    —— 位置比强度重要，光斑跟着位置走；
//    ③ 强度得拧上去（intensity）—— 平方反比：远一倍的暗四倍。
//  只做 ① 学生会以为"灯罩亮了房间就该亮"；只做 ③ 会看到一团光打在地上，
//  于是自己发现"位置不对"—— 这就是这一关最想让人撞见的那一下。
#include "../level.h"

namespace dlab {

namespace {

// 主光 = content/lights.txt 里的 light 0。这个编号是给学生的路标（文件里、
// 提示里、终端里说的都是"light 0"），所以关卡也认它，不做"哪盏灯都算"的宽容 ——
// 宽容会让"提示说的和我做的不一样"这种事变得可能。
constexpr int kMainLight = 0;

// 灯罩的 emissive 大于这个数才算"点亮了"（0.5 是个很暗的值：
// 只要学生把 emissive 从 0.00 改成一个真的会亮的数，就一定超过它）
constexpr float kShadeOn = 0.5f;

// 光离吊灯挂点多远算"装回灯里了"（米）。0.35 = 差不多一个灯罩的尺寸：
// 学生照着数字填就一定在范围里，但也不会"随手放个大概"就蒙混过去。
constexpr float kHangTolerance = 0.35f;

// 判定线。第 0 关同款做法：先量出厂状态、再量改对之后的状态，达标线落在中间。
//
// 这里量过两组数，因为玩家可能在两种亮度底子上做这一关：
//   ① 第 0 关做完了（kAmbientStrength = 0.6，正常路线）
//        房间出厂态（灯全关）      0.0484 → 0%
//        光在门口、intensity 48    0.1009
//        装回吊灯、intensity 48    0.2415
//   ② 直接跳进第 1 关（`level 1` / --level 1，环境光还是出厂的 0）
//        房间出厂态                0.0000 → 0%
//        装回吊灯、intensity 48    0.1931        ← 两组里较小的那个
//   实测亮度就是 intensity 的线性函数（这条灯路没有人眼适应、没有自动曝光）：
//   ②里 48 → 0.1931，也就是每 1 点强度值 0.0040。
//
// kBright 取 0.1650：比 ① 的 0.2415 和 ② 的 0.1931 都低一截，所以**不管环境光
// 是 0 还是 0.6，照提示把灯装好、强度拧到 48 都稳过**。这一条是被实测逼出来的：
// 一开始按 ① 的 85% 取 0.2100，结果直接跳进第 1 关的玩家（环境光 0）把一切做对
// 也只读 96% —— 屏幕上写着没做完，可他明明做完了，比不过关还难受。
// 关卡不该押在上一关的答案上：判定线要挪到"两种底子都够得着"的地方。
// kDim 取 0.0500（比 ① 的出厂态略高）：出厂状态干净地读 0%。
constexpr float kDimLuminance = 0.0500f;
constexpr float kBrightLuminance = 0.1650f;

// 吊灯挂在哪儿：问场景要，不写死坐标 —— 改了 workshop.cpp 里吊灯的位置，
// 这一关的"正确位置"和灯罩的铭牌会跟着一起动，不会悄悄对不上。
Vec3 lampMount(const World& w) {
    const Entity* lamp = w.entity("吊灯");
    return lamp != nullptr ? lamp->position : Vec3{2.2f, 2.9f, 1.2f};
}

// 灯罩亮不亮：读材质，不读画面。
// 为什么不量图？灯罩是块 0.84m 的板，挂在 96x54 的小图边缘时只占几个像素 ——
// 而"emissive 是不是 0"在数据里是个非黑即白的事实，量它零风险。
// （对比第 0 关：那一关量的是"整间屋子的亮度"，没有对应的数据可读，只能看图。）
float shadeGlow(const World& w) {
    const int idx = w.findMaterial("lamp");
    if (idx < 0) return 0.0f;
    const Vec3 e = w.materials[size_t(idx)].emissive;
    return maxf(maxf(e.x, e.y), e.z);
}

float hangDistance(const World& w) {
    if (w.lightCount <= kMainLight) return 1.0e9f;
    return length(w.lights[kMainLight].position - lampMount(w));
}

float progressFirstLight(const LevelView& view) {
    const World& w = view.world;

    const float shade = shadeGlow(w) >= kShadeOn ? 1.0f : 0.0f;            // ① 灯罩亮了
    const float hang = hangDistance(w) <= kHangTolerance ? 1.0f : 0.0f;    // ② 光装回灯里了
    // ③ 展台真的被照亮了（连续量：光挪近一点、拧亮一点，进度条就往前走一点）
    const float bright = clampf((view.luminance - kDimLuminance) / (kBrightLuminance - kDimLuminance),
                                0.0f, 1.0f);

    const float p = shade * 0.25f + hang * 0.35f + bright * 0.40f;
    // 三件事都做到 = 1.0，明明白白地写出来。不靠 0.25+0.35+0.40 的浮点和正好凑到 1
    // —— 那种"差最后一位浮点就永远过不了关"的坑，第 0 关已经踩过一次了。
    if (shade > 0.0f && hang > 0.0f && bright >= 1.0f) return 1.0f;
    return p;
}

// 评委机位：站在展厅这一头，正对三个展台。
//   · 只有 +x 一个方向，三个展台沿 z 排开 —— 画面里是"三个球并排"，不是叠在一起；
//   · 视野收到 42 度（比玩家的 62 度窄），把墙和天花板的占比压下去，
//     画面上"亮"的部分尽可能都是展台附近的，量出来的数才敏感；
//   · 吊灯不在画面里：灯罩的状态是读材质数据的，不靠这张图（见 shadeGlow）。
Camera judgeCamera() {
    Camera cam;
    cam.fovY = radians(42.0f);
    aimCamera(cam, Vec3{-0.6f, 1.60f, -1.20f}, Vec3{3.60f, 1.20f, -1.20f});
    return cam;
}

const char* kHint =
    "这间屋子还差一盏灯。吊灯挂在天花板上（桌子右上方），但它是暗的：\n"
    "content/lights.txt 里的 light 0 —— intensity 是 0，pos 也被搬器材的人改乱了（光现在留在门口）。\n"
    "把 pos 改回 2.20 2.90 1.20、intensity 拧到 48；再去 content/materials.txt 把 "
    "material lamp 的 emissive 点亮（比如 4.2 3.8 3.0）—— 灯罩自己得发光，"
    "不然房间亮了它还是个黑盒子。\n"
    "改完存盘、切回游戏按 R：这次不用重编译，改的全是数据。"
    "拿不准就走到吊灯底下抬头按 E，它会告诉你灯现在挂在哪。";

const Level& make() {
    static const Level lv = [] {
        Level l;
        l.id = "first-light";
        l.title = "第 1 关 · 第一束光";
        l.goal = "把主光装回吊灯里并点亮，让展台亮起来";
        l.hint = kHint;
        l.judge = judgeCamera();
        l.progress = progressFirstLight;
        return l;
    }();
    return lv;
}

}  // namespace

const Level& levelFirstLight() { return make(); }

}  // namespace dlab
