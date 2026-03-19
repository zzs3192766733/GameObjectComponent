#pragma once
#include "CoreTypes.h"
#include "Component.h"
#include "GameplayTag.h"
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include <functional>
#include <cassert>
#include <unordered_set>
#include <iostream>

// ============================================================
//  GameObject
//  设计原则：
//    - Scene 通过 unique_ptr<GameObject> 拥有所有 GameObject
//    - GameObject 通过 unique_ptr<Component> 拥有所有 Component
//    - 父子关系通过 GameObjectID 引用（不持有所有权）
//    - 通过 ComponentTypeID 实现 O(1) 组件查找
// ============================================================
class Scene;  // 前向声明

class GameObject
{
public:
    // --------------------------------------------------------
    //  构造 / 析构
    // --------------------------------------------------------
    explicit GameObject(GameObjectID id, std::string name, Scene* scene)
        : m_id(id)
        , m_name(std::move(name))
        , m_scene(scene)
    {}

    ~GameObject();

    // 禁止拷贝和移动（由 Scene 通过 unique_ptr 管理）
    GameObject(const GameObject&) = delete;
    GameObject& operator=(const GameObject&) = delete;
    GameObject(GameObject&&) = delete;
    GameObject& operator=(GameObject&&) = delete;

    // --------------------------------------------------------
    //  基本属性
    // --------------------------------------------------------
    GameObjectID        GetID()     const { return m_id; }
    const std::string&  GetName()   const { return m_name; }
    void                SetName(const std::string& name) { m_name = name; }

    bool IsActive()                 const { return m_active; }
    void SetActive(bool active)           { m_active = active; }

    bool IsStatic()                 const { return m_isStatic; }
    void SetStatic(bool isStatic)         { m_isStatic = isStatic; }

    // --------------------------------------------------------
    //  GameplayTag 系统（参考 UE GameplayTags 设计）
    //  替代旧的 string tag + uint32 layer 方案
    //  优势：层级匹配 O(1)、去除硬编码、支持多标签
    // --------------------------------------------------------

    // 获取标签容器（可读写）
    GameplayTagContainer&       GetTags()       { return m_tags; }
    const GameplayTagContainer& GetTags() const { return m_tags; }

    // 快捷方法：添加/移除/检查标签
    void AddTag(const GameplayTag& tag)    { m_tags.AddTag(tag); }
    void RemoveTag(const GameplayTag& tag)  { m_tags.RemoveTag(tag); }
    bool HasTag(const GameplayTag& tag) const { return m_tags.HasTag(tag); }
    bool HasTagExact(const GameplayTag& tag) const { return m_tags.HasTagExact(tag); }

    // 层级（用于渲染排序，保留为简单整数）
    uint32_t            GetLayer()  const { return m_layer; }
    void                SetLayer(uint32_t layer) { m_layer = layer; }

    // --------------------------------------------------------
    //  场景引用（弱引用，不持有所有权）
    //  SetScene 仅由 Scene::AttachGameObject 调用，用于跨场景迁移时更新引用
    // --------------------------------------------------------
    Scene* GetScene() const { return m_scene; }
    void   SetScene(Scene* scene) { m_scene = scene; }

    // --------------------------------------------------------
    //  父子关系（通过ID引用，Scene 负责维护）
    // --------------------------------------------------------
    GameObjectID        GetParentID()   const { return m_parentID; }
    void                SetParentID(GameObjectID id) { m_parentID = id; }

    const std::vector<GameObjectID>& GetChildrenIDs() const { return m_childrenIDs; }

    void AddChildID(GameObjectID childID);
    void RemoveChildID(GameObjectID childID);
    void ClearChildrenIDs() { m_childrenIDs.clear(); }  // 批量清空（由 Scene::DetachGameObject 使用）
    bool HasChildren() const { return !m_childrenIDs.empty(); }

    // --------------------------------------------------------
    //  组件管理
    // --------------------------------------------------------

    // 添加组件（T 必须继承自 Component）
    // 返回组件原始指针（所有权归 GameObject）
    // 若当前正在遍历组件（Update/FixedUpdate/Render 期间），
    // 新组件会在本帧遍历结束后才加入 m_componentOrder，
    // 但可以立即通过 GetComponent<T>() 查到（已写入 m_components）
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args)
    {
        static_assert(std::is_base_of<Component, T>::value,
                      "T 必须继承自 Component");

        ComponentTypeID typeID = ComponentTypeRegistry::GetTypeID<T>();

        // 不允许重复添加同类型组件
        if (m_components.count(typeID))
        {
            std::cerr << "[GameObject] 警告: 对象 '" << m_name
                      << "' 已存在组件类型 " << typeID << "\n";
            return static_cast<T*>(m_components[typeID].get());
        }

        // 创建组件（unique_ptr 管理生命周期）
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* rawPtr = comp.get();

        // 设置所属信息
        rawPtr->SetOwnerID(m_id);
        rawPtr->SetOwnerPtr(this);

        // 写入 map（立即可查，不影响当前遍历的快照）
        m_components[typeID] = std::move(comp);

        if (m_isIteratingComponents)
        {
            // 遍历期间：延迟加入顺序列表，本帧不参与 Update
            m_pendingComponentAdd.push_back(typeID);
        }
        else
        {
            m_componentOrder.push_back(typeID);
        }

        // 触发生命周期回调（无论是否在遍历中都立即触发）
        rawPtr->OnAttach();

        // 修复：如果 GameObject 已经 Start 过，后添加的组件需要立即追赶 OnStart
        // 参考 UE 的 RegisterComponent 行为：如果 Actor 已经 BeginPlay，
        // 新注册的组件会立即收到 BeginPlay 回调
        if (m_hasStarted && rawPtr->IsEnabled())
        {
            rawPtr->OnStart();
        }

        return rawPtr;
    }

    // 获取组件（O(1) 哈希查找）
    template<typename T>
    T* GetComponent() const
    {
        ComponentTypeID typeID = ComponentTypeRegistry::GetTypeID<T>();
        auto it = m_components.find(typeID);
        if (it != m_components.end())
            return static_cast<T*>(it->second.get());
        return nullptr;
    }

    // 检查是否有某类型组件
    template<typename T>
    bool HasComponent() const
    {
        ComponentTypeID typeID = ComponentTypeRegistry::GetTypeID<T>();
        return m_components.count(typeID) > 0;
    }

    // 移除组件
    // 若当前正在遍历组件，延迟到遍历结束后再真正移除
    template<typename T>
    bool RemoveComponent()
    {
        ComponentTypeID typeID = ComponentTypeRegistry::GetTypeID<T>();
        if (!m_components.count(typeID))
            return false;

        if (m_isIteratingComponents)
        {
            // 遍历期间：标记为待移除，遍历结束后统一处理
            m_pendingComponentRemove.push_back(typeID);
            m_pendingComponentRemoveSet.insert(typeID);  // 同步更新 set
            return true;
        }

        return RemoveComponentImmediate(typeID);
    }

    // 遍历所有组件
    void ForEachComponent(const std::function<void(Component*)>& func) const;

    // 获取组件数量
    size_t GetComponentCount() const { return m_components.size(); }

    // --------------------------------------------------------
    //  生命周期驱动（由 Scene 调用）
    // --------------------------------------------------------
    void Start();
    void Update(float deltaTime);
    void FixedUpdate(float fixedDeltaTime);
    void Render();
    void Stop();

    // 查询 GameObject 是否已经完成 Start（用于组件的延迟 OnStart 判断）
    bool HasStarted() const { return m_hasStarted; }

    // --------------------------------------------------------
    //  序列化
    // --------------------------------------------------------
    std::string Serialize() const;

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    void PrintDebugInfo() const;

private:
    // --------------------------------------------------------
    //  内部工具：快照遍历 + 延迟操作
    // --------------------------------------------------------

    // 用快照遍历 m_componentOrder，遍历期间的增删操作被缓冲
    template<typename Func>
    void IterateComponents(Func&& func)
    {
        // 拷贝快照（O(n)，保证遍历安全）
        std::vector<ComponentTypeID> snapshot = m_componentOrder;

        m_isIteratingComponents = true;
        for (ComponentTypeID typeID : snapshot)
        {
            // 跳过遍历期间已被标记为待移除的组件
            if (IsPendingRemove(typeID)) continue;

            auto it = m_components.find(typeID);
            if (it != m_components.end())
                func(it->second.get());
        }
        m_isIteratingComponents = false;

        // 遍历结束后统一提交缓冲的增删操作
        FlushPendingComponents();
    }

    bool IsPendingRemove(ComponentTypeID typeID) const;
    void FlushPendingComponents();
    bool RemoveComponentImmediate(ComponentTypeID typeID);

    // --------------------------------------------------------
    //  核心数据
    // --------------------------------------------------------

    GameObjectID    m_id;           // 唯一ID（不可变）
    std::string     m_name;         // 对象名称
    Scene*          m_scene;        // 所属场景（弱引用）
    bool            m_active   = true;
    bool            m_isStatic = false;
    bool            m_hasStarted = false;  // 是否已调用过 Start（用于后添加组件的 OnStart 追赶）
    GameplayTagContainer m_tags;      // GameplayTag 容器（替代旧的 string tag）
    uint32_t              m_layer = 0; // 渲染层级（简单整数即可）

    // 父子关系（通过ID引用，不持有所有权）
    GameObjectID                m_parentID   = INVALID_GAME_OBJECT_ID;
    std::vector<GameObjectID>   m_childrenIDs;

    // 组件存储：TypeID -> unique_ptr<Component>（O(1) 查找）
    std::unordered_map<ComponentTypeID, std::unique_ptr<Component>> m_components;

    // 组件添加顺序（保证 Update 顺序确定性）
    std::vector<ComponentTypeID> m_componentOrder;

    // --------------------------------------------------------
    //  遍历安全保护
    // --------------------------------------------------------
    bool m_isIteratingComponents = false;  // 当前是否正在遍历组件
    std::vector<ComponentTypeID> m_pendingComponentAdd;     // 遍历期间新增的组件（下一帧生效）
    std::vector<ComponentTypeID> m_pendingComponentRemove;  // 遍历期间待移除的组件
    std::unordered_set<ComponentTypeID> m_pendingComponentRemoveSet;  // 修复：用于 O(1) 快速查找
};