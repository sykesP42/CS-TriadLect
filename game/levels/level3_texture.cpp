// 第 3 关「纹理」—— 这一关教"一张贴图怎么贴到墙上地上"。
//
//  前两关把房间点亮了、把三个球改对了。这一关低头看地板：方格砖被拉成一块一块
//  1.5 米的大方块 —— 因为整块地板只铺了一张贴图。技术美术每天在引擎里打交道的
//  三个词，就是这一关要改的三行：
//    uvscale  一张贴图铺几次（平铺）
//    wrap     铺出去的部分怎么办：repeat 从头再来 / clamp 拉成边缘色
//    filter   一个像素问贴图要颜色，贴图怎么答：nearest 取最近一个 / bilinear 四个插值
//
//  为什么这一关只改数据、判定也只读数据？和材质那一关同一个道理：设置对不对是个
//  事实（"repeat" 就是 repeat），而"看起来对不对"是连续的 —— 0.75 米和 0.8 米的砖
//  在 96x54 的评委图里都是几十个像素，量不出个所以然。读数据还有一个好处：
//  房间黑着也能过关 —— 学生要是跳着关玩，这一关不会因为没点灯而卡住。
//
//  为什么容差不是 0：4.05 当然算 4（砖差一厘米，肉眼看不出来），但 3 和 5 不算 ——
//  平铺次数就是这一关的题面，题面上不能含糊。滤波和寻址没有中间值，是二选一。
#include "../level.h"

namespace dlab {

namespace {

// 这一关的目标：平铺 4x4、repeat、bilinear。三件事各占进度条的一部分。
constexpr float kTargetScale = 4.0f;
constexpr float kScaleTol = 0.05f;  // 4.05 ≈ 4；3 和 5 不行（题面就是"铺几次"）
constexpr float kWeightScale = 0.40f;
constexpr float kWeightWrap = 0.30f;
constexpr float kWeightFilter = 0.30f;

bool nearf(float a, float b, float tol) {
    const float d = a - b;
    return d <= tol && d >= -tol;
}

// 三件事全都做成 = 1.0，缺哪件少哪件的那一份。
//
// 为什么"平铺"单独占 0.40、比另外两件重：它是这一关一眼就能看见的东西
// （地板砖大得离谱），也是学生一进门会先去改的那一项。wrap / filter 各 0.30 ——
// 它们不改变"铺了几张"，但决定了铺出来的东西长什么样：
// wrap 不对，铺出来的是一整块纯色；filter 不对，铺出来的是硬邦邦的马赛克。
float progressTexture(const LevelView& view) {
    const World& w = view.world;
    const int idx = w.findMaterial("floor");
    // 世界里没有地板材质（文件被改得面目全非）→ 0 分。返回 0 而不是崩溃，
    // 更不能是"找不到就当对" —— 那会把改坏文件变成过关。
    if (idx < 0) return 0.0f;

    const Material& m = w.materials[size_t(idx)];
    // 两个方向都要对：只把横着铺成 4、竖着还是 1，画面上是 4 张长条，不是地砖。
    const float scale =
        (nearf(m.uvScale.x, kTargetScale, kScaleTol) && nearf(m.uvScale.y, kTargetScale, kScaleTol))
            ? 1.0f
            : 0.0f;
    const float wrap = m.wrapMode == kWrapRepeat ? 1.0f : 0.0f;
    const float filter = m.filterMode == kFilterBilinear ? 1.0f : 0.0f;

    const float p = scale * kWeightScale + wrap * kWeightWrap + filter * kWeightFilter;
    // 三件都做到 = 1.0，明明白白写出来，不靠 0.4+0.3+0.3 的浮点和正好凑到 1。
    if (scale > 0.0f && wrap > 0.0f && filter > 0.0f) return 1.0f;
    return p;
}

// 评委机位：站在房间这一头往下看地板 —— 这一关的主角就是地板，别的东西都只是参照。
//   · 高度 3.0（天花板下 0.4 米）：俯角够大，画面里八成是地板，而不是墙；
//   · 视场角 50 度、俯角 28 度：从脚前 2.3 米一直看到对面墙角 —— 8 米深的地板
//     全在画面里，平铺 4x4 时有十来排格子，够看清"平不平铺"；
//   · 三个展台在画面右侧边缘露出来，当尺子用：球高 0.45 米，格子多大一眼能比。
Camera judgeCamera() {
    Camera cam;
    cam.fovY = radians(50.0f);
    aimCamera(cam, Vec3{0.0f, 3.0f, 4.0f}, Vec3{0.0f, 0.0f, -1.6f});
    return cam;
}

const char* kHint =
    "低头看看地板：方格砖被拉成了 1.5 米一块的大方块 —— 因为整块地板只铺了一张贴图。\n"
    "content/textures.txt 里 floor 那三行被人调乱了，三个词各管一件事：\n"
    "  uvscale 一张贴图铺几次（现在 1 1，要 4 4）；\n"
    "  wrap repeat 还是 clamp —— 铺出去的部分从头再来，还是拉成边缘那一条颜色。\n"
    "       注意：铺超过一张时，clamp 会把地板变成一整块纯色，那不是坏了，是它在干活；\n"
    "  filter nearest 还是 bilinear —— 一个像素问贴图要颜色，是只取最近的一个（硬、会闪），\n"
    "       还是周围四个插值一下（平滑）。\n"
    "想现场核对：敲 inspect 地板，三个设置现在是什么态都写在上面。改完存盘、按 R，当场见效。\n"
    "小窍门：filter 在 nearest / bilinear 之间来回换，盯着远处的地板看 —— 一个会闪，一个不闪。";

const Level& make() {
    static const Level lv = [] {
        Level l;
        l.id = "texture";
        l.title = "第 3 关 · 纹理";
        l.goal = "把地板铺成方格砖：平铺 4×4、repeat、bilinear";
        l.hint = kHint;
        l.passNote =
            "四关走完了。光栅化、光照、材质、纹理 —— 这条渲染管线你已经亲手过了一遍，"
            "而且改的每一处都当场看见了结果。这就是数媒组每天在做的事。\n"
            "接下来想继续玩：改 content/*.txt 自己配色，或者去读 core/raster.cpp（那 300 行是引擎的心脏）。";
        l.judge = judgeCamera();
        l.progress = progressTexture;
        return l;
    }();
    return lv;
}

}  // namespace

const Level& levelTexture() { return make(); }

}  // namespace dlab
