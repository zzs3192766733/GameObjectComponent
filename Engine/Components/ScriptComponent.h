#pragma once
#include "Component.h"
#include "GameObject.h"
#include <string>
#include <functional>
#include <unordered_map>
#include <sstream>
#include <iostream>

// ============================================================
//  ScriptComponent（脚本组件）
//  负责：绑定游戏逻辑脚本，支持事件回调
//  设计：使用函数对象模拟脚本行为（实际项目可接入 Lua/Python）
// ============================================================

// 脚本事件类型
enum class ScriptEvent
{
    OnStart,
    OnUpdate,
    OnFixedUpdate,
    OnStop,
    OnCollisionEnter,   // 碰撞开始
    OnCollisionExit,    // 碰撞结束
    OnTriggerEnter,     // 触发器进入
    OnTriggerExit,      // 触发器离开
    OnEnable,           // 组件启用
    OnDisable,          // 组件禁用
};

class ScriptComponent : public Component
{
public:
    // 脚本回调函数类型
    using UpdateCallback       = std::function<void(GameObject*, float)>;
    using EventCallback        = std::function<void(GameObject*)>;
    using CollisionCallback    = std::function<void(GameObject*, GameObjectID)>;

    ScriptComponent() = default;
    ~ScriptComponent() override = default;

    // --------------------------------------------------------
    //  生命周期
    // --------------------------------------------------------
    void OnAttach() override;
    void OnDetach() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnFixedUpdate(float fixedDeltaTime) override;
    void OnStop() override;

    // --------------------------------------------------------
    //  脚本名称（用于调试和序列化）
    // --------------------------------------------------------
    void               SetScriptName(const std::string& name) { m_scriptName = name; }
    const std::string& GetScriptName() const                   { return m_scriptName; }

    // --------------------------------------------------------
    //  注册回调（C++ 侧绑定逻辑）
    // --------------------------------------------------------
    void SetOnStart(EventCallback cb)              { m_startCallback = std::move(cb); }
    void SetOnUpdate(UpdateCallback cb)            { m_updateCallback = std::move(cb); }
    void SetOnFixedUpdate(UpdateCallback cb)       { m_fixedUpdateCallback = std::move(cb); }
    void SetOnStop(EventCallback cb)               { m_stopCallback = std::move(cb); }

    void SetOnCollisionEnter(CollisionCallback cb) { m_collisionEnterCallback = std::move(cb); }
    void SetOnCollisionExit(CollisionCallback cb)  { m_collisionExitCallback  = std::move(cb); }
    void SetOnTriggerEnter(CollisionCallback cb)   { m_triggerEnterCallback   = std::move(cb); }
    void SetOnTriggerExit(CollisionCallback cb)    { m_triggerExitCallback    = std::move(cb); }

    // --------------------------------------------------------
    //  碰撞事件触发（由物理系统调用）
    // --------------------------------------------------------
    void TriggerCollisionEnter(GameObjectID otherID);
    void TriggerCollisionExit(GameObjectID otherID);
    void TriggerTriggerEnter(GameObjectID otherID);
    void TriggerTriggerExit(GameObjectID otherID);

    // --------------------------------------------------------
    //  自定义属性（脚本变量存储，用于序列化）
    // --------------------------------------------------------
    void SetProperty(const std::string& key, const std::string& value)
    {
        m_properties[key] = value;
    }

    std::string GetProperty(const std::string& key, const std::string& defaultVal = "") const
    {
        auto it = m_properties.find(key);
        return (it != m_properties.end()) ? it->second : defaultVal;
    }

    bool HasProperty(const std::string& key) const
    {
        return m_properties.count(key) > 0;
    }

    // --------------------------------------------------------
    //  序列化
    // --------------------------------------------------------
    std::string Serialize() const override;

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    std::string GetTypeName() const override { return "ScriptComponent"; }

    std::string GetDebugInfo() const override;

private:
    std::string     m_scriptName;

    // 生命周期回调
    EventCallback       m_startCallback;
    UpdateCallback      m_updateCallback;
    UpdateCallback      m_fixedUpdateCallback;
    EventCallback       m_stopCallback;

    // 碰撞/触发器回调
    CollisionCallback   m_collisionEnterCallback;
    CollisionCallback   m_collisionExitCallback;
    CollisionCallback   m_triggerEnterCallback;
    CollisionCallback   m_triggerExitCallback;

    // 脚本属性（键值对，用于序列化和脚本变量）
    std::unordered_map<std::string, std::string> m_properties;
};