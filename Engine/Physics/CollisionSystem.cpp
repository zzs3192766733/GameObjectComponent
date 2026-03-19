#include "CollisionSystem.h"
#include "Scene.h"
#include "DynamicAABBTree.h"
#include "ScriptComponent.h"
#include <iostream>


// ============================================================
//  主更新入口（每个物理帧由 Scene::FixedUpdate 调用一次）
//  根据 m_broadPhaseMode 选择不同的宽相检测策略：
//    BruteForce：暴力 O(n2) 两两检测，简单直观，适合调试和学习
//    AABBTree  ：DynamicAABBTree 加速 O(n log n)，适合大量碰撞体
//  完整流程：
//    Step 1. 收集场景中所有启用的碰撞体
//    Step 2. 宽相检测（暴力/AABB树 二选一）+ 层过滤 + 窄相精确检测
//    Step 3. 对比上一帧的碰撞对集合，分发 Enter/Stay/Exit 事件
// ============================================================
void CollisionSystem::Update(Scene& scene)
{
    // Step 1. 收集所有启用的碰撞体（预分配64个空间减少扩容次数）
    std::vector<std::pair<GameObjectID, ColliderComponent*>> colliders;
    colliders.reserve(64);

    scene.ForEachWithComponent<ColliderComponent>(
        [&](GameObject* obj, ColliderComponent* col)
        {
            if (col->IsEnabled())
                colliders.emplace_back(obj->GetID(), col);
        }
    );

    CollisionPairSet currentPairs;  // 本帧检测到的所有碰撞对
    std::unordered_map<CollisionPair, NarrowPhaseResult, CollisionPairHash> currentResults;

    // Step 2. 根据宽相模式选择检测策略
    if (m_broadPhaseMode == BroadPhaseMode::BruteForce)
    {
        // ========================================================
        //  暴力模式 O(n2)
        //  对 n 个碰撞体，检查 n*(n-1)/2 个对
        //  流程：层过滤 → AABB 快速排除 → 窄相精确检测
        //  优点：逻辑简单直观，易于理解和调试
        //  缺点：碰撞体数量多时性能急剧下降
        // ========================================================
        for (int i = 0; i < static_cast<int>(colliders.size()); ++i)
        {
            for (int j = i + 1; j < static_cast<int>(colliders.size()); ++j)
            {
                auto [idA, colA] = colliders[i];
                auto [idB, colB] = colliders[j];

                // 第一关：碰撞层过滤（最廉价的检查，先做）
                // 修复 #15：改为双向必须匹配（双方都允许才碰撞，更符合物理引擎惯例）
                // 参考 Unity Physics Layer Collision Matrix：碰撞矩阵是对称的，双向必须同时匹配
                if (!colA->CanCollideWith(colB) || !colB->CanCollideWith(colA)) continue;

                // 第二关：宽相 AABB 重叠检测（O(1)，快速排除明显不相交的对）
                if (!AABBOverlap(colA, colB)) continue;

                // 第三关：窄相精确形状检测（较昂贵，只对通过宽相的对执行）
                NarrowPhaseResult result = NarrowPhase(colA, colB);
                if (!result.hit) continue;

                // 通过所有检测：记录碰撞对和检测结果
                CollisionPair pair(idA, idB);  // 构造函数保证 a < b，消除 (A,B)/(B,A) 重复
                currentPairs.insert(pair);
                currentResults[pair] = result;
            }
        }
    }
    else
    {
        // ========================================================
        //  AABB 树模式 O(n log n)
        //  利用 DynamicAABBTree 的树结构进行空间剪枝
        //  流程：构建 AABB 树 → 每个节点查询重叠 → 层过滤 → 窄相
        //  优点：碰撞体数量多时性能优势明显
        //  缺点：有额外的树构建开销，碰撞体少时可能比暴力慢
        // ========================================================
        DynamicAABBTree tree;
        std::unordered_map<int, int> nodeToIndex;  // 树节点ID → colliders数组索引
        // 修复 #2：新增反向映射，用 GameObjectID 去重代替不可靠的 nodeID 比较
        std::unordered_map<int, GameObjectID> nodeToGameObjectID;

        // 将所有碰撞体插入 AABB 树
        for (int i = 0; i < static_cast<int>(colliders.size()); ++i)
        {
            auto [id, col] = colliders[i];
            Vec2 center = col->GetWorldCenter();
            Vec2 halfExt = GetAABBHalfExtents(col);
            AABB2D aabb = AABB2D::FromCenterHalfExtents(center, halfExt);
            int nodeId = tree.Insert(aabb, nullptr);
            nodeToIndex[nodeId] = i;
            nodeToGameObjectID[nodeId] = id;  // 修复 #2：记录节点对应的 GameObjectID
        }

        // 宽相查询：对每个叶节点，查询与其 AABB 重叠的其他叶节点
        //   利用树的层级剪枝：如果内部节点的 AABB 不与查询区域重叠，跳过整棵子树
        for (auto& [nodeIdA, indexA] : nodeToIndex)
        {
            const AABB2D& aabbA = tree.GetFatAABB(nodeIdA);

            tree.Query(aabbA, [&](int nodeIdB) -> bool
            {
                // 修复 #2：使用 GameObjectID 去重，而非不可靠的内部 nodeID
                // 原来用 nodeIdB <= nodeIdA 去重，依赖节点分配顺序，增量更新树时会失效
                if (nodeIdB == nodeIdA) return true;  // 跳过自身
                
                GameObjectID goIdA = nodeToGameObjectID[nodeIdA];
                GameObjectID goIdB = nodeToGameObjectID[nodeIdB];
                if (goIdB <= goIdA) return true;  // 用 GameObjectID 去重（单调递增，可靠）

                auto itB = nodeToIndex.find(nodeIdB);
                if (itB == nodeToIndex.end()) return true;

                int indexB = itB->second;
                auto [idA, colA] = colliders[indexA];
                auto [idB, colB] = colliders[indexB];

                // 第一关：碰撞层过滤（最廉价的检查，先做）
                // 修复 #15：改为双向必须匹配
                if (!colA->CanCollideWith(colB) || !colB->CanCollideWith(colA)) return true;

                // 第二关：窄相精确形状检测
                NarrowPhaseResult result = NarrowPhase(colA, colB);
                if (!result.hit) return true;

                // 通过所有检测：记录碰撞对和检测结果
                CollisionPair pair(idA, idB);
                currentPairs.insert(pair);
                currentResults[pair] = result;
                return true;
            });
        }
    }

    // Step 3. 对比上一帧的碰撞对集合，分发 Enter/Stay/Exit 事件
    //   本帧有 + 上帧无 → Enter（新碰撞开始）
    //   本帧有 + 上帧有 → Stay（碰撞持续中）
    //   本帧无 + 上帧有 → Exit（碰撞结束）
    DispatchEvents(scene, currentPairs, currentResults);
    // 保存本帧碰撞对，下一帧用于对比
    m_previousPairs = std::move(currentPairs);
}



// ============================================================
//  宽相：AABB 重叠检测
//  AABB = Axis-Aligned Bounding Box（轴对齐包围盒）
//  原理：将任意形状用最小的矩形框起来，矩形边与坐标轴平行
//  两个 AABB 重叠的充要条件：在 X 和 Y 两个轴上的投影都重叠
//  计算量极小（4次减法+2次比较），适合作为第一轮快速筛选
// ============================================================
bool CollisionSystem::AABBOverlap(const ColliderComponent* a, const ColliderComponent* b) const
{
    Vec2 ca = a->GetWorldCenter();    // A 的世界中心
    Vec2 cb = b->GetWorldCenter();    // B 的世界中心
    Vec2 ha = GetAABBHalfExtents(a);  // A 的 AABB 半尺寸
    Vec2 hb = GetAABBHalfExtents(b);  // B 的 AABB 半尺寸

    // 中心距离 < 半尺寸之和 → 重叠
    return std::abs(ca.x - cb.x) < (ha.x + hb.x)
        && std::abs(ca.y - cb.y) < (ha.y + hb.y);
}

// ============================================================
//  宽相：获取碰撞体的 AABB 半尺寸
//  修复：考虑 TransformComponent 的缩放，确保碰撞体尺寸随对象缩放而变化
//  不同形状的包围盒计算方式：
//  - Box：半尺寸 × 缩放
//  - Circle：半径 × max(缩放)，取最大轴的缩放确保包围盒完全覆盖
//  - Capsule：分别考虑半径和半高的缩放
// ============================================================
Vec2 CollisionSystem::GetAABBHalfExtents(const ColliderComponent* col) const
{
    // 获取缩放系数（默认 (1,1)）
    Vec2 scale = { 1.0f, 1.0f };
    if (col->GetOwner())
    {
        if (auto* tf = col->GetOwner()->GetComponent<TransformComponent>())
            scale = tf->GetScale();
    }

    switch (col->GetShape())
    {
    case ColliderComponent::Shape::Box:
    {
        // Box 的半尺寸分别乘以对应轴的缩放系数
        Vec2 he = col->GetBoxHalfExtents();
        return { he.x * std::abs(scale.x), he.y * std::abs(scale.y) };
    }
    case ColliderComponent::Shape::Circle:
    {
        // 圆形在非等比缩放下会变成椭圆，
        // 用 AABB 包围时取两轴缩放的最大值确保完全包含
        float r = col->GetCircleRadius();
        float uniformScale = std::max(std::abs(scale.x), std::abs(scale.y));
        float scaledR = r * uniformScale;
        return { scaledR, scaledR };
    }
    case ColliderComponent::Shape::Capsule:
    {
        float r = col->GetCircleRadius();
        float hh = col->GetCapsuleHalfHeight();
        // 胶囊体的缩放较复杂：轴向缩放影响半高，径向缩放影响半径
        // 用最大缩放系数保证 AABB 完全包含
        float uniformScale = std::max(std::abs(scale.x), std::abs(scale.y));
        float scaledR = r * uniformScale;
        float scaledHH = hh * uniformScale;
        return { scaledR, scaledHH + scaledR };
    }
    }
    return { 0.5f, 0.5f };
}

// ============================================================
//  窄相：根据形状组合选择算法（调度器）
//  3种形状两两组合共 6 种情况（Box-Box, Circle-Circle, 
//  Circle-Box, Capsule-Circle, Capsule-Capsule, Capsule-Box）
//  对于不对称的组合（如 Circle-Box），交换参数后翻转法线方向
// ============================================================
NarrowPhaseResult CollisionSystem::NarrowPhase(const ColliderComponent* a, const ColliderComponent* b) const
{
    using Shape = ColliderComponent::Shape;
    Shape sa = a->GetShape();
    Shape sb = b->GetShape();

    if (sa == Shape::Circle && sb == Shape::Circle)
        return TestCircleCircle(a, b);

    if (sa == Shape::Box && sb == Shape::Box)
        return TestBoxBox(a, b);

    if (sa == Shape::Circle && sb == Shape::Box)
        return TestCircleBox(a, b);
    if (sa == Shape::Box && sb == Shape::Circle)
    {
        NarrowPhaseResult r = TestCircleBox(b, a);
        r.normal = r.normal * -1.0f;
        return r;
    }

    if (sa == Shape::Capsule && sb == Shape::Circle)
        return TestCapsuleCircle(a, b);
    if (sa == Shape::Circle && sb == Shape::Capsule)
    {
        NarrowPhaseResult r = TestCapsuleCircle(b, a);
        r.normal = r.normal * -1.0f;
        return r;
    }

    if (sa == Shape::Capsule && sb == Shape::Capsule)
        return TestCapsuleCapsule(a, b);

    if (sa == Shape::Capsule && sb == Shape::Box)
        return TestCapsuleBox(a, b);
    if (sa == Shape::Box && sb == Shape::Capsule)
    {
        NarrowPhaseResult r = TestCapsuleBox(b, a);
        r.normal = r.normal * -1.0f;
        return r;
    }

    return {};
}

// ============================================================
//  辅助函数：获取碰撞体的缩放系数
//  修复：所有窄相检测函数都需要考虑缩放
// ============================================================
static Vec2 GetColliderScale(const ColliderComponent* col)
{
    if (col->GetOwner())
    {
        if (auto* tf = col->GetOwner()->GetComponent<TransformComponent>())
            return tf->GetScale();
    }
    return { 1.0f, 1.0f };
}

// 获取考虑缩放后的统一缩放因子（用于 Circle/Capsule 等需要统一缩放的形状）
static float GetUniformScale(const ColliderComponent* col)
{
    Vec2 scale = GetColliderScale(col);
    return std::max(std::abs(scale.x), std::abs(scale.y));
}

// ============================================================
//  Circle vs Circle（圆与圆的碰撞检测）
//  修复：半径乘以缩放系数
//  最简单的碰撞检测：比较两圆心距离与半径之和
//  数学原理：|CA - CB| < RA + RB → 碰撞
// ============================================================
NarrowPhaseResult CollisionSystem::TestCircleCircle(const ColliderComponent* a,
                                                     const ColliderComponent* b) const
{
    NarrowPhaseResult result;
    Vec2  ca = a->GetWorldCenter();  // A 圆心
    Vec2  cb = b->GetWorldCenter();  // B 圆心
    // 修复：半径乘以统一缩放因子
    float ra = a->GetCircleRadius() * GetUniformScale(a); // A 缩放后半径
    float rb = b->GetCircleRadius() * GetUniformScale(b); // B 缩放后半径

    Vec2  diff = ca - cb;            // 从 B 指向 A 的向量
    float dist = diff.Length();      // 两圆心距离
    float sumR = ra + rb;            // 半径之和

    if (dist >= sumR) return result;  // 不重叠

    result.hit         = true;
    result.penetration = sumR - dist;  // 穿透深度 = 半径和 - 实际距离
    // 碰撞法线：从 B 指向 A 的单位向量
    // 如果两圆心完全重合（dist ≈ 0），使用默认向上方向避免除零
    result.normal      = (dist > 1e-6f) ? diff.Normalized() : Vec2::Up();
    // 接触点：A 圆心沿法线反方向偏移 A 的半径
    result.contactPoint = { ca.x - result.normal.x * ra,
                             ca.y - result.normal.y * ra };
    return result;
}

// ============================================================
//  Box vs Box（AABB 分离轴定理 / Separating Axis Theorem）
//  修复：半尺寸乘以缩放系数
//  原理：对于轴对齐矩形，只需检查 X 和 Y 两个分离轴
//  在每个轴上计算重叠量，选择重叠最小的轴作为碰撞法线方向
//  （最小穿透方向 = 最容易分离的方向）
// ============================================================
NarrowPhaseResult CollisionSystem::TestBoxBox(const ColliderComponent* a,
                                               const ColliderComponent* b) const
{
    NarrowPhaseResult result;
    Vec2 ca = a->GetWorldCenter();
    Vec2 cb = b->GetWorldCenter();
    // 修复：Box 半尺寸乘以对应轴的缩放系数
    Vec2 scaleA = GetColliderScale(a);
    Vec2 scaleB = GetColliderScale(b);
    Vec2 ha = { a->GetBoxHalfExtents().x * std::abs(scaleA.x),
                a->GetBoxHalfExtents().y * std::abs(scaleA.y) };
    Vec2 hb = { b->GetBoxHalfExtents().x * std::abs(scaleB.x),
                b->GetBoxHalfExtents().y * std::abs(scaleB.y) };

    // 计算 X 和 Y 轴上的重叠量（半尺寸之和 - 中心距离）
    float overlapX = (ha.x + hb.x) - std::abs(ca.x - cb.x);
    float overlapY = (ha.y + hb.y) - std::abs(ca.y - cb.y);

    // 任一轴上没有重叠 → 不碰撞（分离轴存在）
    if (overlapX <= 0 || overlapY <= 0) return result;

    result.hit = true;
    // 选择穿透最浅的轴作为碰撞法线（最小穿透深度原则）
    if (overlapX <= overlapY)
    {
        // X 轴穿透更浅 → 法线沿 X 方向
        result.penetration = overlapX;
        float sign = (ca.x > cb.x) ? 1.0f : -1.0f;  // 从 B 指向 A
        result.normal = { sign, 0.0f };
    }
    else
    {
        // Y 轴穿透更浅 → 法线沿 Y 方向
        result.penetration = overlapY;
        float sign = (ca.y > cb.y) ? 1.0f : -1.0f;  // 从 B 指向 A
        result.normal = { 0.0f, sign };
    }

    // 接触点：两个 AABB 重叠区域的中心
    result.contactPoint =
    {
        (std::max(ca.x - ha.x, cb.x - hb.x) + std::min(ca.x + ha.x, cb.x + hb.x)) * 0.5f,
        (std::max(ca.y - ha.y, cb.y - hb.y) + std::min(ca.y + ha.y, cb.y + hb.y)) * 0.5f
    };
    return result;
}

// ============================================================
//  Circle vs Box（圆与AABB的碰撞检测）
//  算法步骤：
//    1. 找到 Box 上距离圆心最近的点（Clamp 圆心到 Box 边界内）
//    2. 计算该点到圆心的距离
//    3. 距离 < 半径 → 碰撞
//  特殊情况：圆心在 Box 内部时，最近点就是圆心本身，
//    此时需要用 FindBoxInteriorNormal2D 计算法线方向
// ============================================================
NarrowPhaseResult CollisionSystem::TestCircleBox(const ColliderComponent* circle,
                                                  const ColliderComponent* box) const
{
    NarrowPhaseResult result;
    Vec2  cs = circle->GetWorldCenter();   // 圆心
    Vec2  cb = box->GetWorldCenter();      // 矩形中心
    // 修复：半尺寸和半径乘以缩放系数
    Vec2 boxScale = GetColliderScale(box);
    Vec2  hb = { box->GetBoxHalfExtents().x * std::abs(boxScale.x),
                 box->GetBoxHalfExtents().y * std::abs(boxScale.y) };
    float rs = circle->GetCircleRadius() * GetUniformScale(circle);

    // 找到 Box 表面（或内部）上距离圆心最近的点
    // 方法：将圆心坐标 Clamp（夹紧）到 Box 的范围内
    Vec2 closest =
    {
        std::max(cb.x - hb.x, std::min(cs.x, cb.x + hb.x)),
        std::max(cb.y - hb.y, std::min(cs.y, cb.y + hb.y))
    };

    Vec2  diff = cs - closest;  // 从最近点指向圆心
    float dist = diff.Length(); // 最近点到圆心的距离

    if (dist >= rs) return result;  // 距离 >= 半径，不碰撞

    result.hit          = true;
    result.penetration  = rs - dist;
    result.contactPoint = closest;  // 接触点就是 Box 上的最近点
    // 法线方向：从最近点指向圆心的单位向量
    // 如果圆心完全在 Box 内部（dist ≈ 0），需要特殊处理
    result.normal       = (dist > 1e-6f) ? diff.Normalized()
                                          : FindBoxInteriorNormal2D(cs, cb, hb);
    return result;
}

// ============================================================
//  Box 内部法线（当圆心完全在 Box 内部时使用）
//  原理：计算圆心到 Box 四条边的距离，
//        选择距离最近的边的外法线作为碰撞法线
//        （即最快推出圆的方向）
// ============================================================
Vec2 CollisionSystem::FindBoxInteriorNormal2D(const Vec2& circleCenter,
                                               const Vec2& boxCenter,
                                               const Vec2& halfExtents) const
{
    // 转换到 Box 的局部坐标系
    Vec2  local = circleCenter - boxCenter;
    // 计算到四条边的距离：右边、左边、上边、下边
    float dists[4] =
    {
        halfExtents.x - local.x,   // 到右边的距离
        halfExtents.x + local.x,   // 到左边的距离
        halfExtents.y - local.y,   // 到上边的距离
        halfExtents.y + local.y    // 到下边的距离
    };

    // 找距离最近的边
    int   minIdx  = 0;
    float minDist = dists[0];
    for (int i = 1; i < 4; ++i)
        if (dists[i] < minDist) { minDist = dists[i]; minIdx = i; }

    // 对应边的外法线方向：右(+x)、左(-x)、上(+y)、下(-y)
    static const Vec2 normals[4] = { { 1, 0 }, {-1, 0 }, { 0, 1 }, { 0,-1 } };
    return normals[minIdx];
}

// ============================================================
//  Capsule vs Circle（胶囊体与圆的碰撞检测）
//  修复：支持胶囊体旋转，用 TransformComponent::GetForward() 计算端点方向
//  胶囊体 = 线段 + 半径膨胀（Minkowski Sum）
//  本质上等价于：找到线段上离圆心最近的点，
//  然后做一次“两圆碰撞检测”（胶囊半径 + 圆半径）
// ============================================================
NarrowPhaseResult CollisionSystem::TestCapsuleCircle(const ColliderComponent* capsule,
                                                      const ColliderComponent* circle) const
{
    NarrowPhaseResult result;
    Vec2  cc  = capsule->GetWorldCenter();     // 胶囊中心
    // 修复：半径和半高乘以缩放系数
    float capsuleUniformScale = GetUniformScale(capsule);
    float rc  = capsule->GetCircleRadius() * capsuleUniformScale;    // 胶囊缩放后膨胀半径
    float hh  = capsule->GetCapsuleHalfHeight() * capsuleUniformScale; // 胶囊缩放后中轴半高
    Vec2  cs  = circle->GetWorldCenter();      // 圆心
    float rs  = circle->GetCircleRadius() * GetUniformScale(circle); // 圆缩放后半径

    // 修复：根据旋转角度计算胶囊的中轴方向
    // 默认端点方向为竖直（+Y），旋转后沿物体的服向方向
    Vec2 axis = Vec2::Up();  // 默认方向
    if (capsule->GetOwner())
    {
        if (auto* tf = capsule->GetOwner()->GetComponent<TransformComponent>())
        {
            float rad = tf->GetRotationRad();
            // 胶囊体轴向 = 垂直于 forward 的方向（即旋转后的 up 方向）
            axis = { -std::sin(rad), std::cos(rad) };
        }
    }

    // 构建胶囊的中轴线段端点（沿轴向上下偏移）
    Vec2 capTop    = cc + axis * hh;   // 上端点
    Vec2 capBottom = cc - axis * hh;   // 下端点

    // 找到中轴线段上离圆心最近的点
    Vec2 closest = ClosestPointOnSegment2D(cs, capBottom, capTop);
    Vec2  diff = cs - closest;       // 从最近点指向圆心
    float dist = diff.Length();      // 最近点到圆心的距离
    float sumR = rc + rs;            // 碰撞判定距离 = 两半径之和

    if (dist >= sumR) return result;  // 不碰撞

    result.hit          = true;
    result.penetration  = sumR - dist;
    result.contactPoint = closest;
    result.normal       = (dist > 1e-6f) ? diff.Normalized() : Vec2::Up();
    return result;
}

// ============================================================
//  Capsule vs Capsule（胶囊体与胶囊体的碰撞检测）
//  修复：支持胶囊体旋转
//  原理：找到两条中轴线段之间的最近点对，
//  然后做两个膨胀圆的碰撞检测（等价于两圆检测）
// ============================================================
NarrowPhaseResult CollisionSystem::TestCapsuleCapsule(const ColliderComponent* a,
                                                       const ColliderComponent* b) const
{
    NarrowPhaseResult result;
    Vec2  ca = a->GetWorldCenter();
    // 修复：半径和半高乘以缩放系数
    float scaleA = GetUniformScale(a);
    float ra = a->GetCircleRadius() * scaleA;
    float ha = a->GetCapsuleHalfHeight() * scaleA;

    Vec2  cb = b->GetWorldCenter();
    float scaleB = GetUniformScale(b);
    float rb = b->GetCircleRadius() * scaleB;
    float hb = b->GetCapsuleHalfHeight() * scaleB;

    // 修复：根据旋转角度计算胶囊轴向
    auto getCapsuleAxis = [](const ColliderComponent* cap) -> Vec2 {
        if (cap->GetOwner())
        {
            if (auto* tf = cap->GetOwner()->GetComponent<TransformComponent>())
            {
                float rad = tf->GetRotationRad();
                return { -std::sin(rad), std::cos(rad) };
            }
        }
        return Vec2::Up();
    };

    Vec2 axisA = getCapsuleAxis(a);
    Vec2 axisB = getCapsuleAxis(b);

    // 构建两个胶囊的中轴线段端点（沿各自轴向）
    Vec2 aTop    = ca + axisA * ha;
    Vec2 aBottom = ca - axisA * ha;
    Vec2 bTop    = cb + axisB * hb;
    Vec2 bBottom = cb - axisB * hb;

    // 找两条线段之间的最近点对（3D 几何中经典的线段-线段最近点算法）
    Vec2 ptA, ptB;
    ClosestPointsOnSegments2D(aBottom, aTop, bBottom, bTop, ptA, ptB);

    // 转化为两圆检测：最近点 + 各自半径
    Vec2  diff = ptA - ptB;
    float dist = diff.Length();
    float sumR = ra + rb;

    if (dist >= sumR) return result;

    result.hit          = true;
    result.penetration  = sumR - dist;
    result.contactPoint = { (ptA.x + ptB.x) * 0.5f, (ptA.y + ptB.y) * 0.5f };
    result.normal       = (dist > 1e-6f) ? diff.Normalized() : Vec2::Up();
    return result;
}

// ============================================================
//  Capsule vs Box（胶囊体与矩形的碰撞检测）
//  修复：支持胶囊体旋转
//  算法：
//    1. 找到胶囊中轴线段上离 Box 中心最近的点
//    2. 找到 Box 边界上离该点最近的点（Clamp）
//    3. 检查两点距离是否小于胶囊半径
// ============================================================
NarrowPhaseResult CollisionSystem::TestCapsuleBox(const ColliderComponent* capsule,
                                                   const ColliderComponent* box) const
{
    NarrowPhaseResult result;
    Vec2  cc  = capsule->GetWorldCenter();
    // 修复：半径和半高乘以缩放系数
    float capsUniformScale = GetUniformScale(capsule);
    float rc  = capsule->GetCircleRadius() * capsUniformScale;
    float hh  = capsule->GetCapsuleHalfHeight() * capsUniformScale;
    Vec2  cb  = box->GetWorldCenter();
    Vec2  boxSc = GetColliderScale(box);
    Vec2  hb  = { box->GetBoxHalfExtents().x * std::abs(boxSc.x),
                  box->GetBoxHalfExtents().y * std::abs(boxSc.y) };

    // 修复：根据旋转角度计算胶囊轴向
    Vec2 axis = Vec2::Up();
    if (capsule->GetOwner())
    {
        if (auto* tf = capsule->GetOwner()->GetComponent<TransformComponent>())
        {
            float rad = tf->GetRotationRad();
            axis = { -std::sin(rad), std::cos(rad) };
        }
    }

    Vec2 capTop    = cc + axis * hh;
    Vec2 capBottom = cc - axis * hh;

    Vec2 closestOnSeg = ClosestPointOnSegment2D(cb, capBottom, capTop);
    Vec2 closestOnBox =
    {
        std::max(cb.x - hb.x, std::min(closestOnSeg.x, cb.x + hb.x)),
        std::max(cb.y - hb.y, std::min(closestOnSeg.y, cb.y + hb.y))
    };

    Vec2  diff = closestOnSeg - closestOnBox;
    float dist = diff.Length();

    if (dist >= rc) return result;

    result.hit          = true;
    result.penetration  = rc - dist;
    result.contactPoint = closestOnBox;
    result.normal       = (dist > 1e-6f) ? diff.Normalized()
                                          : FindBoxInteriorNormal2D(closestOnSeg, cb, hb);
    return result;
}

// ============================================================
//  2D 几何工具函数
// ============================================================

// 求点到线段的最近点
// 将点投影到线段所在直线上，得到参数 t ∈ [0,1]：
//   t = dot(point - segStart, segEnd - segStart) / |segEnd - segStart|2
// 然后 clamp t 到 [0,1] 范围内（保证结果在线段上而非延长线上）
Vec2 CollisionSystem::ClosestPointOnSegment2D(const Vec2& point,
                                               const Vec2& segStart,
                                               const Vec2& segEnd) const
{
    Vec2  d    = segEnd - segStart;  // 线段方向向量
    float len2 = d.LengthSq();      // 线段长度的平方
    if (len2 < 1e-10f) return segStart;  // 退化为点（线段长度为0）

    Vec2  v = point - segStart;
    float t = v.Dot(d) / len2;                  // 投影参数
    t = std::max(0.0f, std::min(1.0f, t));      // 限制在线段范围内
    return { segStart.x + d.x * t, segStart.y + d.y * t };  // 插值得到最近点
}

// 求两条线段之间的最近点对
// 参数化两条线段：
//   线段1: P(s) = p1 + s * (p2 - p1),  s ∈ [0,1]
//   线段2: Q(t) = p3 + t * (p4 - p3),  t ∈ [0,1]
// 求使 |P(s) - Q(t)| 最小的 (s, t)，并返回对应的两个点
// 算法参考：Real-Time Collision Detection, Christer Ericson, Chapter 5.1.9
void CollisionSystem::ClosestPointsOnSegments2D(const Vec2& p1, const Vec2& p2,
                                                 const Vec2& p3, const Vec2& p4,
                                                 Vec2& outA, Vec2& outB) const
{
    Vec2  d1 = p2 - p1;   // 线段1方向
    Vec2  d2 = p4 - p3;   // 线段2方向
    Vec2  r  = p1 - p3;   // 起点之差

    float a = d1.Dot(d1); // 线段1长度2
    float e = d2.Dot(d2); // 线段2长度2
    float f = d2.Dot(r);

    float s, t;

    if (a <= 1e-10f && e <= 1e-10f)
    {
        outA = p1; outB = p3; return;
    }
    if (a <= 1e-10f)
    {
        s = 0.0f;
        t = std::max(0.0f, std::min(1.0f, f / e));
    }
    else
    {
        float c = d1.Dot(r);
        if (e <= 1e-10f)
        {
            t = 0.0f;
            s = std::max(0.0f, std::min(1.0f, -c / a));
        }
        else
        {
            float b     = d1.Dot(d2);
            float denom = a * e - b * b;

            if (std::abs(denom) > 1e-10f)
                s = std::max(0.0f, std::min(1.0f, (b * f - c * e) / denom));
            else
                s = 0.0f;

            t = (b * s + f) / e;
            if (t < 0.0f)
            {
                t = 0.0f;
                s = std::max(0.0f, std::min(1.0f, -c / a));
            }
            else if (t > 1.0f)
            {
                t = 1.0f;
                s = std::max(0.0f, std::min(1.0f, (b - c) / a));
            }
        }
    }

    outA = { p1.x + d1.x * s, p1.y + d1.y * s };
    outB = { p3.x + d2.x * t, p3.y + d2.y * t };
}

// ============================================================
//  事件分发（碰撞状态机）
//  通过对比「本帧碰撞对集合」和「上帧碰撞对集合」，
//  确定每对碰撞体的状态变化，分发对应事件：
//    Enter：本帧有 & 上帧无 → 新碰撞开始
//    Stay ：本帧有 & 上帧有 → 碰撞持续中
//    Exit ：本帧无 & 上帧有 → 碰撞结束
// ============================================================
void CollisionSystem::DispatchEvents(
    Scene& scene,
    const CollisionPairSet& currentPairs,
    const std::unordered_map<CollisionPair, NarrowPhaseResult, CollisionPairHash>& results)
{
    // 1. Enter 事件：本帧新出现的碰撞对（上帧不存在）
    for (const auto& pair : currentPairs)
    {
        if (m_previousPairs.find(pair) == m_previousPairs.end())
            FireEvent(scene, pair, results.at(pair), EventType::Enter);
    }

    // 2. Stay 事件：两帧都存在的碰撞对（持续碰撞）
    for (const auto& pair : currentPairs)
    {
        if (m_previousPairs.find(pair) != m_previousPairs.end())
            FireEvent(scene, pair, results.at(pair), EventType::Stay);
    }

    // 3. Exit 事件：上帧存在但本帧消失的碰撞对（碰撞结束）
    //    此时没有新的检测结果，传入空的 NarrowPhaseResult
    for (const auto& pair : m_previousPairs)
    {
        if (currentPairs.find(pair) == currentPairs.end())
            FireEvent(scene, pair, NarrowPhaseResult{}, EventType::Exit);
    }
}

// ============================================================
//  NotifyObjectDestroyed（对象销毁时的碰撞对清理）
//  修复：当对象被销毁时，必须清理 m_previousPairs 中涉及该对象的碰撞对
//        并触发 Exit 事件，否则用户不会收到 OnCollisionExit/OnTriggerExit 回调
//  注意：此时被销毁的对象尚未从 Scene 中移除（Stop 已调用但尚未 erase）
//        所以 FireEvent 仍然可以通过 scene.GetGameObject 找到它
// ============================================================
void CollisionSystem::NotifyObjectDestroyed(Scene& scene, GameObjectID destroyedID)
{
    // 收集所有涉及被销毁对象的碰撞对
    std::vector<CollisionPair> pairsToRemove;
    for (const auto& pair : m_previousPairs)
    {
        if (pair.a == destroyedID || pair.b == destroyedID)
            pairsToRemove.push_back(pair);
    }

    // 对每个碰撞对触发 Exit 事件（给存活的一方发送通知）
    for (const auto& pair : pairsToRemove)
    {
        FireEvent(scene, pair, NarrowPhaseResult{}, EventType::Exit);
        m_previousPairs.erase(pair);
    }
}

// ============================================================
//  事件触发（向碰撞对的双方分发回调）
//  流程：
//    1. 获取双方的 GameObject 和 ColliderComponent
//    2. 判断是否为 Trigger 碰撞（任一方是 Trigger 即为 Trigger 碰撞）
//    3. 构造 CollisionInfo（双方视角的法线方向相反）
//    4. 分发到 ColliderComponent 的回调
//    5. 分发到 ScriptComponent 的回调（如果有的话）
// ============================================================
void CollisionSystem::FireEvent(
    Scene& scene,
    const CollisionPair& pair,
    const NarrowPhaseResult& res,
    EventType type)
{
    GameObject* objA = scene.GetGameObject(pair.a);
    GameObject* objB = scene.GetGameObject(pair.b);
    if (!objA || !objB) return;  // 对象可能在碰撞期间被销毁

    ColliderComponent* colA = objA->GetComponent<ColliderComponent>();
    ColliderComponent* colB = objB->GetComponent<ColliderComponent>();
    if (!colA || !colB) return;

    // 判断是否为 Trigger 碰撞
    // Trigger 碰撞不产生物理响应，只触发事件（如道具拾取、区域检测等）
    bool isTrigger = colA->IsTrigger() || colB->IsTrigger();

    // 构造双方各自视角的碰撞信息
    // A 看到的：对方是 B，法线从 B 指向 A
    CollisionInfo infoA;
    infoA.otherID       = pair.b;
    infoA.contactPoint  = res.contactPoint;
    infoA.contactNormal = res.normal;          // 法线方向：B → A
    infoA.penetration   = res.penetration;
    infoA.isTrigger     = isTrigger;

    // B 看到的：对方是 A，法线方向相反（A → B）
    CollisionInfo infoB;
    infoB.otherID       = pair.a;
    infoB.contactPoint  = res.contactPoint;
    infoB.contactNormal = res.normal * -1.0f;  // 法线方向翻转：A → B
    infoB.penetration   = res.penetration;
    infoB.isTrigger     = isTrigger;

    // 分发 ColliderComponent 回调（直接注册在碰撞体上的回调）
    if (isTrigger)
    {
        switch (type)
        {
        case EventType::Enter:
            colA->FireTriggerEnter(infoA); colB->FireTriggerEnter(infoB); break;
        case EventType::Stay:
            colA->FireTriggerStay(infoA);  colB->FireTriggerStay(infoB);  break;
        case EventType::Exit:
            colA->FireTriggerExit(infoA);  colB->FireTriggerExit(infoB);  break;
        }
    }
    else
    {
        switch (type)
        {
        case EventType::Enter:
            colA->FireCollisionEnter(infoA); colB->FireCollisionEnter(infoB); break;
        case EventType::Stay:
            colA->FireCollisionStay(infoA);  colB->FireCollisionStay(infoB);  break;
        case EventType::Exit:
            colA->FireCollisionExit(infoA);  colB->FireCollisionExit(infoB);  break;
        }
    }

    // 分发 ScriptComponent 碰撞回调（脚本组件的碰撞事件）
    // 注意：ScriptComponent 只接收 Enter/Exit 事件，不接收 Stay
    //       （Stay 事件频率太高，通常脚本不需要每帧处理持续碰撞）
    ScriptComponent* scriptA = objA->GetComponent<ScriptComponent>();
    ScriptComponent* scriptB = objB->GetComponent<ScriptComponent>();

    if (isTrigger)
    {
        switch (type)
        {
        case EventType::Enter:
            if (scriptA) scriptA->TriggerTriggerEnter(pair.b);
            if (scriptB) scriptB->TriggerTriggerEnter(pair.a);
            break;
        case EventType::Exit:
            if (scriptA) scriptA->TriggerTriggerExit(pair.b);
            if (scriptB) scriptB->TriggerTriggerExit(pair.a);
            break;
        default: break;
        }
    }
    else
    {
        switch (type)
        {
        case EventType::Enter:
            if (scriptA) scriptA->TriggerCollisionEnter(pair.b);
            if (scriptB) scriptB->TriggerCollisionEnter(pair.a);
            break;
        case EventType::Exit:
            if (scriptA) scriptA->TriggerCollisionExit(pair.b);
            if (scriptB) scriptB->TriggerCollisionExit(pair.a);
            break;
        default: break;
        }
    }
}

