
#pragma once
#include "CoreTypes.h"
#include <string>


// ============================================================
//  Component 基类
//  设计原则：
//    - Component 不拥有 GameObject，只持有其ID（避免循环引用）
//    - Component 由 GameObject 通过 unique_ptr 管理生命周期
//    - 通过 ComponentTypeID 实现 O(1) 类型查找
// ============================================================
class GameObject;  // 前向声明

class Component
{
public:
    Component() = default;
    virtual ~Component() = default;

    // 禁止拷贝和移动（组件绑定到特定 GameObject，移动会导致 m_ownerPtr 悬空）
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    Component(Component&&) = delete;
    Component& operator=(Component&&) = delete;

    // --------------------------------------------------------
    //  生命周期回调（由 GameObject/Scene 驱动）
    // --------------------------------------------------------

    // 组件被添加到 GameObject 后调用（初始化资源）
    virtual void OnAttach() {}

    // 组件从 GameObject 移除前调用（释放资源）
    virtual void OnDetach() {}

    // 每帧更新
    virtual void OnUpdate(float deltaTime) {}

    // 固定时间步长更新（物理用）
    virtual void OnFixedUpdate(float fixedDeltaTime) {}

    // 渲染前调用
    virtual void OnRender() {}

    // 场景开始时调用
    virtual void OnStart() {}

    // 场景停止时调用
    virtual void OnStop() {}

    // --------------------------------------------------------
    //  序列化接口
    // --------------------------------------------------------
    virtual std::string Serialize() const { return "{}"; }
    virtual void Deserialize(const std::string& data) {}

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    virtual std::string GetDebugInfo() const { return GetTypeName(); }
    virtual std::string GetTypeName() const { return "Component"; }

    // --------------------------------------------------------
    //  状态控制
    // --------------------------------------------------------
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // --------------------------------------------------------
    //  所属对象（弱引用，通过ID，不持有所有权）
    // --------------------------------------------------------
    void SetOwnerID(GameObjectID ownerID) { m_ownerID = ownerID; }
    GameObjectID GetOwnerID() const { return m_ownerID; }

    // 获取所属 GameObject 指针（缓存的原始指针，由 GameObject::AddComponent 设置）
    // 修复 Bug#6：添加 IsOwnerValid() 安全检查方法
    //   正常流程下 Component 随 GameObject 一起销毁，m_ownerPtr 不会悬空
    //   但在延迟回调/事件系统中，建议先调用 IsOwnerValid() 再访问 GetOwner()
    void SetOwnerPtr(GameObject* owner) { m_ownerPtr = owner; }
    GameObject* GetOwner() const { return m_ownerPtr; }

    // 安全检查：所属对象是否仍然有效
    // 组件被 Detach 或 Owner 被销毁时，m_ownerPtr 会被置空
    bool IsOwnerValid() const { return m_ownerPtr != nullptr && m_ownerID != INVALID_GAME_OBJECT_ID; }

    // 清除所属对象引用（在 Detach 时由 GameObject 调用）
    void ClearOwner()
    {
        m_ownerPtr = nullptr;
        m_ownerID = INVALID_GAME_OBJECT_ID;
    }

protected:
    bool         m_enabled  = true;
    GameObjectID m_ownerID  = INVALID_GAME_OBJECT_ID;
    GameObject*  m_ownerPtr = nullptr;  // 原始指针（非拥有），由 GameObject 管理
};