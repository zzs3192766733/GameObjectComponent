#include "TransformComponent.h"
#include "PhysicsComponent.h"
#include "GameObject.h"
#include "PropertyMeta.h"


// ============================================================
//  属性反射注册（核心！）
//  这里声明 TransformComponent 的哪些属性参与序列化，哪些不参与
//  参考 UE 的 UPROPERTY(SaveGame) / UPROPERTY(Transient) 标记方式
// ============================================================

// 自动注册到全局 PropertyRegistry（程序启动时自动执行）
REGISTER_COMPONENT_PROPERTIES(TransformComponent)

// 属性列表定义
std::vector<PropertyMeta> TransformComponent::RegisterProperties()
{
    std::vector<PropertyMeta> props;

    // Serializable 属性：参与序列化
    REGISTER_PROPERTY(props, TransformComponent, m_position, EPropertyType::Vec2,
                      EPropertyFlags::Serializable, "Transform");
    REGISTER_PROPERTY(props, TransformComponent, m_rotation, EPropertyType::Float,
                      EPropertyFlags::Serializable, "Transform");
    REGISTER_PROPERTY(props, TransformComponent, m_scale, EPropertyType::Vec2,
                      EPropertyFlags::Serializable, "Transform");

    // Transient 属性：不参与序列化（运行时临时数据）
    REGISTER_PROPERTY(props, TransformComponent, m_velocity, EPropertyType::Vec2,
                      EPropertyFlags::Transient, "Physics");

    return props;
}


// 组件附加到 GameObject 后的初始化
// 当前无需额外操作，成员变量已在声明处有默认值
void TransformComponent::OnAttach()
{
    // 成员变量已在构造函数中初始化，无需重复设置
}


// 每帧更新位置（简单运动积分）
// 如果对象同时拥有 PhysicsComponent，则跳过此处的积分，
// 因为 PhysicsComponent::OnFixedUpdate 已经处理了位置更新。
// 两者同时积分会导致"双重积分"，对象移动速度翻倍。
void TransformComponent::OnUpdate(float deltaTime)
{
    // 如果存在 PhysicsComponent，则由物理系统负责位置积分，避免双重积分
    if (m_ownerPtr && m_ownerPtr->GetComponent<PhysicsComponent>())
        return;

    // 仅当速度不为零时才更新位置（避免浮点误差累积）
    if (m_velocity.LengthSq() > 1e-8f)
    {
        // 简单欧拉积分：位置 += 速度 × 时间步长
        m_position += m_velocity * deltaTime;
    }
}


// 获取物体的朝向向量（2D 中用旋转角度计算）
// 0度 → 朝右(1,0)，90度 → 朝上(0,1)，遵循数学上的逆时针正方向
Vec2 TransformComponent::GetForward() const
{
    float rad = GetRotationRad();
    return { std::cos(rad), std::sin(rad) };
}

// 基于属性反射的自动序列化（新增）
// 直接从 PropertyRegistry 读取元数据，自动项序列化标记为 Serializable 的属性
std::string TransformComponent::AutoSerializeProperties() const
{
    return AutoSerializer::SerializeByType(
        this, std::type_index(typeid(TransformComponent)), true);
}

// 手动序列化（保留原有实现，与 AutoSerialize 输出一致）
// 用户可以选择任一方式
std::string TransformComponent::Serialize() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{"
        << "\"position\":{\"x\":" << m_position.x << ",\"y\":" << m_position.y << "},"
        << "\"rotation\":"        << m_rotation << ","
        << "\"scale\":{\"x\":"    << m_scale.x  << ",\"y\":" << m_scale.y << "}"
        << "}";
    return oss.str();
}

// 反序列化（暂未实现）
// 实际项目中应使用 JSON 库（如 nlohmann/json）解析字符串并赋值
void TransformComponent::Deserialize(const std::string& /*data*/)
{
    // 简化实现：实际项目中使用 JSON 库解析
}

// 调试信息：输出位置、旋转角度、缩放，便于开发阶段排查问题
std::string TransformComponent::GetDebugInfo() const
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Pos=" << m_position.ToString()
        << " Rot=" << m_rotation << "deg"
        << " Scale=" << m_scale.ToString();
    return oss.str();
}
