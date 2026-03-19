#pragma once
#include "Component.h"
#include "CoreTypes.h"
#include "TransformComponent.h"
#include <functional>
#include <string>
#include <sstream>
#include <iomanip>

// ============================================================
//  碰撞信息（每次碰撞事件携带的数据）
//  由 CollisionSystem 生成，传递给 ColliderComponent 和 ScriptComponent 的回调
// ============================================================
struct CollisionInfo
{
    GameObjectID otherID       = INVALID_GAME_OBJECT_ID;  // 碰撞对方的 ID
    Vec2         contactPoint  = { 0, 0 };   // 接触点（世界坐标）
    Vec2         contactNormal = { 0, 1 };   // 碰撞法线（从对方指向自身）
    float        penetration   = 0.0f;       // 穿透深度（两物体重叠的距离）
    bool         isTrigger     = false;       // 是否为触发器碰撞
};

// ============================================================
//  ColliderComponent（2D 碰撞体组件）
//  支持形状：Box（矩形） / Circle（圆） / Capsule（胶囊）
//  功能：
//    - 碰撞检测（与 CollisionSystem 配合）
//    - 层级过滤（Layer + Mask 控制哪些碰撞体之间可以碰撞）
//    - 触发器模式（不产生物理响应，只触发事件）
//    - 碰撞回调（Enter/Stay/Exit 三种状态）
// ============================================================
class ColliderComponent : public Component
{
public:
    enum class Shape { Box, Circle, Capsule };

    using LayerMask = uint32_t;
    static constexpr LayerMask LAYER_DEFAULT    = (1u << 0);
    static constexpr LayerMask LAYER_PLAYER     = (1u << 1);
    static constexpr LayerMask LAYER_ENEMY      = (1u << 2);
    static constexpr LayerMask LAYER_PROJECTILE = (1u << 3);
    static constexpr LayerMask LAYER_TERRAIN    = (1u << 4);
    static constexpr LayerMask LAYER_TRIGGER    = (1u << 5);
    static constexpr LayerMask LAYER_ALL        = 0xFFFFFFFF;

    using CollisionCallback = std::function<void(const CollisionInfo&)>;

    explicit ColliderComponent(Shape shape = Shape::Box) : m_shape(shape) {}
    ~ColliderComponent() override = default;

    // --------------------------------------------------------
    //  生命周期
    // --------------------------------------------------------
    void OnAttach() override {}
    void OnDetach() override
    {
        // 清空所有回调，防止捕获的外部指针变成悬空引用
        m_onCollisionEnter = nullptr;
        m_onCollisionStay  = nullptr;
        m_onCollisionExit  = nullptr;
        m_onTriggerEnter   = nullptr;
        m_onTriggerStay    = nullptr;
        m_onTriggerExit    = nullptr;
    }


    // --------------------------------------------------------
    //  形状
    // --------------------------------------------------------
    void  SetShape(Shape shape) { m_shape = shape; }
    Shape GetShape() const      { return m_shape; }

    void        SetBoxHalfExtents(float hx, float hy) { m_halfExtents = { hx, hy }; }
    void        SetBoxHalfExtents(const Vec2& he)     { m_halfExtents = he; }
    const Vec2& GetBoxHalfExtents() const             { return m_halfExtents; }

    void  SetCircleRadius(float r) { m_radius = (r > 0.0f) ? r : 0.001f; }
    float GetCircleRadius() const  { return m_radius; }

    void  SetCapsuleRadius(float r)     { m_radius = (r > 0.0f) ? r : 0.001f; }
    void  SetCapsuleHalfHeight(float h) { m_capsuleHalfHeight = (h > 0.0f) ? h : 0.001f; }
    float GetCapsuleHalfHeight() const  { return m_capsuleHalfHeight; }

    void        SetCenter(float x, float y) { m_center = { x, y }; }
    void        SetCenter(const Vec2& c)    { m_center = c; }
    const Vec2& GetCenter() const           { return m_center; }

    // 获取世界坐标中心
    Vec2 GetWorldCenter() const;

    // --------------------------------------------------------
    //  触发器模式
    // --------------------------------------------------------
    void SetTrigger(bool isTrigger) { m_isTrigger = isTrigger; }
    bool IsTrigger() const          { return m_isTrigger; }

    // --------------------------------------------------------
    //  碰撞层
    // --------------------------------------------------------
    void      SetLayer(LayerMask layer)        { m_layer = layer; }
    LayerMask GetLayer() const                 { return m_layer; }

    void      SetCollisionMask(LayerMask mask) { m_collisionMask = mask; }
    LayerMask GetCollisionMask() const         { return m_collisionMask; }

    bool CanCollideWith(const ColliderComponent* other) const
    {
        if (!other) return false;
        return (m_collisionMask & other->m_layer) != 0;
    }

    // --------------------------------------------------------
    //  碰撞回调注册
    // --------------------------------------------------------
    void SetOnCollisionEnter(CollisionCallback cb) { m_onCollisionEnter = std::move(cb); }
    void SetOnCollisionStay(CollisionCallback cb)  { m_onCollisionStay  = std::move(cb); }
    void SetOnCollisionExit(CollisionCallback cb)  { m_onCollisionExit  = std::move(cb); }
    void SetOnTriggerEnter(CollisionCallback cb)   { m_onTriggerEnter   = std::move(cb); }
    void SetOnTriggerStay(CollisionCallback cb)    { m_onTriggerStay    = std::move(cb); }
    void SetOnTriggerExit(CollisionCallback cb)    { m_onTriggerExit    = std::move(cb); }

    // --------------------------------------------------------
    //  回调触发（由 CollisionSystem 调用）
    // --------------------------------------------------------
    void FireCollisionEnter(const CollisionInfo& info) const { if (m_onCollisionEnter) m_onCollisionEnter(info); }
    void FireCollisionStay(const CollisionInfo& info)  const { if (m_onCollisionStay)  m_onCollisionStay(info);  }
    void FireCollisionExit(const CollisionInfo& info)  const { if (m_onCollisionExit)  m_onCollisionExit(info);  }
    void FireTriggerEnter(const CollisionInfo& info)   const { if (m_onTriggerEnter)   m_onTriggerEnter(info);   }
    void FireTriggerStay(const CollisionInfo& info)    const { if (m_onTriggerStay)    m_onTriggerStay(info);    }
    void FireTriggerExit(const CollisionInfo& info)    const { if (m_onTriggerExit)    m_onTriggerExit(info);    }

    // --------------------------------------------------------
    //  序列化 / 调试
    // --------------------------------------------------------
    std::string Serialize()    const override;
    std::string GetTypeName()  const override { return "ColliderComponent"; }
    std::string GetDebugInfo() const override;

private:
    Shape m_shape             = Shape::Box;
    Vec2  m_halfExtents       = { 0.5f, 0.5f };
    float m_radius            = 0.5f;
    float m_capsuleHalfHeight = 1.0f;
    Vec2  m_center            = { 0.0f, 0.0f };

    bool      m_isTrigger     = false;
    LayerMask m_layer         = LAYER_DEFAULT;
    LayerMask m_collisionMask = LAYER_ALL;

    CollisionCallback m_onCollisionEnter;
    CollisionCallback m_onCollisionStay;
    CollisionCallback m_onCollisionExit;
    CollisionCallback m_onTriggerEnter;
    CollisionCallback m_onTriggerStay;
    CollisionCallback m_onTriggerExit;
};
