#pragma once
#include "Component.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include <string>
#include <sstream>
#include <iomanip>
#include <iostream>

// ============================================================
//  PhysicsComponent（2D 物理组件）
//  负责：刚体模拟、速度、加速度、重力（-Y 方向）
//  依赖：TransformComponent（更新位置）
// ============================================================
class PhysicsComponent : public Component
{
public:
    PhysicsComponent() = default;
    ~PhysicsComponent() override = default;

    // --------------------------------------------------------
    //  生命周期
    // --------------------------------------------------------
    void OnAttach()      override {}
    void OnDetach()      override {}
    void OnStart()       override;
    void OnFixedUpdate(float fixedDeltaTime) override;

    // --------------------------------------------------------
    //  质量
    //  修复 Bug#5：最小值从 0.0001f 提高到 0.01f，避免极小质量导致物理数值爆炸
    //  同时 GetInverseMass 添加安全检查
    // --------------------------------------------------------
    void  SetMass(float mass)    { m_mass = (mass > 0.01f) ? mass : 0.01f; }
    float GetMass() const        { return m_mass; }
    float GetInverseMass() const { return (m_mass > 0.0f) ? (1.0f / m_mass) : 0.0f; }

    // --------------------------------------------------------
    //  速度
    //  修复 Bug#5：添加速度上限保护，避免数值爆炸
    // --------------------------------------------------------
    static constexpr float MAX_VELOCITY = 1e6f;

    void        SetVelocity(float vx, float vy) { m_velocity = { vx, vy }; ClampVelocity(); }
    void        SetVelocity(const Vec2& vel)    { m_velocity = vel; ClampVelocity(); }
    const Vec2& GetVelocity() const             { return m_velocity; }

    void ApplyImpulse(const Vec2& impulse) { m_velocity += impulse * GetInverseMass(); ClampVelocity(); }
    void ApplyForce(const Vec2& force)     { m_acceleration += force * GetInverseMass(); }

    // --------------------------------------------------------
    //  重力
    // --------------------------------------------------------
    void  SetGravityEnabled(bool enabled) { m_gravityEnabled = enabled; }
    bool  IsGravityEnabled() const        { return m_gravityEnabled; }
    void  SetGravityScale(float scale)    { m_gravityScale = scale; }
    float GetGravityScale() const         { return m_gravityScale; }

    // --------------------------------------------------------
    //  运动学模式
    // --------------------------------------------------------
    void SetKinematic(bool kinematic) { m_isKinematic = kinematic; }
    bool IsKinematic() const          { return m_isKinematic; }

    // --------------------------------------------------------
    //  阻尼
    // --------------------------------------------------------
    void  SetLinearDamping(float damping) { m_linearDamping = damping; }
    float GetLinearDamping() const        { return m_linearDamping; }

    // --------------------------------------------------------
    //  序列化 / 调试
    // --------------------------------------------------------
    std::string Serialize()    const override;
    std::string GetTypeName()  const override { return "PhysicsComponent"; }
    std::string GetDebugInfo() const override;

private:
    float m_mass           = 1.0f;
    Vec2  m_velocity       = { 0.0f, 0.0f };
    Vec2  m_acceleration   = { 0.0f, 0.0f };
    bool  m_gravityEnabled = true;
    float m_gravityScale   = 1.0f;
    bool  m_isKinematic    = false;
    float m_linearDamping  = 0.01f;

    // 修复 Bug#5：速度上限钳制，避免物理数值爆炸
    void ClampVelocity()
    {
        if (m_velocity.x >  MAX_VELOCITY) m_velocity.x =  MAX_VELOCITY;
        if (m_velocity.x < -MAX_VELOCITY) m_velocity.x = -MAX_VELOCITY;
        if (m_velocity.y >  MAX_VELOCITY) m_velocity.y =  MAX_VELOCITY;
        if (m_velocity.y < -MAX_VELOCITY) m_velocity.y = -MAX_VELOCITY;
    }
};