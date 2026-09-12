#include "workshop.h"

#include "../core/texture.h"

namespace dlab {

namespace {

Entity makeEntity(const char* name, int mesh, int material, Vec3 position) {
    Entity e;
    e.name = name;
    e.mesh = mesh;
    e.material = material;
    e.position = position;
    return e;
}

}  // namespace

Workshop buildWorkshop(World& w) {
    Workshop ws;

    // ---------------------------------------------------------------- 网格库
    // 每种形状只造一份，靠实体的位置/缩放摆出整间屋子
    const int mPlane = w.addMesh("plane", makePlane(12.0f));
    const int mWall = w.addMesh("wall", makeBox(Vec3{6.0f, 1.7f, 0.15f}));
    const int mSideWall = w.addMesh("sideWall", makeBox(Vec3{0.15f, 1.7f, 6.0f}));
    const int mTop = w.addMesh("deskTop", makeBox(Vec3{1.6f, 0.045f, 0.5f}));
    const int mLeg = w.addMesh("deskLeg", makeBox(Vec3{0.05f, 0.39f, 0.45f}));
    const int mMonitor = w.addMesh("monitor", makeBox(Vec3{0.32f, 0.21f, 0.03f}));
    const int mScreen = w.addMesh("screen", makeBox(Vec3{0.27f, 0.16f, 0.008f}));
    const int mKeyboard = w.addMesh("keyboard", makeBox(Vec3{0.3f, 0.012f, 0.11f}));
    const int mPedestal = w.addMesh("pedestal", makeCylinder(0.55f, 0.95f, 28));
    const int mOrb = w.addMesh("orb", makeSphere(0.45f, 36, 18));
    const int mLampBody = w.addMesh("lampBody", makeBox(Vec3{0.42f, 0.06f, 0.42f}));
    const int mBoard = w.addMesh("board", makeBox(Vec3{0.04f, 0.7f, 1.1f}));
    const int mShelf = w.addMesh("shelf", makeBox(Vec3{0.3f, 0.9f, 1.2f}));
    const int mCrate = w.addMesh("crate", makeBox(Vec3{0.4f, 0.4f, 0.4f}));

    // ---------------------------------------------------------------- 材质表
    // 名字会被 content/materials.txt 按同名覆盖 —— 这就是"数据热重载"的接口
    const Texture& checker = w.addTexture(makeChecker(128, Vec3{0.055f, 0.062f, 0.075f},
                                                     Vec3{0.70f, 0.72f, 0.76f}, 4));

    Material floor;
    floor.albedo = Vec3{1.0f, 1.0f, 1.0f};
    floor.roughness = 0.30f;
    floor.albedoTexture = &checker;
    floor.uvScale = Vec2{6.0f, 6.0f};
    ws.matFloor = w.addMaterial("floor", floor);

    Material wall;
    wall.albedo = Vec3{0.30f, 0.32f, 0.36f};
    wall.roughness = 0.88f;
    ws.matWall = w.addMaterial("wall", wall);

    Material ceiling;
    ceiling.albedo = Vec3{0.16f, 0.17f, 0.20f};
    ceiling.roughness = 0.95f;
    ws.matCeiling = w.addMaterial("ceiling", ceiling);

    Material wood;
    wood.albedo = Vec3{0.52f, 0.33f, 0.16f};
    wood.roughness = 0.55f;
    ws.matWood = w.addMaterial("wood", wood);

    Material chrome;
    chrome.albedo = Vec3{0.95f, 0.93f, 0.90f};
    chrome.roughness = 0.06f;
    chrome.metallic = 1.0f;
    ws.matChrome = w.addMaterial("chrome", chrome);

    Material plastic;
    plastic.albedo = Vec3{0.82f, 0.13f, 0.11f};
    plastic.roughness = 0.26f;
    ws.matPlastic = w.addMaterial("plastic", plastic);

    Material clay;
    clay.albedo = Vec3{0.74f, 0.53f, 0.32f};
    clay.roughness = 0.92f;
    ws.matClay = w.addMaterial("clay", clay);

    Material stone;
    stone.albedo = Vec3{0.52f, 0.53f, 0.55f};
    stone.roughness = 0.62f;
    ws.matStone = w.addMaterial("stone", stone);

    // 灯罩：自发光。它既是画面里"亮着的东西"，也是主光的位置
    Material lampMat;
    lampMat.albedo = Vec3{0.0f, 0.0f, 0.0f};
    lampMat.emissive = Vec3{4.2f, 3.8f, 3.0f};
    lampMat.roughness = 0.5f;
    ws.matLamp = w.addMaterial("lamp", lampMat);

    // 屏幕：暗房间里唯一亮着的东西 —— 它会把人引到终端面前
    Material screen;
    screen.albedo = Vec3{0.0f, 0.0f, 0.0f};
    screen.emissive = Vec3{0.35f, 1.35f, 1.05f};
    screen.roughness = 0.4f;
    ws.matScreen = w.addMaterial("screen", screen);

    Material board;
    board.albedo = Vec3{0.68f, 0.66f, 0.60f};
    board.roughness = 0.72f;
    ws.matBoard = w.addMaterial("board", board);

    // ---------------------------------------------------------------- 实体
    // 地板 / 天花板
    w.addEntity(makeEntity("地板", mPlane, ws.matFloor, Vec3{0.0f, 0.0f, 0.0f}));
    w.addEntity(makeEntity("天花板", mPlane, ws.matCeiling, Vec3{0.0f, 3.4f, 0.0f}));

    // 四面墙（内表面正好贴在 ±6 上）
    w.addEntity(makeEntity("后墙", mWall, ws.matWall, Vec3{0.0f, 1.7f, -6.15f}));
    w.addEntity(makeEntity("前墙", mWall, ws.matWall, Vec3{0.0f, 1.7f, 6.15f}));
    w.addEntity(makeEntity("左墙", mSideWall, ws.matWall, Vec3{-6.15f, 1.7f, 0.0f}));
    w.addEntity(makeEntity("右墙", mSideWall, ws.matWall, Vec3{6.15f, 1.7f, 0.0f}));

    // 靠后墙的桌子
    w.addEntity(makeEntity("桌面", mTop, ws.matWood, Vec3{0.0f, 0.78f, -5.0f}));
    w.addEntity(makeEntity("桌腿左", mLeg, ws.matWood, Vec3{-1.5f, 0.39f, -5.0f}));
    w.addEntity(makeEntity("桌腿右", mLeg, ws.matWood, Vec3{1.5f, 0.39f, -5.0f}));
    w.addEntity(makeEntity("键盘", mKeyboard, ws.matStone, Vec3{0.0f, 0.837f, -4.65f}));

    // 终端：显示器 + 屏幕。玩家走到它面前按 E，就是第 0 关的开场
    Entity terminal = makeEntity("终端", mMonitor, ws.matStone, Vec3{0.0f, 1.035f, -5.28f});
    terminal.prompt = "按 E 使用终端";
    terminal.command = "use terminal";
    ws.entTerminal = w.addEntity(terminal);

    Entity screenEnt = makeEntity("屏幕", mScreen, ws.matScreen, Vec3{0.0f, 1.035f, -5.245f});
    screenEnt.solid = false;
    ws.entScreen = w.addEntity(screenEnt);

    // 吊灯：主光在这里。挂在离天花板 0.5m 的位置，灯罩本体刚好挡住天花板上
    // 那个"距离 0.5m 的平方反比热点"，不然会糊成一大片白。
    Entity lamp = makeEntity("吊灯", mLampBody, ws.matLamp, Vec3{2.2f, 2.9f, 1.2f});
    lamp.solid = false;
    ws.entLamp = w.addEntity(lamp);

    // 左墙展板
    Entity boardEnt = makeEntity("展板", mBoard, ws.matBoard, Vec3{-5.94f, 1.9f, 1.2f});
    boardEnt.solid = false;
    boardEnt.prompt = "按 E 看展板";
    boardEnt.command = "look board";
    ws.entBoard = w.addEntity(boardEnt);

    // 右墙书柜 + 左前方的木箱（斜着摆，顺便验证 AABB 对任意角度都对）
    w.addEntity(makeEntity("书柜", mShelf, ws.matWood, Vec3{5.7f, 0.9f, 0.5f}));
    Entity crate = makeEntity("木箱", mCrate, ws.matWood, Vec3{-3.6f, 0.4f, 2.4f});
    crate.rotationY = radians(24.0f);
    w.addEntity(crate);

    // 三个展台 + 三个球：镜面 / 塑料 / 陶土。第 2 关「材质」就发生在这里。
    // 摆成右侧一列 —— 出生点正前方要留给桌子和终端，中间不能有东西挡视线。
    const float pedZ[3] = {-3.0f, -1.2f, 0.6f};
    const int orbMat[3] = {ws.matChrome, ws.matPlastic, ws.matClay};
    const char* orbName[3] = {"镜面球", "塑料球", "陶土球"};
    for (int i = 0; i < 3; ++i) {
        Entity ped = makeEntity("展台", mPedestal, ws.matStone, Vec3{3.6f, 0.475f, pedZ[i]});
        ped.name = std::string("展台") + char('A' + i);
        ws.entPedestals[i] = w.addEntity(ped);

        Entity orb = makeEntity(orbName[i], mOrb, orbMat[i], Vec3{3.6f, 1.4f, pedZ[i]});
        orb.prompt = "按 E 观察材质";
        orb.command = std::string("inspect ") + orbName[i];
        ws.entOrbs[i] = w.addEntity(orb);
    }

    // ---------------------------------------------------------------- 光照
    w.lights[0].position = Vec3{2.2f, 2.9f, 1.2f};  // = 吊灯本体位置
    w.lights[0].color = Vec3{1.0f, 0.93f, 0.82f};
    w.lights[0].intensity = 48.0f;
    // radius 是灯的物理尺寸：吊灯是块 0.84m 的发光板，所以给它 0.9 ——
    // 相当于一盏"软盒灯"，近距离不会数值爆炸（这就是 shader 里那行 dist2 + radius）
    w.lights[0].radius = 0.9f;

    w.lights[1].position = Vec3{-4.2f, 2.6f, 1.6f};  // 冷色补光，把暗部从纯黑里拉回来
    w.lights[1].color = Vec3{0.45f, 0.62f, 1.0f};
    w.lights[1].intensity = 26.0f;
    w.lights[1].radius = 0.45f;
    w.lightCount = 2;

    w.ambient = Vec3{0.42f, 0.44f, 0.50f};
    w.skyColor = Vec3{0.46f, 0.56f, 0.78f};
    w.groundColor = Vec3{0.24f, 0.20f, 0.17f};

    w.refreshBounds();
    return ws;
}

}  // namespace dlab
