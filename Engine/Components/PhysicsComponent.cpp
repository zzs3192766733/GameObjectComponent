#include "PhysicsComponent.h"
#include "TransformComponent.h"

// OnStart：物理组件启动时的初始化
// 不重置速度和加速度，因为用户可能在 Start 之前通过
// SetVelocity() 设置了初始速度（例如投射物的发射速度）
void PhysicsComponent::OnStart()
{
    // 不重置速度和加速度，保留用户在 Start 之前设置的初始值
    // 加速度在每帧 FixedUpdate 末尾已会被清零
}


// 固定时间步长物理更新（核心物理循环）
// 执行流程：施加重力 → 速度积分 → 阻尼衰减 → 更新位置 → 重置加速度
// 使用半隐式欧拉法（Semi-implicit Euler）：先更新速度，再用新速度更新位置
// 相比显式欧拉法，能量守恒性更好，适合游戏物理
void PhysicsComponent::OnFixedUpdate(float fixedDeltaTime)
{
    // 禁用或运动学模式下不执行物理模拟
    // 运动学（Kinematic）物体由用户直接控制位置，不受物理力影响
    if (!IsEnabled() || m_isKinematic) return;

    // 1. 应用重力（2D 重力沿 -Y 轴，即向下）
    //    m_gravityScale 可以调节重力强度：>1 更重，<1 更轻，0 无重力
    if (m_gravityEnabled)
    {
        m_acceleration.y -= m_gravityScale * 9.81f;
    }

    // 2. 速度积分（半隐式欧拉法：v' = v + a * dt）
    //    先更新速度，保证后续位置积分使用的是最新速度
    m_velocity += m_acceleration * fixedDeltaTime;

    // 3. 应用线性阻尼（模拟空气阻力/摩擦力）
    //    修复：使用 clamp 防止阻尼因子变为负数（当 damping * dt >= 1 时速度会反向）
    //    正常游戏帧率下不会触发，但作为防御性措施保留
    float dampFactor = std::max(0.0f, 1.0f - m_linearDamping * fixedDeltaTime);
    m_velocity *= dampFactor;

    // 4. 更新 Transform 位置（位置积分：pos' = pos + v * dt）
    //    PhysicsComponent 不直接存储位置，而是修改 TransformComponent 的位置
    //    这保证了位置数据的单一数据源（Single Source of Truth）
    if (m_ownerPtr)
    {
        if (auto* transform = m_ownerPtr->GetComponent<TransformComponent>())
        {
            Vec2 pos = transform->GetPosition();
            pos += m_velocity * fixedDeltaTime;
            transform->SetPosition(pos);
        }
    }

    // 5. 重置帧加速度
    //    重力在下一帧 FixedUpdate 开头重新施加
    //    外部施加的力（ApplyForce）也是单帧有效，需要每帧重新施加
    m_acceleration = { 0.0f, 0.0f };
}

// 序列化为 JSON 字符串（包含所有物理属性）
std::string PhysicsComponent::Serialize() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{"
        << "\"mass\":"           << m_mass << ","
        << "\"velocity\":{\"x\":" << m_velocity.x << ",\"y\":" << m_velocity.y << "},"
        << "\"gravityEnabled\":" << (m_gravityEnabled ? "true" : "false") << ","
        << "\"gravityScale\":"   << m_gravityScale << ","
        << "\"isKinematic\":"    << (m_isKinematic ? "true" : "false") << ","
        << "\"linearDamping\":"  << m_linearDamping
        << "}";
    return oss.str();
}

// 调试信息：输出质量、速度、重力开关、运动学模式等关键状态
std::string PhysicsComponent::GetDebugInfo() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Mass=" << m_mass
        << " Vel=" << m_velocity.ToString()
        << " Gravity=" << (m_gravityEnabled ? "on" : "off")
        << " Kinematic=" << (m_isKinematic ? "yes" : "no");
    return oss.str();
}