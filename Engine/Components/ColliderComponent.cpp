#include "ColliderComponent.h"
#include "TransformComponent.h"
#include "GameObject.h"


// 获取碰撞体在世界坐标中的中心位置
// 修复：考虑 TransformComponent 的缩放，局部偏移量随缩放变化
Vec2 ColliderComponent::GetWorldCenter() const
{
    if (m_ownerPtr)
    {
        if (auto* tf = m_ownerPtr->GetComponent<TransformComponent>())
        {
            const Vec2& pos   = tf->GetPosition();
            const Vec2& scale = tf->GetScale();
            // 局部偏移×缩放 + 世界位置 = 世界中心
            return { pos.x + m_center.x * scale.x, pos.y + m_center.y * scale.y };
        }
    }
    // 没有 TransformComponent 时，退化为局部偏移量本身
    return m_center;
}

// 序列化为 JSON 字符串
// 将形状类型存为整数（0=Box, 1=Circle, 2=Capsule）
std::string ColliderComponent::Serialize() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{"
        << "\"shape\":"     << static_cast<int>(m_shape)        << ","
        << "\"isTrigger\":" << (m_isTrigger ? "true" : "false") << ","
        << "\"layer\":"     << m_layer                          << ","
        << "\"mask\":"      << m_collisionMask                  << ","
        << "\"radius\":"    << m_radius                         << ","
        << "\"halfExtents\":{\"x\":" << m_halfExtents.x << ",\"y\":" << m_halfExtents.y << "},"
        << "\"center\":{\"x\":"      << m_center.x      << ",\"y\":" << m_center.y      << "}"
        << "}";
    return oss.str();
}

// 调试信息：输出形状类型、触发器状态、碰撞层等
// 根据形状不同，输出不同的几何参数（Box输出半尺寸，Circle输出半径等）
std::string ColliderComponent::GetDebugInfo() const
{
    std::ostringstream oss;
    const char* shapeNames[] = { "Box", "Circle", "Capsule" };
    oss << "Shape=" << shapeNames[static_cast<int>(m_shape)]
        << " Trigger=" << (m_isTrigger ? "yes" : "no")
        << " Layer=" << m_layer
        << " Center=" << m_center.ToString();
    if (m_shape == Shape::Box)
        oss << " HalfExt=" << m_halfExtents.ToString();
    else
        oss << " Radius=" << m_radius;
    if (m_shape == Shape::Capsule)
        oss << " HalfH=" << m_capsuleHalfHeight;
    return oss.str();
}
