#pragma once
#include "Component.h"
#include <string>
#include <sstream>

// ============================================================
//  RenderComponent（2D 渲染组件）
//  负责：精灵/纹理、颜色、可见性、渲染排序层
// ============================================================
class RenderComponent : public Component
{
public:
    RenderComponent() = default;
    ~RenderComponent() override = default;

    // --------------------------------------------------------
    //  生命周期
    // --------------------------------------------------------
    void OnAttach() override {}
    void OnDetach() override {}
    void OnStart()  override;
    void OnStop()   override;
    void OnRender() override;

    // --------------------------------------------------------
    //  可见性
    // --------------------------------------------------------
    void SetVisible(bool visible) { m_visible = visible; }
    bool IsVisible() const        { return m_visible; }

    // --------------------------------------------------------
    //  渲染排序层
    // --------------------------------------------------------
    void SetSortingLayer(int layer) { m_sortingLayer = layer; }
    int  GetSortingLayer() const    { return m_sortingLayer; }

    void SetOrderInLayer(int order) { m_orderInLayer = order; }
    int  GetOrderInLayer() const    { return m_orderInLayer; }

    int GetSortKey() const { return m_sortingLayer * 10000 + m_orderInLayer; }

    // --------------------------------------------------------
    //  颜色（RGBA）
    // --------------------------------------------------------
    void SetColor(float r, float g, float b, float a = 1.0f)
    {
        m_colorR = r; m_colorG = g; m_colorB = b; m_colorA = a;
    }
    void  SetAlpha(float a) { m_colorA = a; }
    float GetAlpha() const  { return m_colorA; }

    // --------------------------------------------------------
    //  翻转（2D 精灵常用）
    // --------------------------------------------------------
    void SetFlipX(bool flip) { m_flipX = flip; }
    bool GetFlipX() const    { return m_flipX; }

    void SetFlipY(bool flip) { m_flipY = flip; }
    bool GetFlipY() const    { return m_flipY; }

    // --------------------------------------------------------
    //  序列化 / 调试
    // --------------------------------------------------------
    std::string Serialize()    const override;
    std::string GetTypeName()  const override { return "RenderComponent"; }
    std::string GetDebugInfo() const override;

private:
    void LoadResources();
    void UnloadResources();

private:
    std::string m_spriteName;
    std::string m_materialName;
    bool        m_visible       = true;
    int         m_sortingLayer  = 0;
    int         m_orderInLayer  = 0;
    bool        m_flipX         = false;
    bool        m_flipY         = false;
    float       m_colorR = 1.0f, m_colorG = 1.0f, m_colorB = 1.0f, m_colorA = 1.0f;
    bool        m_resourcesLoaded = false;
};