#pragma once
#include "Component.h"
#include "PropertyMeta.h"
#include <string>
#include <cmath>
#include <sstream>
#include <iomanip>

// ============================================================
//  2D 向量（避免引入外部数学库依赖）
//  支持：基本运算、点乘/叉乘、归一化、常用静态方向向量
//  ? operator== 是浮点精确比较，不适合经过多次计算后的比较
// ============================================================
struct Vec2
{
    float x = 0.0f, y = 0.0f;

    Vec2() = default;
    Vec2(float x, float y) : x(x), y(y) {}

    // 基本算术运算符（不修改原向量，返回新向量）
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }  // 向量加法
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }  // 向量减法
    Vec2 operator*(float s)       const { return { x * s,   y * s   }; }  // 标量乘法
    Vec2 operator/(float s)       const { return { x / s,   y / s   }; }  // 标量除法
    // 复合赋值运算符（修改原向量）
    Vec2& operator+=(const Vec2& o)     { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o)     { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(float s)           { x *= s;   y *= s;   return *this; }

    // ? 浮点精确比较（可能因累积误差失效，建议用 NearlyEqual 替代）
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }

    // 修复：提供带容差的浮点比较，适用于经过多次计算后的向量比较
    bool NearlyEqual(const Vec2& o, float epsilon = 1e-5f) const
    {
        return std::abs(x - o.x) < epsilon && std::abs(y - o.y) < epsilon;
    }

    float LengthSq() const { return x * x + y * y; }  // 长度的平方（避免开方，用于比较）
    float Length()   const { return std::sqrt(LengthSq()); }  // 向量长度（模）

    float Dot(const Vec2& o)   const { return x * o.x + y * o.y; }  // 点乘（投影、夹角）
    float Cross(const Vec2& o) const { return x * o.y - y * o.x; }  // 叉乘（2D中返回标量，用于判断左右侧）

    // 归一化（返回单位向量）
    // 零向量安全处理：长度近似为 0 时返回零向量

    Vec2 Normalized() const
    {
        float len = Length();
        if (len < 1e-8f) return { 0.0f, 0.0f };
        return { x / len, y / len };
    }

    Vec2 Perpendicular() const { return { -y, x }; }  // 垂直向量（逆时针旋转90°）

    std::string ToString() const
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2)
            << "(" << x << ", " << y << ")";
        return oss.str();
    }

    // 常用静态方向向量（便捷工厂方法）
    static Vec2 Zero()  { return { 0.0f, 0.0f }; }   // 原点
    static Vec2 One()   { return { 1.0f, 1.0f }; }   // 均匀缩放
    static Vec2 Up()    { return { 0.0f, 1.0f }; }   // +Y 方向
    static Vec2 Down()  { return { 0.0f,-1.0f }; }   // -Y 方向
    static Vec2 Left()  { return {-1.0f, 0.0f }; }   // -X 方向
    static Vec2 Right() { return { 1.0f, 0.0f }; }   // +X 方向
};

// ============================================================
//  TransformComponent（2D 变换组件）
//  负责：位置（Vec2）、旋转（单个 float，角度）、缩放（Vec2）
//  每个 GameObject 通常都有且只有一个 TransformComponent
// ============================================================
class TransformComponent : public Component
{
public:
    TransformComponent() = default;
    ~TransformComponent() override = default;

    // --------------------------------------------------------
    //  生命周期
    // --------------------------------------------------------
    void OnAttach() override;
    void OnUpdate(float deltaTime) override;

    // --------------------------------------------------------
    //  位置
    // --------------------------------------------------------
    void        SetPosition(float x, float y) { m_position = { x, y }; }
    void        SetPosition(const Vec2& pos)  { m_position = pos; }
    const Vec2& GetPosition() const           { return m_position; }

    void Translate(float dx, float dy)        { m_position.x += dx; m_position.y += dy; }
    void Translate(const Vec2& delta)         { m_position += delta; }

    // --------------------------------------------------------
    //  旋转（单位：度，2D 只有 Z 轴旋转）
    // --------------------------------------------------------
    void  SetRotation(float degrees) { m_rotation = degrees; }
    float GetRotation() const        { return m_rotation; }
    void  Rotate(float degrees)      { m_rotation += degrees; }

    float GetRotationRad() const
    {
        constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;
        return m_rotation * DEG2RAD;
    }

    Vec2 GetForward() const;

    // --------------------------------------------------------
    //  缩放
    // --------------------------------------------------------
    void        SetScale(float x, float y)   { m_scale = { x, y }; }
    void        SetScale(float uniform)      { m_scale = { uniform, uniform }; }
    void        SetScale(const Vec2& scale)  { m_scale = scale; }
    const Vec2& GetScale() const             { return m_scale; }

    // --------------------------------------------------------
    //  速度（已废弃，建议使用 PhysicsComponent 管理速度）
    //  修复：保留接口但标记为 deprecated，避免与 PhysicsComponent 职责重叠
    //  仅在没有 PhysicsComponent 时生效（用于简单运动模拟）
    // --------------------------------------------------------
    [[deprecated("请使用 PhysicsComponent::SetVelocity 替代")]]
    void        SetVelocity(const Vec2& vel)        { m_velocity = vel; }
    [[deprecated("请使用 PhysicsComponent::SetVelocity 替代")]]
    void        SetVelocity(float vx, float vy)     { m_velocity = { vx, vy }; }
    const Vec2& GetVelocity() const                 { return m_velocity; }

    // --------------------------------------------------------
    //  便捷方法
    // --------------------------------------------------------
    std::string GetPositionStr() const { return m_position.ToString(); }
    std::string GetScaleStr()    const { return m_scale.ToString(); }

    // --------------------------------------------------------
    //  属性反射注册（声明哪些属性参与序列化）
    //  参考 UE UPROPERTY() 的注册方式
    // --------------------------------------------------------
    static std::vector<PropertyMeta> RegisterProperties();

    // --------------------------------------------------------
    //  序列化（新增：基于属性反射的自动序列化）
    // --------------------------------------------------------
    std::string Serialize() const override;
    std::string AutoSerializeProperties() const;
    void        Deserialize(const std::string& data) override;

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    std::string GetTypeName()  const override { return "TransformComponent"; }
    std::string GetDebugInfo() const override;

private:
    // --------------------------------------------------------
    //  属性标记说明（参考 C# [SerializeField] / UE UPROPERTY()）
    //
    //  MPROPERTY(Serializable)   → 参与序列化
    //  MPROPERTY(Transient)      → 不参与序列化（运行时数据）
    //  不加 MPROPERTY            → 完全不被反射系统感知
    // --------------------------------------------------------
    MPROPERTY(Serializable)   Vec2  m_position = { 0.0f, 0.0f };
    MPROPERTY(Serializable)   float m_rotation = 0.0f;            // 旋转角度（度，逆时针为正）
    MPROPERTY(Serializable)   Vec2  m_scale    = { 1.0f, 1.0f };
    MPROPERTY(Transient)      Vec2  m_velocity = { 0.0f, 0.0f };  // 运行时数据，不序列化
};