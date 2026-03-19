#pragma once
#include "CoreTypes.h"
#include "GameObject.h"
#include "ColliderComponent.h"
#include "TransformComponent.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <utility>
#include <cmath>
#include <iostream>

// Scene 的完整定义在 CollisionSystem.cpp 中通过 #include "Scene.h" 引入
// 这里只做前向声明，避免循环包含
class Scene;

// ============================================================
//  碰撞对（保证 (A,B) == (B,A)）
//  通过构造时将较小的 ID 放在 a，较大的放在 b，
//  从而消除序形式（A,B）和（B,A）的重复
// ============================================================
struct CollisionPair
{
    GameObjectID a;  // 恒为较小的 ID
    GameObjectID b;  // 恒为较大的 ID

    // 构造时自动排序，保证 a < b
    CollisionPair(GameObjectID x, GameObjectID y)
        : a(x < y ? x : y), b(x < y ? y : x)
    {}

    bool operator==(const CollisionPair& o) const
    {
        return a == o.a && b == o.b;
    }
};

// 碰撞对的哈希函数（用于 unordered_set/unordered_map）
// 修复 #11：使用更散列的组合方式，减少连续小整数 ID 的哈希碰撞率
// 对称性已由 CollisionPair 构造函数保证（a < b）
struct CollisionPairHash
{
    size_t operator()(const CollisionPair& p) const
    {
        // 先对连续小整数做散列，再组合
        size_t h1 = static_cast<size_t>(p.a) * 2654435769ULL;
        size_t h2 = static_cast<size_t>(p.b) * 40503ULL;
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

using CollisionPairSet = std::unordered_set<CollisionPair, CollisionPairHash>;

// ============================================================
//  2D 窄相检测结果
//  包含：是否碰撞、接触点、碰撞法线、穿透深度
//  法线方向约定：从 B 指向 A（与 CollisionPair 中 a < b 对应）
// ============================================================
struct NarrowPhaseResult
{
    bool  hit          = false;       // 是否发生碰撞
    Vec2  contactPoint = { 0, 0 };   // 接触点（世界坐标）
    Vec2  normal       = { 0, 1 };   // 碰撞法线（从 B 指向 A）
    float penetration  = 0.0f;       // 穿透深度（两物体重叠的距离）
};

// ============================================================
//  宽相检测模式枚举
//  BruteForce：暴力 O(n2) 两两检测，简单易懂，适合碰撞体数量少的场景
//  AABBTree  ：使用 DynamicAABBTree 加速，O(n log n)，适合碰撞体数量多的场景
// ============================================================
enum class BroadPhaseMode
{
    BruteForce,  // 暴力两两检测 O(n2)，简单直观
    AABBTree     // AABB 树加速 O(n log n)，适合大量碰撞体
};

// ============================================================
//  CollisionSystem（2D 碰撞检测系统）
//
//  架构：
//    宽相（Broad Phase）：可切换暴力检测 / AABB 树加速
//    窄相（Narrow Phase）：精确 2D 形状检测
//      - Circle vs Circle
//      - Box vs Box（AABB SAT）
//      - Circle vs Box
//      - Capsule vs Circle
//      - Capsule vs Capsule
//      - Capsule vs Box
//    状态跟踪：Enter / Stay / Exit 事件分发
// ============================================================
class CollisionSystem
{
public:
    CollisionSystem() = default;

    // ========================================================
    //  宽相模式切换
    //  默认使用 AABBTree 模式（性能更优）
    //  调试或学习时可切换为 BruteForce 模式（逻辑更直观）
    // ========================================================
    void SetBroadPhaseMode(BroadPhaseMode mode) { m_broadPhaseMode = mode; }
    BroadPhaseMode GetBroadPhaseMode() const    { return m_broadPhaseMode; }

    // 主更新入口（每个物理帧由 Scene::FixedUpdate 调用一次）
    // 完整流程：收集碰撞体 → 层过滤 → 宽相(暴力/AABB树) → 窄相精确 → 分发事件
    void Update(Scene& scene);

    // 通知碰撞系统某个对象即将被销毁
    // 作用：清理 m_previousPairs 中涉及该对象的碰撞对，并触发 Exit 事件
    //       这样用户的 OnCollisionExit/OnTriggerExit 回调能正确收到通知
    void NotifyObjectDestroyed(Scene& scene, GameObjectID destroyedID);

    void PrintStats() const
    {
        std::cout << "[CollisionSystem] 当前碰撞对数=" << m_previousPairs.size() << "\n";
    }

private:
    // ========================================================
    //  宽相：AABB 快速排除
    //  用轴对齐包围盒快速判断两个碰撞体是否可能相交
    // ========================================================
    Vec2 GetAABBHalfExtents(const ColliderComponent* col) const;  // 获取碰撞体的 AABB 半尺寸
    bool AABBOverlap(const ColliderComponent* a, const ColliderComponent* b) const;  // 检查两个 AABB 是否重叠

    // ========================================================
    //  窄相：根据形状组合选择算法
    //  支持 6 种形状组合：
    //    Circle-Circle / Box-Box / Circle-Box
    //    Capsule-Circle / Capsule-Capsule / Capsule-Box
    // ========================================================
    NarrowPhaseResult NarrowPhase(const ColliderComponent* a, const ColliderComponent* b) const;  // 调度器

    NarrowPhaseResult TestCircleCircle(const ColliderComponent* a, const ColliderComponent* b) const;   // 圆-圆
    NarrowPhaseResult TestBoxBox(const ColliderComponent* a, const ColliderComponent* b) const;         // 矩形-矩形
    NarrowPhaseResult TestCircleBox(const ColliderComponent* circle, const ColliderComponent* box) const; // 圆-矩形
    NarrowPhaseResult TestCapsuleCircle(const ColliderComponent* capsule, const ColliderComponent* circle) const; // 胶囊-圆
    NarrowPhaseResult TestCapsuleCapsule(const ColliderComponent* a, const ColliderComponent* b) const;  // 胶囊-胶囊
    NarrowPhaseResult TestCapsuleBox(const ColliderComponent* capsule, const ColliderComponent* box) const; // 胶囊-矩形

    Vec2 FindBoxInteriorNormal2D(const Vec2& circleCenter, const Vec2& boxCenter, const Vec2& halfExtents) const;  // 圆心在Box内部时的法线

    // ========================================================
    //  2D 几何工具函数
    // ========================================================
    Vec2 ClosestPointOnSegment2D(const Vec2& point, const Vec2& segStart, const Vec2& segEnd) const;  // 点到线段最近点
    void ClosestPointsOnSegments2D(const Vec2& p1, const Vec2& p2,
                                   const Vec2& p3, const Vec2& p4,
                                   Vec2& outA, Vec2& outB) const;  // 两线段最近点对

    // ========================================================
    //  事件分发
    //  通过对比本帧和上帧的碰撞对集合，确定每对碰撞体的状态变化
    // ========================================================
    enum class EventType { Enter, Stay, Exit };  // 碰撞事件类型

    void DispatchEvents(Scene& scene,
                        const CollisionPairSet& currentPairs,
                        const std::unordered_map<CollisionPair,
                              NarrowPhaseResult, CollisionPairHash>& results);

    void FireEvent(Scene& scene,
                   const CollisionPair& pair,
                   const NarrowPhaseResult& res,
                   EventType type);

private:
    CollisionPairSet  m_previousPairs;  // 上一帧的碰撞对集合（用于对比生成 Enter/Stay/Exit 事件）
    BroadPhaseMode    m_broadPhaseMode = BroadPhaseMode::AABBTree;  // 宽相检测模式（默认使用 AABB 树）
};
