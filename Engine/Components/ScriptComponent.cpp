#include "ScriptComponent.h"

// 组件附加到 GameObject 后的初始化
// 实际项目中：此处应加载并编译绑定的脚本文件
// 例如 Lua 引擎：ScriptEngine::Get().LoadScript(m_scriptName, this);
void ScriptComponent::OnAttach()
{
    // 实际项目中：加载并编译脚本文件
    // ScriptEngine::Get().LoadScript(m_scriptName, this);
}

// 组件从 GameObject 移除前的清理
// 清空所有回调函数，防止回调中捕获的外部指针变成悬空引用
// （lambda 可能捕获了其他 GameObject 的指针，该对象可能已被销毁）
void ScriptComponent::OnDetach()
{
    m_updateCallback = nullptr;
    m_startCallback  = nullptr;
    m_stopCallback   = nullptr;
    m_fixedUpdateCallback    = nullptr;
    m_collisionEnterCallback = nullptr;
    m_collisionExitCallback  = nullptr;
    m_triggerEnterCallback   = nullptr;
    m_triggerExitCallback    = nullptr;
}


// 以下为生命周期回调的转发实现
// 每个回调先检查函数对象是否有效（不为空），再检查组件是否启用，
// 然后将 ownerPtr 传给回调，让脚本代码可以操作所属 GameObject

// 场景启动时触发
void ScriptComponent::OnStart()
{
    if (m_startCallback && IsEnabled())
        m_startCallback(m_ownerPtr);
}

// 每帧更新（传入 deltaTime 供脚本做时间相关逻辑，如冷却计时等）
void ScriptComponent::OnUpdate(float deltaTime)
{
    if (m_updateCallback && IsEnabled())
        m_updateCallback(m_ownerPtr, deltaTime);
}

// 固定时间步长更新（适合需要帧率无关的脚本逻辑，如 AI 决策频率控制）
void ScriptComponent::OnFixedUpdate(float fixedDeltaTime)
{
    if (m_fixedUpdateCallback && IsEnabled())
        m_fixedUpdateCallback(m_ownerPtr, fixedDeltaTime);
}

// 场景停止时触发（注意：Stop 不检查 IsEnabled，确保清理逻辑总能执行）
void ScriptComponent::OnStop()
{
    if (m_stopCallback)
        m_stopCallback(m_ownerPtr);
}

// --------------------------------------------------------
//  碰撞/触发器事件转发
//  由 CollisionSystem 检测到碰撞后调用
//  将事件转发给脚本注册的回调函数
// --------------------------------------------------------

// 碰撞开始：两个非触发器碰撞体首次接触
void ScriptComponent::TriggerCollisionEnter(GameObjectID otherID)
{
    if (m_collisionEnterCallback && IsEnabled())
        m_collisionEnterCallback(m_ownerPtr, otherID);
}

// 碰撞结束：两个非触发器碰撞体分离
void ScriptComponent::TriggerCollisionExit(GameObjectID otherID)
{
    if (m_collisionExitCallback && IsEnabled())
        m_collisionExitCallback(m_ownerPtr, otherID);
}

// 触发器进入：物体进入触发区域（至少一方是 Trigger）
void ScriptComponent::TriggerTriggerEnter(GameObjectID otherID)
{
    if (m_triggerEnterCallback && IsEnabled())
        m_triggerEnterCallback(m_ownerPtr, otherID);
}

// 触发器离开：物体离开触发区域
void ScriptComponent::TriggerTriggerExit(GameObjectID otherID)
{
    if (m_triggerExitCallback && IsEnabled())
        m_triggerExitCallback(m_ownerPtr, otherID);
}

// 序列化：输出脚本名称和自定义属性（键值对）
std::string ScriptComponent::Serialize() const
{
    std::ostringstream oss;
    oss << "{\"script\":\"" << m_scriptName << "\",\"properties\":{";
    bool first = true;
    for (auto& [k, v] : m_properties)
    {
        if (!first) oss << ",";
        oss << "\"" << k << "\":\"" << v << "\"";
        first = false;
    }
    oss << "}}";
    return oss.str();
}

// 调试信息：脚本名称、属性数量、是否有 Update 回调
std::string ScriptComponent::GetDebugInfo() const
{
    return "Script='" + m_scriptName + "'"
         + " Properties=" + std::to_string(m_properties.size())
         + " HasUpdate=" + (m_updateCallback ? "yes" : "no");
}
