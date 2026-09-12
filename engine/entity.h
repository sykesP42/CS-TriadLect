// 场景里的"一个东西"。
//
// P0 的变换刻意只给 位置 / 绕 Y 旋转 / 缩放 三种 —— 够摆出一间工作室，
// 而且世界空间包围盒、碰撞、交互射线全都能一眼算对。
// 换成完整矩阵的话，AABB 就得做 8 个角点变换，P0 不值得。
#pragma once

#include <string>

#include "../core/mat.h"
#include "../core/vec.h"
#include "../engine/mesh.h"

namespace dlab {

struct Entity {
    std::string name;     // 控制台和关卡用它引用物体，如 "展台1"
    int mesh = 0;         // World::meshes 的下标
    int material = 0;     // World::materials 的下标

    Vec3 position{0.0f, 0.0f, 0.0f};
    float rotationY = 0.0f;  // 弧度
    Vec3 scale{1.0f, 1.0f, 1.0f};

    // 世界空间包围盒 —— refreshBounds() 按网格包围盒 + 上面的变换算好
    Vec3 aabbMin{0.0f, 0.0f, 0.0f};
    Vec3 aabbMax{0.0f, 0.0f, 0.0f};

    bool solid = true;    // 挡不挡路
    bool visible = true;

    // 交互：看着它按 E 时执行 command（复用控制台解释器，一条代码路径）。
    // prompt 是准星旁的提示文字，留空表示不可交互。
    std::string prompt;
    std::string command;
};

inline Mat4 entityTransform(const Entity& e) {
    return translation(e.position) * rotationY(e.rotationY) * scaling(e.scale);
}

inline Vec3 aabbCenter(const Entity& e) { return (e.aabbMin + e.aabbMax) * 0.5f; }

}  // namespace dlab
