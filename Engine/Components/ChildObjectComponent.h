
#pragma once
#include "Component.h"
#include "CoreTypes.h"
#include <vector>
#include <string>
#include <functional>

// 前向声明（避免循环包含）
class Scene;
class GameObject;

// ============================================================
//  ChildObjectComponent（子对象容器组件基类）
//
//  设计原则（参考 UE UChildActorComponent）：
//    - 组件本身不拥有子 GameObject 的所有权（所有权归 Scene）
//    - 通过 Scene API 创建/销毁子对象，并利用 Scene 的父子关系建立关联
//    - 组件销毁时（OnDetach）自动销毁所有管理的子对象
//    - 提供虚函数钩子（OnChildCreated / OnChildDestroyed），子类可重写实现自定义逻辑
//    - 支持按名称/索引查找子对象
//    - 防御循环创建（参考 UE CreateChildActor 中的循环检测）
//
//  使用方式：
//    1. 直接使用：挂到 GameObject 上，通过 CreateChild() 管理子对象
//    2. 继承使用：子类重写 OnChildCreated/OnChildDestroyed 实现特定系统需求
//       例如：
//         - InventoryComponent（背包系统）→ 子对象代表道具
//         - SpawnerComponent（生成器）→ 子对象代表生成的实体
//         - UIContainerComponent（UI容器）→ 子对象代表 UI 元素
//
//  与 UE UChildActorComponent 的对应关系：
//    UE CreateChildActor()  → 本类 CreateChild()
//    UE DestroyChildActor() → 本类 DestroyChild() / DestroyAllChildren()
//    UE OnRegister()        → 本类 OnAttach()
//    UE OnComponentDestroyed() → 本类 OnDetach()
//    UE BeginPlay()         → 本类 OnStart()
// ============================================================
class ChildObjectComponent : public Component
{
public:
    ChildObjectComponent() = default;
    ~ChildObjectComponent() override = default;

    // --------------------------------------------------------
    //  子对象工厂函数类型
    //  用于创建子对象后的自定义初始化（添加组件、设置属性等）
    //  参考 UE 的 FActorSpawnParameters::Template 机制
    // --------------------------------------------------------
    using ChildInitializer = std::function<void(GameObject*)>;

    // --------------------------------------------------------
    //  生命周期回调
    // --------------------------------------------------------

    // 组件从 GameObject 移除前，自动销毁所有管理的子对象
    // 参考 UE UChildActorComponent::OnComponentDestroyed()
    void OnDetach() override;

    // 组件停止时，不主动销毁子对象（留给 OnDetach 处理）
    // 但子类可重写此方法实现自定义的停止逻辑
    void OnStop() override;

    // --------------------------------------------------------
    //  子对象创建
    // --------------------------------------------------------

    // 创建一个子 GameObject 并自动建立父子关系
    // name: 子对象名称
    // initializer: 可选的初始化回调（用于给子对象添加组件等）
    // 返回子对象的 ID，失败返回 INVALID_GAME_OBJECT_ID
    //
    // 参考 UE UChildActorComponent::CreateChildActor()
    GameObjectID CreateChild(const std::string& name = "ChildObject",
                              ChildInitializer initializer = nullptr);

    // 批量创建子对象
    // 返回成功创建的子对象 ID 列表
    std::vector<GameObjectID> CreateChildren(int count,
                                               const std::string& namePrefix = "Child",
                                               ChildInitializer initializer = nullptr);

    // --------------------------------------------------------
    //  子对象销毁
    // --------------------------------------------------------

    // 销毁指定的子对象
    // 参考 UE UChildActorComponent::DestroyChildActor()
    bool DestroyChild(GameObjectID childID);

    // 销毁所有子对象
    // 参考 UE 在 OnComponentDestroyed 中清理所有 ChildActor 的行为
    void DestroyAllChildren();

    // --------------------------------------------------------
    //  子对象查询
    // --------------------------------------------------------

    // 获取所有子对象 ID
    const std::vector<GameObjectID>& GetChildIDs() const { return m_childIDs; }

    // 获取子对象数量
    size_t GetChildCount() const { return m_childIDs.size(); }

    // 是否为空（没有子对象）
    bool IsEmpty() const { return m_childIDs.empty(); }

    // 按索引获取子对象指针
    // 返回 nullptr 表示索引越界或对象不存在
    GameObject* GetChildAt(size_t index) const;

    // 按名称查找子对象（线性搜索，O(n)）
    // 仅搜索本组件管理的子对象，不搜索整个场景
    GameObject* FindChildByName(const std::string& name) const;

    // 检查某个 ID 是否是本组件管理的子对象
    bool HasChild(GameObjectID id) const;

    // 遍历所有子对象（安全遍历：先拷贝 ID 列表，回调中可安全增删）
    void ForEachChild(const std::function<void(GameObject*)>& func) const;

    // --------------------------------------------------------
    //  容量控制
    // --------------------------------------------------------

    // 设置子对象数量上限（0 = 不限制）
    void SetMaxChildren(int max) { m_maxChildren = max; }
    int  GetMaxChildren() const  { return m_maxChildren; }

    // --------------------------------------------------------
    //  可重写的虚函数钩子（子类用于实现自定义逻辑）
    // --------------------------------------------------------
protected:

    // 子对象创建后回调（此时子对象已经在场景中，已建立父子关系）
    // 子类可重写此方法：给子对象添加默认组件、注册事件等
    // 参考 UE 在 CreateChildActor 完成后设置 ParentComponent 和 AttachToComponent
    virtual void OnChildCreated(GameObject* child);

    // 子对象销毁前回调（此时子对象仍然有效）
    // 子类可重写此方法：取消注册事件、回收资源等
    virtual void OnChildDestroyed(GameObject* child);

    // --------------------------------------------------------
    //  序列化 / 调试
    // --------------------------------------------------------
public:
    std::string GetTypeName() const override;
    std::string GetDebugInfo() const override;
    std::string Serialize() const override;

    // --------------------------------------------------------
    //  Scene 交互代理方法（隔离 Scene.h 的直接依赖）
    //  这些方法通过 GameObject::GetScene() 间接访问 Scene API
    //  实现位于 ChildObjectComponent.cpp
    // --------------------------------------------------------
private:
    Scene* GetOwnerScene() const;
    GameObjectID CreateChildInScene(Scene* scene, const std::string& name);
    bool SetParentInScene(Scene* scene, GameObjectID childID, GameObjectID parentID);
    GameObject* GetChildFromScene(Scene* scene, GameObjectID id) const;
    bool DestroyChildInScene(Scene* scene, GameObjectID childID);

    // --------------------------------------------------------
    //  核心数据
    // --------------------------------------------------------

    // 管理的子对象 ID 列表（不持有所有权，所有权归 Scene）
    std::vector<GameObjectID> m_childIDs;

    // 子对象数量上限（0 = 不限制）
    int m_maxChildren = 0;
};