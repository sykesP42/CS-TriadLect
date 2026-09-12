// 关卡表 + 评委机位（"判定怎么算"这件事只在这里发生一次，所有关卡共用）。
//
//  加一关要做的事：
//    ① 在 game/levels/ 下新建一个 .cpp，实现一个返回 const Level& 的函数；
//    ② 在 level.h 里给它加一行声明；
//    ③ 把它填进下面的 kLevels 表。
//  build.bat 已经会把 game/levels/*.cpp 自动编进去，不用改构建脚本。
#include "level.h"

namespace dlab {

namespace {

const Level* const kLevels[] = {
    &levelDark(),  // 第 0 关
};

constexpr int kLevelCount = int(sizeof(kLevels) / sizeof(kLevels[0]));

}  // namespace

int levelCount() { return kLevelCount; }

const Level& levelAt(int index) {
    if (index < 0) index = 0;
    if (index >= kLevelCount) index = kLevelCount - 1;
    return *kLevels[index];
}

int findLevel(const std::string& id) {
    for (int i = 0; i < kLevelCount; ++i) {
        if (id == kLevels[i]->id) return i;
    }
    return -1;
}

LevelStatus Judge::evaluate(const World& world, const Level& lv) {
    if (rz_.framebuffer().width != kJudgeWidth || rz_.framebuffer().height != kJudgeHeight) {
        rz_.resize(kJudgeWidth, kJudgeHeight);
    }
    // 评委相机拍的是"场景本身"：没东西挡着的地方就是纯黑，不铺底色。
    // 关卡问的是"屋里亮起来了吗"，不是"背景有多亮"—— 要是拿界面的底色当亮度，
    // 一台对着墙外的相机也能把进度条推满。
    rz_.framebuffer().clear(Vec3{0.0f, 0.0f, 0.0f});
    world.render(rz_, lv.judge);

    LevelStatus st;
    st.luminance = rz_.framebuffer().meanLuminance();
    if (lv.progress != nullptr) {
        const LevelView view{world, rz_.framebuffer(), st.luminance};
        st.progress = clampf(lv.progress(view), 0.0f, 1.0f);
    }
    return st;
}

}  // namespace dlab
