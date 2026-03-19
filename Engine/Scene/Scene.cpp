#include "Scene.h"
#include "CollisionSystem.h"
#include "ColliderComponent.h"
#include <iostream>
#include <algorithm>
#include <unordered_set>


// ============================================================
//  构造 / 析构
//  Scene 拥有 CollisionSystem 的所有权（unique_ptr），
//  在构造时立即创建碰撞系统，确保 FixedUpdate 中可以使用
// ============================================================
Scene::Scene(std::string name)
    : m_name(std::move(name))
    , m_collisionSystem(std::make_unique<CollisionSystem>())
{}

// 析构时先通知碰撞系统清理所有碰撞对，再停止所有对象
// 修复 #7：场景卸载时如果不逐个清理碰撞对，m_previousPairs 会残留无效引用
// 这确保了所有 OnCollisionExit 回调被正确触发
Scene::~Scene()
{
    // 先清理碰撞系统中涉及本场景所有对象的碰撞对
    if (m_collisionSystem)
    {
        for (auto& [id, obj] : m_objects)
            m_collisionSystem->NotifyObjectDestroyed(*this, id);
    }

    for (auto& [id, obj] : m_objects)
        obj->Stop();
    m_objects.clear();
    // unique_ptr 自动析构 CollisionSystem
}

// ============================================================
//  场景生命周期
//  生命周期调用链：Start → (Update + FixedUpdate + Render 循环) → Stop
//  所有遍历均使用快照（IterateWithSnapshot），确保回调中
//  创建/销毁对象不会破坏迭代器
// ============================================================
void Scene::Start()
{
    m_running = true;
    // 使用快照遍历，确保 Start 期间创建的新对象不会重复触发 Start
    // （新对象在 FlushPendingOperations 中才加入主列表，此时才会触发 Start）
    IterateWithSnapshot([&](GameObjectID id)
    {
        if (auto it = m_objects.find(id); it != m_objects.end())
            it->second->Start();
    });
}


void Scene::Update(float deltaTime)
{
    if (!m_running) return;
    // 逐对象调用 Update：每个 GameObject 内部再逐组件调用 OnUpdate
    // 遍历顺序由 m_creationOrder 决定，保证确定性
    IterateWithSnapshot([&](GameObjectID id)
    {
        if (auto it = m_objects.find(id); it != m_objects.end())
            it->second->Update(deltaTime);
    });
}

void Scene::FixedUpdate(float fixedDeltaTime)
{
    if (!m_running) return;

    // 1. 物理组件更新（积分速度、位置）
    //    PhysicsComponent::OnFixedUpdate 内部会：
    //    施加重力 → 速度积分 → 阻尼衰减 → 更新 Transform 位置
    IterateWithSnapshot([&](GameObjectID id)
    {
        if (auto it = m_objects.find(id); it != m_objects.end())
            it->second->FixedUpdate(fixedDeltaTime);
    });

    // 2. 碰撞检测（在物理积分之后执行）
    //    流程：收集碰撞体 → 宽相AABB排除 → 窄相精确检测 → 分发Enter/Stay/Exit事件
    if (m_collisionSystem)
        m_collisionSystem->Update(*this);
}

void Scene::Render()
{
    if (!m_running) return;
    // 遍历所有对象，调用 Render（实际绘制由 RenderComponent::OnRender 完成）
    // 注意：当前按创建顺序遍历，正式引擎应按 SortingLayer 排序后再绘制
    IterateWithSnapshot([&](GameObjectID id)
    {
        if (auto it = m_objects.find(id); it != m_objects.end())
            it->second->Render();
    });
}

void Scene::Stop()
{
    m_running = false;
    // 修复：Stop 也使用快照遍历，因为 OnStop() 回调中可能销毁其他对象
    //       直接遍历 m_objects 时如果 OnStop 触发了 DestroyGameObject，
    //       会导致 m_objects 迭代器失效（未定义行为）
    IterateWithSnapshot([&](GameObjectID id)
    {
        if (auto it = m_objects.find(id); it != m_objects.end())
            it->second->Stop();
    });
}

// 获取碰撞系统的引用，供外部配置（如设置碰撞矩阵）
CollisionSystem& Scene::GetCollisionSystem()
{
    return *m_collisionSystem;
}

// ============================================================
//  GameObject 管理
// ============================================================

// 创建一个新的 GameObject 并加入场景
// 如果当前正在遍历（m_isIterating），新对象会暂存到 m_pendingAdd，
// 遍历结束后才真正加入 m_objects（避免遍历期间修改容器）
// ?? 注意：遍历期间创建的对象，虽然返回了有效 ID，
//    但 GetGameObject(id) 在遍历结束前会返回 nullptr
GameObjectID Scene::CreateGameObject(const std::string& name)
{
    // 生成全局唯一ID（原子操作，线程安全）
    GameObjectID id = GameObjectIDGenerator::Generate();
    auto obj = std::make_unique<GameObject>(id, name, this);

    if (m_iterateDepth > 0)
    {
        // 遍历中：暂存到待添加列表，延迟到 FlushPendingOperations 再处理
        m_pendingAdd.emplace_back(id, std::move(obj));
    }
    else
    {
        // 非遍历中：直接加入主容器
        m_objects[id] = std::move(obj);
        m_creationOrder.push_back(id);

        // 如果场景已经启动，新对象需要立即调用 Start
        if (m_running)
            m_objects[id]->Start();
    }
    return id;
}

// 通过 ID 获取 GameObject 指针（O(1) 哈希查找）
// 返回 nullptr 表示 ID 无效或对象不存在
// 修复：同时搜索 m_pendingAdd 列表，确保遍历期间创建的对象也能被立即访问
GameObject* Scene::GetGameObject(GameObjectID id) const
{
    auto it = m_objects.find(id);
    if (it != m_objects.end())
        return it->second.get();

    // 在待添加列表中搜索（遍历期间新创建的对象暂存在此处）
    for (const auto& [pendingID, pendingObj] : m_pendingAdd)
    {
        if (pendingID == id)
            return pendingObj.get();
    }
    return nullptr;
}

// 按名称查找（线性遍历 O(n)，适用于编辑器/调试，不建议每帧调用）
GameObject* Scene::FindGameObjectByName(const std::string& name) const
{
    for (auto& [id, obj] : m_objects)
    {
        if (obj->GetName() == name)
            return obj.get();
    }
    return nullptr;
}

// ============================================================
//  对象迁移 API（用于 DontDestroyOnLoad 跨场景迁移）
//  参考 Unity 内部的 MoveGameObjectToScene 实现
//
//  DetachGameObject：将对象从场景中移出，所有权转移给调用者
//    - 先解除父子关系（成为根对象）
//    - 从 m_objects 和 m_creationOrder 中移除
//    - 不调用 Stop（对象仍然"活着"，只是换了场景）
//
//  AttachGameObject：将外部对象移入场景，场景获取所有权
//    - 更新对象的场景引用指针
//    - 加入 m_objects 和 m_creationOrder
//    - 如果场景已运行且对象尚未 Start，则调用 Start
// ============================================================
std::unique_ptr<GameObject> Scene::DetachGameObject(GameObjectID id)
{
    auto it = m_objects.find(id);
    if (it == m_objects.end())
    {
        std::cerr << "[Scene] DetachGameObject: 未找到 ID=" << id << "\n";
        return nullptr;
    }

    // 不允许在遍历期间执行迁移（涉及容器修改）
    if (m_iterateDepth > 0)
    {
        std::cerr << "[Scene] DetachGameObject: 不能在遍历期间迁移对象\n";
        return nullptr;
    }

    GameObject* obj = it->second.get();

    // 解除父子关系：从父对象的子列表中移除自己
    if (obj->GetParentID() != INVALID_GAME_OBJECT_ID)
    {
        if (GameObject* parent = GetGameObject(obj->GetParentID()))
            parent->RemoveChildID(id);
        obj->SetParentID(INVALID_GAME_OBJECT_ID);
    }

    // 解除子对象的父引用（子对象留在源场景成为根对象）
    // 注意：DontDestroyOnLoad 只迁移对象本身，不递归迁移子对象
    // 如果需要连同子对象一起迁移，应该由调用者显式处理
    // 先拷贝子列表（因为后续会清空）
    std::vector<GameObjectID> childrenCopy = obj->GetChildrenIDs();
    for (GameObjectID childID : childrenCopy)
    {
        if (GameObject* child = GetGameObject(childID))
            child->SetParentID(INVALID_GAME_OBJECT_ID);
    }
    // 直接清空子列表（O(1) 操作，替代之前的 O(n2) while 循环）
    // 子对象仍在源场景中，只是不再与被迁移的对象有父子关系
    obj->ClearChildrenIDs();

    // 从 m_objects 中取出 unique_ptr（所有权转移）
    std::unique_ptr<GameObject> detached = std::move(it->second);
    m_objects.erase(it);

    // 从创建顺序中移除
    auto orderIt = std::find(m_creationOrder.begin(), m_creationOrder.end(), id);
    if (orderIt != m_creationOrder.end())
        m_creationOrder.erase(orderIt);

    std::cout << "[Scene] 已从场景 '" << m_name << "' 分离对象: '"
              << detached->GetName() << "' (ID=" << id << ")\n";

    return detached;
}

bool Scene::AttachGameObject(std::unique_ptr<GameObject> obj)
{
    if (!obj)
    {
        std::cerr << "[Scene] AttachGameObject: 对象为空\n";
        return false;
    }

    // 不允许在遍历期间执行迁移
    if (m_iterateDepth > 0)
    {
        std::cerr << "[Scene] AttachGameObject: 不能在遍历期间迁移对象\n";
        return false;
    }

    GameObjectID id = obj->GetID();

    // 检查 ID 是否已存在
    if (m_objects.count(id))
    {
        std::cerr << "[Scene] AttachGameObject: ID=" << id << " 已存在\n";
        return false;
    }

    // 更新对象的场景引用，指向当前场景
    // 注意：m_scene 是 private 成员，我们通过一个新的公共方法来设置
    obj->SetScene(this);

    std::string objName = obj->GetName();

    // 加入主容器
    m_objects[id] = std::move(obj);
    m_creationOrder.push_back(id);

    // 如果场景已运行且对象尚未 Start，则追赶启动
    // 注意：通过 DontDestroyOnLoad 迁移的对象 HasStarted()==true，
    //       不会重复 Start——这是预期行为，对象的组件状态完整保留
    // 此分支主要服务于：在运行中的场景新创建并附加的对象
    if (m_running && !m_objects[id]->HasStarted())
    {
        m_objects[id]->Start();
    }

    std::cout << "[Scene] 已将对象 '" << objName << "' (ID=" << id
              << ") 附加到场景 '" << m_name << "'\n";

    return true;
}

// ============================================================
//  基于 GameplayTag 的查询系列方法
//  参考 UE 的 FGameplayTagContainer::HasTag / HasAll / HasAny 设计
//  所有查询均为 O(n) 遍历 × O(1) 哈希匹配
// ============================================================

// 层级匹配查找：查找拥有匹配指定 tag 的标签的所有对象
// 例如查找 "Enemy" 会匹配拥有 "Enemy.Boss" 或 "Enemy.Minion" 的对象
// 修复 Bug#3：添加遍历保护，避免回调中修改对象容器导致迭代器失效
std::vector<GameObject*> Scene::FindGameObjectsByTag(const GameplayTag& tag) const
{
    std::vector<GameObject*> result;
    ++m_iterateDepth;
    for (auto& [id, obj] : m_objects)
    {
        if (IsPendingDestroy(id)) continue;
        if (obj->GetTags().HasTag(tag))
            result.push_back(obj.get());
    }
    --m_iterateDepth;
    if (m_iterateDepth == 0)
        const_cast<Scene*>(this)->FlushPendingOperations();
    return result;
}

// 精确匹配查找：仅查找拥有该精确标签的对象
// 修复 Bug#3：同上
std::vector<GameObject*> Scene::FindGameObjectsByTagExact(const GameplayTag& tag) const
{
    std::vector<GameObject*> result;
    ++m_iterateDepth;
    for (auto& [id, obj] : m_objects)
    {
        if (IsPendingDestroy(id)) continue;
        if (obj->GetTags().HasTagExact(tag))
            result.push_back(obj.get());
    }
    --m_iterateDepth;
    if (m_iterateDepth == 0)
        const_cast<Scene*>(this)->FlushPendingOperations();
    return result;
}

// 查找同时拥有容器中所有标签的对象（层级匹配）
// 修复 Bug#3：同上
std::vector<GameObject*> Scene::FindGameObjectsWithAllTags(const GameplayTagContainer& tags) const
{
    std::vector<GameObject*> result;
    ++m_iterateDepth;
    for (auto& [id, obj] : m_objects)
    {
        if (IsPendingDestroy(id)) continue;
        if (obj->GetTags().HasAll(tags))
            result.push_back(obj.get());
    }
    --m_iterateDepth;
    if (m_iterateDepth == 0)
        const_cast<Scene*>(this)->FlushPendingOperations();
    return result;
}

// 查找拥有容器中任一标签的对象（层级匹配）
// 修复 Bug#3：同上
std::vector<GameObject*> Scene::FindGameObjectsWithAnyTags(const GameplayTagContainer& tags) const
{
    std::vector<GameObject*> result;
    ++m_iterateDepth;
    for (auto& [id, obj] : m_objects)
    {
        if (IsPendingDestroy(id)) continue;
        if (obj->GetTags().HasAny(tags))
            result.push_back(obj.get());
    }
    --m_iterateDepth;
    if (m_iterateDepth == 0)
        const_cast<Scene*>(this)->FlushPendingOperations();
    return result;
}

// 销毁一个 GameObject
// 如果正在遍历：延迟到遍历结束后销毁（标记到 m_pendingDestroy）
// 如果不在遍历：立即销毁（递归销毁所有子对象）
bool Scene::DestroyGameObject(GameObjectID id)
{
    if (m_objects.find(id) == m_objects.end())
        return false;

    if (m_iterateDepth > 0)
    {
        // 遍历中：只做标记，不立即销毁
        m_pendingDestroy.push_back(id);
        m_pendingDestroySet.insert(id);  // 同步更新 set，保证 O(1) 查找
        return true;
    }

    return DestroyGameObjectImmediate(id);
}

// ============================================================
//  父子关系管理
//  场景图（Scene Graph）的核心功能：
//  - SetParent：建立父子关系（含循环引用检测）
//  - UnsetParent：解除父子关系
//  - IsAncestor：祖先链查找（用于循环检测）
// ============================================================

// 将 childID 设为 parentID 的子节点
// 检查项：自身引用、循环引用、旧父节点解绑
bool Scene::SetParent(GameObjectID childID, GameObjectID parentID)
{
    GameObject* child  = GetGameObject(childID);
    GameObject* parent = GetGameObject(parentID);

    if (!child || !parent)
        return false;

    // 不能将自身设为自己的父节点
    if (childID == parentID)
    {
        std::cerr << "[Scene] 错误: 不能将对象设为自己的父节点\n";
        return false;
    }

    // 循环引用检测：检查 childID 是否是 parentID 的祖先
    // 如果 childID 已经是 parentID 的祖先（即 parentID → ... → childID 的链条已经存在），
    // 再让 childID 成为 parentID 的子节点就会形成环
    // 修复：原来参数顺序反转了，正确应该是 IsAncestor(childID, parentID)
    if (IsAncestor(childID, parentID))
    {
        std::cerr << "[Scene] 错误: 设置父子关系会造成循环引用\n";
        return false;
    }

    // 如果 child 已有旧的父节点，先从旧父节点的子列表中移除
    if (child->GetParentID() != INVALID_GAME_OBJECT_ID)
    {
        if (GameObject* oldParent = GetGameObject(child->GetParentID()))
            oldParent->RemoveChildID(childID);
    }

    child->SetParentID(parentID);
    parent->AddChildID(childID);
    return true;
}

// 解除 childID 的父子关系，使其成为根对象
bool Scene::UnsetParent(GameObjectID childID)
{
    GameObject* child = GetGameObject(childID);
    if (!child) return false;

    // 有父节点才需要解除
    if (child->GetParentID() != INVALID_GAME_OBJECT_ID)
    {
        // 从父节点的子列表中移除自己
        if (GameObject* parent = GetGameObject(child->GetParentID()))
            parent->RemoveChildID(childID);
        // 重置自己的父ID为无效值（成为根对象）
        child->SetParentID(INVALID_GAME_OBJECT_ID);
    }
    return true;
}

// 判断 ancestorID 是否是 nodeID 的祖先
// 实现：从 nodeID 的父节点开始，沿父链向上遍历，
//       如果途中遇到 ancestorID 则返回 true
// 注意：不把节点视为自身的祖先（从父节点开始遍历，不包含自身）
bool Scene::IsAncestor(GameObjectID ancestorID, GameObjectID nodeID) const
{
    // 从 nodeID 的父节点开始向上遍历，不把自身视为自己的祖先
    GameObject* node = GetGameObject(nodeID);
    if (!node) return false;

    // 沿着父链向上爬，直到到达根节点（parentID == INVALID）或找到目标
    GameObjectID current = node->GetParentID();
    while (current != INVALID_GAME_OBJECT_ID)
    {
        if (current == ancestorID) return true;  // 找到了，ancestorID 确实是祖先
        GameObject* obj = GetGameObject(current);
        if (!obj) break;  // 父节点不存在（数据不一致），中断
        current = obj->GetParentID();
    }
    return false;  // 遍历到根都没找到，不是祖先
}


// 获取所有根对象（没有父节点的对象）
// 用于构建场景图的顶层，也可用于编辑器的层级面板显示
std::vector<GameObject*> Scene::GetRootObjects() const
{
    std::vector<GameObject*> roots;
    for (auto& [id, obj] : m_objects)
    {
        if (obj->GetParentID() == INVALID_GAME_OBJECT_ID)
            roots.push_back(obj.get());
    }
    return roots;
}

// ============================================================
//  序列化
//  将整个场景导出为 JSON 字符串（包含所有对象及其组件）
//  按 m_creationOrder 顺序输出，保证反序列化时能还原创建顺序
// ============================================================
std::string Scene::Serialize() const
{
    std::string result = "{\n";
    result += "  \"scene\": \"" + m_name + "\",\n";
    result += "  \"objectCount\": " + std::to_string(m_objects.size()) + ",\n";
    result += "  \"gameObjects\": [\n";

    bool first = true;
    for (auto id : m_creationOrder)
    {
        auto it = m_objects.find(id);
        if (it != m_objects.end())
        {
            if (!first) result += ",\n";
            std::string objStr = it->second->Serialize();
            result += "    " + objStr;
            first = false;
        }
    }
    result += "\n  ]\n}";
    return result;
}

// ============================================================
//  调试信息
//  输出场景概况 + 层级树结构（缩进表示父子关系）
// ============================================================
void Scene::PrintSceneInfo() const
{
    std::cout << "[Scene] 名称='" << m_name << "'"
              << " 对象数=" << m_objects.size()
              << " 运行中=" << (m_running ? "是" : "否") << "\n";

    std::cout << "  场景图层级:\n";
    PrintHierarchy(INVALID_GAME_OBJECT_ID, 2);
}

// ============================================================
//  内部工具
// ============================================================

// 检查某个对象是否在本帧遍历期间被标记为"待销毁"
// 快照遍历时会调用此函数跳过已标记的对象
// 修复：使用 unordered_set 替代线性搜索，提升查找效率 O(n) → O(1)
bool Scene::IsPendingDestroy(GameObjectID id) const
{
    return m_pendingDestroySet.count(id) > 0;
}

// 将遍历期间缓冲的增删操作一次性提交
// 执行顺序：先销毁后添加（防止 ID 冲突，确保被销毁的对象不会"复活"）
void Scene::FlushPendingOperations()
{
    // 使用 swap 技巧获取待处理列表的副本并清空原列表
    // 这样即使处理过程中再次触发延迟操作，也不会影响当前批次
    std::vector<GameObjectID> toDestroy;
    toDestroy.swap(m_pendingDestroy);
    m_pendingDestroySet.clear();  // 同步清空 set
    // 逐个执行立即销毁（会递归销毁子对象）
    for (GameObjectID id : toDestroy)
        DestroyGameObjectImmediate(id);

    std::vector<std::pair<GameObjectID, std::unique_ptr<GameObject>>> toAdd;
    toAdd.swap(m_pendingAdd);
    for (auto& [id, obj] : toAdd)
    {
        // 将延迟创建的对象加入主容器
        m_objects[id] = std::move(obj);
        m_creationOrder.push_back(id);

        // 场景运行中：新对象需要立即调用 Start（追赶场景进度）
        if (m_running)
            m_objects[id]->Start();
    }
}

// 立即销毁一个 GameObject（递归销毁所有子对象）
// 修复 Bug#4：销毁父对象时自动递归销毁所有子对象，
//   并在销毁前正确断开子对象的父引用，避免孤儿对象指向无效父 ID
// 步骤：
//   1. 从父对象的子列表中移除自己
//   2. 递归销毁所有子对象（深度优先，先断开父引用再销毁）
//   3. 清理 pending 列表中的残留引用
//   4. 调用 Stop 通知所有组件
//   5. 从 m_objects 和 m_creationOrder 中移除
bool Scene::DestroyGameObjectImmediate(GameObjectID id)
{
    auto it = m_objects.find(id);
    if (it == m_objects.end())
        return false;

    GameObject* obj = it->second.get();

    // 1. 如果有父对象，从父对象的子列表中移除
    if (obj->GetParentID() != INVALID_GAME_OBJECT_ID)
    {
        if (GameObject* parent = GetGameObject(obj->GetParentID()))
            parent->RemoveChildID(id);
    }

    // 2. 通知碰撞系统清理涉及此对象的碰撞对，并触发 Exit 事件
    //    必须在 Stop() 之前，这样 Exit 回调中对象状态仍然完整
    if (m_collisionSystem)
        m_collisionSystem->NotifyObjectDestroyed(*this, id);

    // 3. 修复 Bug#4：递归销毁所有子对象（先复制子列表，因为递归中会修改原列表）
    //    参考 UE AActor::Destroy() 自动销毁所有附加的子 Actor
    //    先断开子对象的父引用，再递归销毁，避免子对象销毁时再次操作父的子列表
    std::vector<GameObjectID> children = obj->GetChildrenIDs();
    obj->ClearChildrenIDs();  // 清空父对象的子列表
    for (GameObjectID childID : children)
    {
        // 断开子对象的父引用（这样 DestroyGameObjectImmediate 不会再次操作父的子列表）
        if (GameObject* child = GetGameObject(childID))
            child->SetParentID(INVALID_GAME_OBJECT_ID);
        DestroyGameObjectImmediate(childID);
    }

    // 4. 子对象全部销毁后，再通知父对象停止
    obj->Stop();

    // 5. 清理 m_pendingAdd 中对应此 ID 的条目
    m_pendingAdd.erase(
        std::remove_if(m_pendingAdd.begin(), m_pendingAdd.end(),
            [id](const std::pair<GameObjectID, std::unique_ptr<GameObject>>& p)
            {
                return p.first == id;
            }),
        m_pendingAdd.end());

    // 6. 从主容器中删除（unique_ptr 自动析构 GameObject 及其所有组件）
    m_objects.erase(it);

    // 7. 从创建顺序列表中移除
    m_creationOrder.erase(
        std::remove(m_creationOrder.begin(), m_creationOrder.end(), id),
        m_creationOrder.end());

    std::cout << "[Scene] 已销毁对象 ID=" << id << "\n";
    return true;
}

// 递归打印场景层级树
// parentID = INVALID_GAME_OBJECT_ID 表示从根节点开始
// indent 控制缩进层数，每深一层缩进+2
void Scene::PrintHierarchy(GameObjectID parentID, int indent) const
{
    std::string prefix(indent, ' ');
    for (auto id : m_creationOrder)
    {
        auto it = m_objects.find(id);
        if (it == m_objects.end()) continue;

        GameObject* obj = it->second.get();
        if (obj->GetParentID() != parentID) continue;

        std::cout << prefix << "- [" << obj->GetID() << "] "
                  << obj->GetName()
                  << " (组件数=" << obj->GetComponentCount() << ")\n";

        PrintHierarchy(id, indent + 2);
    }
}