#pragma once
#include "CoreTypes.h"
#include "TransformComponent.h"  // Vec2 定义在此
#include <vector>

#include <functional>
#include <cassert>
#include <limits>

// ============================================================
//  AABB2D  —— 2D 轴对齐包围盒
//  用途：将任意 2D 形状包裹在一个与坐标轴平行的矩形中
//  特点：重叠检测极快（4次比较），但不旋转（无法紧密贴合旋转后的形状）
// ============================================================
struct AABB2D
{
    Vec2 min{ 0, 0 };
    Vec2 max{ 0, 0 };

    static AABB2D FromCenterHalfExtents(const Vec2& center, const Vec2& half)
    {
        return { center - half, center + half };
    }

    // 合并两个 AABB
    static AABB2D Merge(const AABB2D& a, const AABB2D& b)
    {
        return
        {
            { std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y) },
            { std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y) }
        };
    }

    // 是否与另一个 AABB 相交（分离轴检测：任意一轴上不重叠则不相交）
    bool Overlaps(const AABB2D& o) const
    {
        if (max.x < o.min.x || o.max.x < min.x) return false;
        if (max.y < o.min.y || o.max.y < min.y) return false;
        return true;
    }

    // 是否完全包含另一个 AABB（用于判断是否需要重建节点）
    bool Contains(const AABB2D& o) const
    {
        return min.x <= o.min.x && min.y <= o.min.y
            && max.x >= o.max.x && max.y >= o.max.y;
    }

    // 表面积（用于 SAH 启发式选择插入位置）
    // 在 2D 中是周长的 2 倍（与 3D 中的表面积对应）
    // SAH 原理：插入新节点时，选择使总表面积增加最少的兄弟节点
    float Area() const
    {
        Vec2 d = max - min;
        return 2.0f * (d.x + d.y);
    }

    Vec2 Center() const
    {
        return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f };
    }
};

// ============================================================
//  DynamicAABBTree（动态 AABB 树）
//
//  TODO #14：当前为 header-only 实现（约 670 行），
//  复杂函数（InsertLeaf, Rotate 等）不应 inline——
//  编译器几乎不会真的内联，但 header-only 导致每个包含它的
//  .cpp 都编译一份。建议后续移到 DynamicAABBTree.cpp。
//
//  用途：2D 碰撞宽相加速，将 O(n2) 的暴力两两检测降至 O(n log n)
//
//  核心思想：
//    将所有碰撞体的 AABB 组织成二叉树，
//    每个内部节点的 AABB = 子节点 AABB 的并集，
//    查询时从根节点开始，若当前节点的 AABB 不与查询区域重叠，
//    则跳过整棵子树（剪枝）
//
//  特性：
//    - 插入/删除 O(log n)
//    - 查询重叠对 O(n log n)
//    - 使用 fatAABB（扩大边距）减少移动时重建频率
//    - 节点池 + 空闲链表避免频繁 new/delete
//    - AVL 旋转保持树高度平衡
// ============================================================
class DynamicAABBTree
{
public:
    // 无效节点 ID
    static constexpr int NULL_NODE = -1;

    // fat margin：将 AABB 向外扩展，减少移动时重建频率
    // 原理：碰撞体小幅移动时，只要新 AABB 仍在 fatAABB 内就不需要重建树
    // 值越大，重建越少，但宽相误报越多（权衡取舒）
    static constexpr float FAT_MARGIN = 0.2f;

    // 用户数据类型（存储碰撞体指针或 ID）
    using UserData = void*;

    DynamicAABBTree();
    ~DynamicAABBTree() = default;

    // 插入一个叶节点，返回节点 ID
    int  Insert(const AABB2D& aabb, UserData userData);

    // 删除一个叶节点
    void Remove(int nodeId);

    // 移动一个叶节点（AABB 变化时调用，如碰撞体位置变化）
    // 优化：如果新 AABB 仍在 fat AABB 内则不重建，返回 false
    // 否则重建（先删除再插入），返回 true
    bool Move(int nodeId, const AABB2D& newAABB);

    // 查询与给定 AABB 重叠的所有叶节点
    // 利用树结构剪枝：如果内部节点的 AABB 不与查询区域重叠，跳过整棵子树
    // callback 返回 false 则提前终止查询
    void Query(const AABB2D& aabb, const std::function<bool(int)>& callback) const;

    // 遍历所有重叠对（宽相主入口）
    // callback(nodeIdA, nodeIdB) 返回 false 则停止
    // ? 当前实现是收集所有叶节点后暴力 O(n2) 两两检查，
    //    未充分利用树结构剪枝，后续可优化为对每个叶节点调用 Query
    void QueryAllPairs(const std::function<bool(int, int)>& callback) const;

    // 获取节点的 fat AABB
    const AABB2D& GetFatAABB(int nodeId) const { return m_nodes[nodeId].fatAABB; }

    // 获取节点的用户数据
    UserData GetUserData(int nodeId) const { return m_nodes[nodeId].userData; }

    // 是否为叶节点
    bool IsLeaf(int nodeId) const { return m_nodes[nodeId].child1 == NULL_NODE; }

    // 当前叶节点数量
    int GetLeafCount() const { return m_leafCount; }

    // 重置（清空所有节点）
    void Clear();

private:
    struct Node
    {
        AABB2D   fatAABB;                // 包含边距的包围盒（叶节点：fat化后的AABB，内部节点：子节点的并集）
        UserData userData = nullptr;     // 用户数据（仅叶节点有意义，储存碰撞体指针或 ID）
        int      parent   = NULL_NODE;   // 父节点索引
        int      child1   = NULL_NODE;   // 左子节点（NULL_NODE 表示叶节点）
        int      child2   = NULL_NODE;   // 右子节点
        int      height   = 0;           // 树高：叶节点=0，内部节点=max(子树高)+1
        int      next     = NULL_NODE;   // 空闲链表指针（节点被回收后串入空闲链表）

        bool IsLeaf() const { return child1 == NULL_NODE; }  // 无子节点即为叶节点
    };

    // 节点池管理（避免频繁 new/delete）
    // AllocNode 优先从空闲链表取，没有则向 vector 末尾追加
    // FreeNode 将节点归还到空闲链表头部
    int  AllocNode();
    void FreeNode(int nodeId);

    // 树操作：插入/删除叶节点
    // InsertLeaf：找最佳兄弟 → 创建新内部节点 → 向上修复
    // RemoveLeaf：用兄弟替代父节点 → 向上修复
    void InsertLeaf(int leafId);
    void RemoveLeaf(int leafId);

    // 选择最佳兄弟节点（SAH 启发式）
    // SAH = Surface Area Heuristic（表面积启发式）
    // 目标：找到一个节点，使得插入后树的总表面积增加最少
    int  FindBestSibling(const AABB2D& leafAABB) const;

    // 插入后向上修复 AABB 和高度（从插入点往根方向遍历）
    void FixUpwards(int nodeId);

    // AVL 旋转（保持树高度平衡，防止退化为链表）
    // 左右子树高度差 > 1 时触发旋转
    int  Rotate(int nodeId);

private:
    std::vector<Node> m_nodes;         // 节点池（所有节点存储在这里，通过索引引用）
    int               m_root      = NULL_NODE;   // 根节点索引
    int               m_freeList  = NULL_NODE;   // 空闲节点链表头
    int               m_leafCount = 0;           // 叶节点计数（碰撞体数量）
};

// ============================================================
//  实现（header-only，避免额外 .cpp）
//  采用 header-only 是因为 DynamicAABBTree 作为工具类可能被多个编译单元引用，
//  使用 inline 函数避免链接时的重复定义错误
// ============================================================

// 初始化：预分配 64 个节点空间（减少早期扩容次数）
inline DynamicAABBTree::DynamicAABBTree()
{
    m_nodes.reserve(64);
}

// 分配节点：优先从空闲链表取（复用已释放的节点），否则追加新节点
inline int DynamicAABBTree::AllocNode()
{
    if (m_freeList != NULL_NODE)
    {
        int id      = m_freeList;
        m_freeList  = m_nodes[id].next;
        m_nodes[id] = Node{};
        return id;
    }
    m_nodes.emplace_back();
    return static_cast<int>(m_nodes.size()) - 1;
}

// 释放节点：归还到空闲链表头部（不实际释放内存，只是标记为可复用）
// 修复 #8：重置节点关键字段，避免通过缓存的旧 nodeId 访问到过期数据
inline void DynamicAABBTree::FreeNode(int nodeId)
{
    m_nodes[nodeId].parent   = NULL_NODE;
    m_nodes[nodeId].child1   = NULL_NODE;
    m_nodes[nodeId].child2   = NULL_NODE;
    m_nodes[nodeId].height   = -1;        // 用 -1 标记为已释放
    m_nodes[nodeId].userData = nullptr;
    m_nodes[nodeId].next     = m_freeList;
    m_freeList = nodeId;
}

// 重置树：清空所有节点，回到初始状态
inline void DynamicAABBTree::Clear()
{
    m_nodes.clear();
    m_root      = NULL_NODE;
    m_freeList  = NULL_NODE;
    m_leafCount = 0;
}

// 插入一个叶节点
// 流程：分配节点 → 设置 fatAABB（原始 AABB + 边距） → 插入树中
inline int DynamicAABBTree::Insert(const AABB2D& aabb, UserData userData)
{
    int leafId = AllocNode();
    // 扩大 fat AABB：向四周各扩展 FAT_MARGIN
    // 这样碰撞体小幅移动时不需要重建树结构
    m_nodes[leafId].fatAABB  = { aabb.min - Vec2{FAT_MARGIN, FAT_MARGIN},
                                  aabb.max + Vec2{FAT_MARGIN, FAT_MARGIN} };
    m_nodes[leafId].userData = userData;
    m_nodes[leafId].height   = 0;

    InsertLeaf(leafId);
    ++m_leafCount;
    return leafId;
}

inline void DynamicAABBTree::Remove(int nodeId)
{
    assert(IsLeaf(nodeId));
    RemoveLeaf(nodeId);
    FreeNode(nodeId);
    --m_leafCount;
}

// 移动叶节点：当碰撞体位置变化时调用
// 如果新 AABB 仍在旧 fatAABB 内（碰撞体只移动了一小步），不需要重建树
// 否则先删除再插入（代价为 O(log n)）
inline bool DynamicAABBTree::Move(int nodeId, const AABB2D& newAABB)
{
    assert(IsLeaf(nodeId));
    // 检查新 AABB 是否仍在 fatAABB 内（避免不必要的重建）
    if (m_nodes[nodeId].fatAABB.Contains(newAABB))
        return false;  // 不需要重建

    RemoveLeaf(nodeId);
    m_nodes[nodeId].fatAABB = { newAABB.min - Vec2{FAT_MARGIN, FAT_MARGIN},
                                 newAABB.max + Vec2{FAT_MARGIN, FAT_MARGIN} };
    InsertLeaf(nodeId);
    return true;
}

// SAH 启发式最佳兄弟选择
// 目标：找到一个节点，使得将 leafAABB 插入到其旁边后，树的总 AABB 表面积增加最少
// 算法：从根节点开始贪心下降，每步选择代价更小的子节点
// 代价 = 直接代价（合并AABB面积） + 继承代价（祖先节点的额外扩大）
inline int DynamicAABBTree::FindBestSibling(const AABB2D& leafAABB) const
{
    // SAH 贪心下降：每步选择插入代价最小的子树
    // 插入代价 = 直接代价 + 祖先代价（inheritanceCost）
    int   bestNode = m_root;
    float bestCost = AABB2D::Merge(m_nodes[m_root].fatAABB, leafAABB).Area();

    int   node          = m_root;
    float inheritanceCost = 0.0f;  // 沿路径向上传递的额外代价

    while (!m_nodes[node].IsLeaf())
    {
        float combinedArea    = AABB2D::Merge(m_nodes[node].fatAABB, leafAABB).Area();
        float directCost      = combinedArea;
        float totalCostHere   = directCost + inheritanceCost;

        // 更新最优节点
        if (totalCostHere < bestCost)
        {
            bestCost = totalCostHere;
            bestNode = node;
        }

        // 当前节点扩大的代价（传递给子节点）
        float enlargement = combinedArea - m_nodes[node].fatAABB.Area();
        inheritanceCost  += enlargement;

        int   c1 = m_nodes[node].child1;
        int   c2 = m_nodes[node].child2;

        // 估算插入到 c1 的最低可能代价（下界）
        // 剩枝优化：如果子树的下界已经超过当前最优，无需继续探索
        float lowerBound1 = leafAABB.Area() + inheritanceCost;
        if (lowerBound1 >= bestCost)
        {
            // 剪枝：整棵子树都不可能更优
            break;
        }

        // 估算插入到左子节点(c1)和右子节点(c2)的直接代价
        float cost1 = AABB2D::Merge(m_nodes[c1].fatAABB, leafAABB).Area() + inheritanceCost;
        float cost2 = AABB2D::Merge(m_nodes[c2].fatAABB, leafAABB).Area() + inheritanceCost;

        // 下降到代价更小的子节点（贪心策略）
        if (cost1 < cost2)
            node = c1;
        else
            node = c2;
    }

    // 最后检查叶节点（可能直接插入到叶节点旁边更优）
    float finalCost = AABB2D::Merge(m_nodes[node].fatAABB, leafAABB).Area() + inheritanceCost;
    if (finalCost < bestCost)
        bestNode = node;

    return bestNode;
}


// 插入叶节点到树中
// 流程：
//   1. 如果树为空，直接作为根节点
//   2. 找到最佳兄弟节点（SAH）
//   3. 创建新的内部节点，将兄弟和新叶节点作为其两个子节点
//   4. 向上修复 AABB 和高度（并做平衡旋转）
inline void DynamicAABBTree::InsertLeaf(int leafId)
{
    if (m_root == NULL_NODE)
    {
        m_root = leafId;
        m_nodes[m_root].parent = NULL_NODE;
        return;
    }

    // 找最佳兄弟（SAH 启发式选择）
    int sibling = FindBestSibling(m_nodes[leafId].fatAABB);

    // 创建新内部节点，作为 sibling 和 leafId 的共同父节点
    //   修改前：  oldParent -> sibling
    //   修改后：  oldParent -> newParent -> { sibling, leafId }
    int oldParent = m_nodes[sibling].parent;
    int newParent = AllocNode();
    m_nodes[newParent].parent  = oldParent;
    m_nodes[newParent].fatAABB = AABB2D::Merge(m_nodes[leafId].fatAABB, m_nodes[sibling].fatAABB);
    m_nodes[newParent].height  = m_nodes[sibling].height + 1;

    if (oldParent != NULL_NODE)
    {
        if (m_nodes[oldParent].child1 == sibling)
            m_nodes[oldParent].child1 = newParent;
        else
            m_nodes[oldParent].child2 = newParent;
    }
    else
    {
        m_root = newParent;
    }

    m_nodes[newParent].child1 = sibling;
    m_nodes[newParent].child2 = leafId;
    m_nodes[sibling].parent   = newParent;
    m_nodes[leafId].parent    = newParent;

    FixUpwards(newParent);
}

// 从树中删除叶节点
// 流程：
//   1. 如果是根节点，直接清空树
//   2. 否则用兄弟节点替代父节点的位置（父节点变得多余，回收）
//   3. 向上修复
inline void DynamicAABBTree::RemoveLeaf(int leafId)
{
    if (leafId == m_root)
    {
        m_root = NULL_NODE;
        return;
    }

    int parent      = m_nodes[leafId].parent;
    int grandParent = m_nodes[parent].parent;
    int sibling     = (m_nodes[parent].child1 == leafId)
                      ? m_nodes[parent].child2
                      : m_nodes[parent].child1;

    if (grandParent != NULL_NODE)
    {
        if (m_nodes[grandParent].child1 == parent)
            m_nodes[grandParent].child1 = sibling;
        else
            m_nodes[grandParent].child2 = sibling;

        m_nodes[sibling].parent = grandParent;
        FreeNode(parent);
        FixUpwards(grandParent);
    }
    else
    {
        m_root = sibling;
        m_nodes[sibling].parent = NULL_NODE;
        FreeNode(parent);
    }
}

// 向上修复：从指定节点向根节点遍历，更新每个节点的 AABB 和高度
// 每个节点先做旋转平衡，再更新属性
inline void DynamicAABBTree::FixUpwards(int nodeId)
{
    while (nodeId != NULL_NODE)
    {
        nodeId = Rotate(nodeId);

        int c1 = m_nodes[nodeId].child1;
        int c2 = m_nodes[nodeId].child2;

        m_nodes[nodeId].height  = 1 + std::max(m_nodes[c1].height, m_nodes[c2].height);
        m_nodes[nodeId].fatAABB = AABB2D::Merge(m_nodes[c1].fatAABB, m_nodes[c2].fatAABB);

        nodeId = m_nodes[nodeId].parent;
    }
}

// AVL 风格旋转：保持树高度平衡
// 左右子树高度差超过 1 时触发旋转：
//   balance > 1  → 右旋（左子树过高）
//   balance < -1 → 左旋（右子树过高）
// 旋转后返回新的子树根节点
inline int DynamicAABBTree::Rotate(int nodeId)
{
    // 叶节点或高度 < 2 不需要旋转
    if (m_nodes[nodeId].IsLeaf() || m_nodes[nodeId].height < 2)
        return nodeId;

    int c1 = m_nodes[nodeId].child1;
    int c2 = m_nodes[nodeId].child2;

    int balance = m_nodes[c1].height - m_nodes[c2].height;  // 平衡因子

    // 右旋：c1 子树过高
    // 操作：将 c1 提升为新的子树根，nodeId 下移为 c1 的子节点
    // 修复：原实现中先设置 c1.child2=nodeId，随后又用 c1c1/c1c2 覆盖了 c1.child2，
    //       导致 nodeId 变成孤儿节点。现在统一在分支内正确设置 c1.child1 和 c1.child2
    if (balance > 1)
    {
        int c1c1 = m_nodes[c1].child1;
        int c1c2 = m_nodes[c1].child2;

        // 将 c1 提升到 nodeId 的位置
        m_nodes[c1].parent     = m_nodes[nodeId].parent;
        m_nodes[nodeId].parent = c1;

        if (m_nodes[c1].parent != NULL_NODE)
        {
            if (m_nodes[m_nodes[c1].parent].child1 == nodeId)
                m_nodes[m_nodes[c1].parent].child1 = c1;
            else
                m_nodes[m_nodes[c1].parent].child2 = c1;
        }
        else
        {
            m_root = c1;
        }

        // 根据子树高度决定哪个子树留在 c1 下、哪个下放到 nodeId
        // 目标：更高的子树留在 c1 旁（减少树高），更矮的下放到 nodeId
        if (m_nodes[c1c1].height > m_nodes[c1c2].height)
        {
            // c1c1 更高 → c1c1 留在 c1 左子，c1c2 下放到 nodeId 左子
            // 结构：    c1
            //          /    \
            //       c1c1   nodeId
            //              /    \
            //           c1c2    c2
            m_nodes[c1].child1     = c1c1;     // 保持不变（c1c1 已经是 c1 的左子）
            m_nodes[c1].child2     = nodeId;   // c1 的右子 = nodeId
            m_nodes[nodeId].child1 = c1c2;     // nodeId 的左子 = 更矮的 c1c2
            m_nodes[c1c2].parent   = nodeId;

            m_nodes[nodeId].fatAABB = AABB2D::Merge(m_nodes[c2].fatAABB, m_nodes[c1c2].fatAABB);
            m_nodes[c1].fatAABB     = AABB2D::Merge(m_nodes[nodeId].fatAABB, m_nodes[c1c1].fatAABB);
            m_nodes[nodeId].height  = 1 + std::max(m_nodes[c2].height, m_nodes[c1c2].height);
            m_nodes[c1].height      = 1 + std::max(m_nodes[nodeId].height, m_nodes[c1c1].height);
        }
        else
        {
            // c1c2 更高（或等高）→ c1c2 留在 c1 右子，c1c1 下放到 nodeId 左子
            // 结构：    c1
            //          /    \
            //       nodeId  c1c2
            //       /    \
            //     c1c1    c2
            m_nodes[c1].child1     = nodeId;   // c1 的左子 = nodeId
            m_nodes[c1].child2     = c1c2;     // c1 的右子 = 更高的 c1c2
            m_nodes[nodeId].child1 = c1c1;     // nodeId 的左子 = 更矮的 c1c1
            m_nodes[c1c1].parent   = nodeId;

            m_nodes[nodeId].fatAABB = AABB2D::Merge(m_nodes[c2].fatAABB, m_nodes[c1c1].fatAABB);
            m_nodes[c1].fatAABB     = AABB2D::Merge(m_nodes[nodeId].fatAABB, m_nodes[c1c2].fatAABB);
            m_nodes[nodeId].height  = 1 + std::max(m_nodes[c2].height, m_nodes[c1c1].height);
            m_nodes[c1].height      = 1 + std::max(m_nodes[nodeId].height, m_nodes[c1c2].height);
        }
        return c1;
    }

    // 左旋：c2 子树过高（与右旋对称的操作）
    // 修复：同样统一在分支内正确设置 child1 和 child2
    if (balance < -1)
    {
        int c2c1 = m_nodes[c2].child1;
        int c2c2 = m_nodes[c2].child2;

        m_nodes[c2].parent     = m_nodes[nodeId].parent;
        m_nodes[nodeId].parent = c2;

        if (m_nodes[c2].parent != NULL_NODE)
        {
            if (m_nodes[m_nodes[c2].parent].child1 == nodeId)
                m_nodes[m_nodes[c2].parent].child1 = c2;
            else
                m_nodes[m_nodes[c2].parent].child2 = c2;
        }
        else
        {
            m_root = c2;
        }

        if (m_nodes[c2c1].height > m_nodes[c2c2].height)
        {
            // c2c1 更高 → c2c1 留在 c2 右子，c2c2 下放到 nodeId 右子
            // 结构：       c2
            //             /    \
            //          nodeId  c2c1
            //          /    \
            //        c1    c2c2
            m_nodes[c2].child1      = nodeId;
            m_nodes[c2].child2      = c2c1;
            m_nodes[nodeId].child2  = c2c2;
            m_nodes[c2c2].parent    = nodeId;

            m_nodes[nodeId].fatAABB = AABB2D::Merge(m_nodes[c1].fatAABB, m_nodes[c2c2].fatAABB);
            m_nodes[c2].fatAABB     = AABB2D::Merge(m_nodes[nodeId].fatAABB, m_nodes[c2c1].fatAABB);
            m_nodes[nodeId].height  = 1 + std::max(m_nodes[c1].height, m_nodes[c2c2].height);
            m_nodes[c2].height      = 1 + std::max(m_nodes[nodeId].height, m_nodes[c2c1].height);
        }
        else
        {
            // c2c2 更高（或等高）→ c2c2 留在 c2 右子，c2c1 下放到 nodeId 右子
            // 结构：       c2
            //             /    \
            //          nodeId  c2c2
            //          /    \
            //        c1    c2c1
            m_nodes[c2].child1      = nodeId;
            m_nodes[c2].child2      = c2c2;
            m_nodes[nodeId].child2  = c2c1;
            m_nodes[c2c1].parent    = nodeId;

            m_nodes[nodeId].fatAABB = AABB2D::Merge(m_nodes[c1].fatAABB, m_nodes[c2c1].fatAABB);
            m_nodes[c2].fatAABB     = AABB2D::Merge(m_nodes[nodeId].fatAABB, m_nodes[c2c2].fatAABB);
            m_nodes[nodeId].height  = 1 + std::max(m_nodes[c1].height, m_nodes[c2c1].height);
            m_nodes[c2].height      = 1 + std::max(m_nodes[nodeId].height, m_nodes[c2c2].height);
        }
        return c2;
    }

    return nodeId;
}

// 查询：找出所有与给定 AABB 重叠的叶节点
// 用栈代替递归（避免栈溢出，也更快）
// 剪枝策略：如果当前节点的 fatAABB 不与查询区域重叠，跳过整棵子树
inline void DynamicAABBTree::Query(const AABB2D& aabb,
                                    const std::function<bool(int)>& callback) const
{
    if (m_root == NULL_NODE) return;

    // 用栈模拟树的深度优先遍历（DFS）
    std::vector<int> stack;
    stack.reserve(32);  // 预分配避免频繁扩容
    stack.push_back(m_root);

    while (!stack.empty())
    {
        int nodeId = stack.back();
        stack.pop_back();

        // 剪枝：当前节点的 AABB 不与查询区域重叠，跳过整棵子树
        if (!m_nodes[nodeId].fatAABB.Overlaps(aabb)) continue;

        if (m_nodes[nodeId].IsLeaf())
        {
            // 叶节点：通过回调返回结果，回调返回 false 则提前终止
            if (!callback(nodeId)) return;
        }
        else
        {
            // 内部节点：将两个子节点压入栈继续探索
            stack.push_back(m_nodes[nodeId].child1);
            stack.push_back(m_nodes[nodeId].child2);
        }
    }
}

// 遍历所有重叠的叶节点对（宽相检测的主入口）
// 修复：对每个叶节点调用 Query() 利用树结构剪枝，平均复杂度 O(n log n)
// 通过 nodeIdA < nodeIdB 避免 (A,B) 和 (B,A) 重复
inline void DynamicAABBTree::QueryAllPairs(
    const std::function<bool(int, int)>& callback) const
{
    if (m_root == NULL_NODE || m_leafCount < 2) return;

    // 收集所有叶节点
    std::vector<int> leaves;
    leaves.reserve(m_leafCount);

    std::vector<int> collectStack;
    collectStack.reserve(32);
    collectStack.push_back(m_root);

    while (!collectStack.empty())
    {
        int nodeId = collectStack.back();
        collectStack.pop_back();
        if (m_nodes[nodeId].IsLeaf())
        {
            leaves.push_back(nodeId);
        }
        else
        {
            collectStack.push_back(m_nodes[nodeId].child1);
            collectStack.push_back(m_nodes[nodeId].child2);
        }
    }

    // 对每个叶节点调用 Query()，利用树结构剪枝
    // 只处理 leafA < leafB 的情况，避免 (A,B)/(B,A) 重复
    bool shouldContinue = true;
    for (int leafA : leaves)
    {
        if (!shouldContinue) break;

        const AABB2D& aabbA = m_nodes[leafA].fatAABB;

        Query(aabbA, [&](int leafB) -> bool
        {
            // 跳过自身和已检测的对（只处理 leafA < leafB）
            if (leafB <= leafA) return true;

            if (!callback(leafA, leafB))
            {
                shouldContinue = false;
                return false;
            }
            return true;
        });
    }
}