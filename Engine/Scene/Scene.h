#pragma once
#include "CoreTypes.h"
#include "GameObject.h"
#include "GameplayTag.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <algorithm>
#include <unordered_set>
#include <iostream>

// CollisionSystem 的完整定义在 Scene.cpp 中通过 #include "CollisionSystem.h" 引入
// 这里只做前向声明，避免循环包含
class CollisionSystem;

// ============================================================
//  Scene（场景）
//  设计原则：
//    - Scene 是所有 GameObject 的唯一拥有者（unique_ptr）
//    - 外部通过 GameObjectID 引用对象（避免悬空指针）
//    - 支持父子层级（场景图）
//    - 支持按标签/层级批量查询
//    - 支持序列化（保存/加载场景状态）
// ============================================================
class Scene
{
public:
    // --------------------------------------------------------
    //  构造 / 析构
    // --------------------------------------------------------
    explicit Scene(std::string name);
    ~Scene();

    // 禁止拷贝
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // --------------------------------------------------------
    //  场景属性
    // --------------------------------------------------------
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    // --------------------------------------------------------
    //  GameObject 生命周期管理
    // --------------------------------------------------------
    GameObjectID CreateGameObject(const std::string& name = "GameObject");
    GameObject*  GetGameObject(GameObjectID id) const;
    GameObject*  FindGameObjectByName(const std::string& name) const;

    // 对象迁移 API（用于 DontDestroyOnLoad 跨场景迁移）
    // DetachGameObject：将对象从场景中移出，返回 unique_ptr（所有权转移给调用者）
    // AttachGameObject：将外部对象移入场景（获取所有权）
    // 参考 Unity 内部的 MoveGameObjectToScene 实现
    std::unique_ptr<GameObject> DetachGameObject(GameObjectID id);
    bool AttachGameObject(std::unique_ptr<GameObject> obj);

    // 基于 GameplayTag 的查询（层级匹配，例如查找 "Enemy" 会匹配所有拥有 "Enemy.Boss" 的对象）
    std::vector<GameObject*> FindGameObjectsByTag(const GameplayTag& tag) const;

    // 基于 GameplayTag 的查询（精确匹配）
    std::vector<GameObject*> FindGameObjectsByTagExact(const GameplayTag& tag) const;

    // 查找拥有容器中所有标签的对象（层级匹配）
    std::vector<GameObject*> FindGameObjectsWithAllTags(const GameplayTagContainer& tags) const;

    // 查找拥有容器中任一标签的对象（层级匹配）
    std::vector<GameObject*> FindGameObjectsWithAnyTags(const GameplayTagContainer& tags) const;

    bool         DestroyGameObject(GameObjectID id);

    bool   IsValid(GameObjectID id) const
    {
        return id != INVALID_GAME_OBJECT_ID && m_objects.count(id) > 0;
    }
    size_t GetGameObjectCount() const { return m_objects.size(); }

    // --------------------------------------------------------
    //  父子关系管理（场景图）
    // --------------------------------------------------------
    bool SetParent(GameObjectID childID, GameObjectID parentID);
    bool UnsetParent(GameObjectID childID);
    bool IsAncestor(GameObjectID ancestorID, GameObjectID nodeID) const;
    std::vector<GameObject*> GetRootObjects() const;

    // --------------------------------------------------------
    //  按组件类型批量查询（ECS 风格遍历）
    // --------------------------------------------------------

    // 遍历所有拥有组件 T 的对象
    // 修复 Bug#1：添加 m_iterateDepth 遍历保护，使用快照避免迭代器失效
    //   遍历期间调用 DestroyGameObject 会延迟到遍历结束后统一销毁
    template<typename T>
    void ForEachWithComponent(const std::function<void(GameObject*, T*)>& func) const
    {
        // 使用 m_creationOrder 的快照遍历，确保遍历中增删不破坏迭代器
        std::vector<GameObjectID> snapshot = m_creationOrder;
        ++m_iterateDepth;
        for (GameObjectID id : snapshot)
        {
            if (IsPendingDestroy(id)) continue;  // 跳过待销毁对象
            auto it = m_objects.find(id);
            if (it == m_objects.end()) continue;
            if (T* comp = it->second->GetComponent<T>())
                func(it->second.get(), comp);
        }
        --m_iterateDepth;
        if (m_iterateDepth == 0)
            FlushIfOutermostConst();
    }

    // 收集所有拥有组件 T 的对象
    // 修复 Bug#2：添加遍历保护 + 跳过待销毁对象
    template<typename T>
    std::vector<std::pair<GameObject*, T*>> GetAllWithComponent() const
    {
        std::vector<std::pair<GameObject*, T*>> result;
        ++m_iterateDepth;
        for (auto& [id, obj] : m_objects)
        {
            if (IsPendingDestroy(id)) continue;  // 跳过待销毁对象
            if (T* comp = obj->GetComponent<T>())
                result.emplace_back(obj.get(), comp);
        }
        --m_iterateDepth;
        if (m_iterateDepth == 0)
            FlushIfOutermostConst();
        return result;
    }

    // --------------------------------------------------------
    //  场景生命周期驱动
    // --------------------------------------------------------
    void Start();
    void Update(float deltaTime);
    void FixedUpdate(float fixedDeltaTime);
    void Render();
    void Stop();

    bool IsRunning() const { return m_running; }

    // 获取碰撞系统（用于外部配置）
    CollisionSystem& GetCollisionSystem();

    // --------------------------------------------------------
    //  序列化
    // --------------------------------------------------------
    std::string Serialize() const;

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    void PrintSceneInfo() const;

private:
    // --------------------------------------------------------
    //  内部工具：快照遍历 + 延迟操作
    // --------------------------------------------------------

    // 用快照遍历 m_creationOrder，遍历期间的增删操作被缓冲
    template<typename Func>
    void IterateWithSnapshot(Func&& func)
    {
        std::vector<GameObjectID> snapshot = m_creationOrder;

        // 修复 #10：使用深度计数器替代 bool 标志
        // 支持嵌套遍历（如 Update 回调中触发 Start），只在最外层归零时才 Flush
        ++m_iterateDepth;
        for (GameObjectID id : snapshot)
        {
            if (IsPendingDestroy(id)) continue;
            func(id);
        }
        --m_iterateDepth;

        // 只有最外层遍历结束后才执行 Flush，避免内层 Flush 破坏外层遍历
        if (m_iterateDepth == 0)
            FlushPendingOperations();
    }

    bool IsPendingDestroy(GameObjectID id) const;
    void FlushPendingOperations();
    // 安全的 const 版本：仅在 const 查询方法中使用
    // Scene 对象永远不会放在只读内存中（因为包含 mutable 成员），所以 const_cast 是安全的
    void FlushIfOutermostConst() const
    {
        if (m_iterateDepth == 0)
            const_cast<Scene*>(this)->FlushPendingOperations();
    }
    bool DestroyGameObjectImmediate(GameObjectID id);
    void PrintHierarchy(GameObjectID parentID, int indent) const;

private:
    // --------------------------------------------------------
    //  核心数据
    // --------------------------------------------------------
    std::string     m_name;
    bool            m_running = false;

    // 碰撞检测系统（Scene 拥有，每帧在 FixedUpdate 中驱动）
    // 使用 unique_ptr 避免循环包含（CollisionSystem.h 需要 Scene 完整定义）
    std::unique_ptr<CollisionSystem> m_collisionSystem;

    // --------------------------------------------------------
    //  遍历安全保护
    // --------------------------------------------------------
    // 修复 #10：遍历深度计数器（替代原来的 bool m_isIterating）
    // 支持嵌套遍历，只在最外层结束时才 FlushPendingOperations
    // mutable：因为 ForEachWithComponent/GetAllWithComponent 是 const 方法
    //   但需要修改遍历深度计数器来保证遍历安全性
    mutable int m_iterateDepth = 0;

    // 遍历期间新增的对象（下一帧生效）
    // 修复 Bug#4：声明为 mutable，因为 const 查询方法（FindGameObjectsByTag 等）
    //   在遍历结束后需要调用 FlushPendingOperations，消除 const_cast 导致的 UB 风险
    mutable std::vector<std::pair<GameObjectID, std::unique_ptr<GameObject>>> m_pendingAdd;

    // 遍历期间待销毁的对象（遍历结束后统一销毁）
    mutable std::vector<GameObjectID> m_pendingDestroy;
    mutable std::unordered_set<GameObjectID> m_pendingDestroySet;  // 修复：用于 O(1) 快速查找

    // 对象存储：ID -> unique_ptr<GameObject>（Scene 拥有所有权）
    std::unordered_map<GameObjectID, std::unique_ptr<GameObject>> m_objects;

    // 创建顺序（保证 Update 顺序确定性）
    std::vector<GameObjectID> m_creationOrder;
};