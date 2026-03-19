#include "RenderComponent.h"

// 场景启动时加载渲染资源（纹理、材质等）
void RenderComponent::OnStart()
{
    LoadResources();
}

// 场景停止时释放渲染资源，避免内存泄漏
void RenderComponent::OnStop()
{
    UnloadResources();
}

// 每帧渲染回调
// 检查可见性和启用状态后，提交渲染命令
// 当前为占位实现，实际项目中应调用 2D 渲染器的精灵批处理接口
void RenderComponent::OnRender()
{
    if (!m_visible || !IsEnabled()) return;
    // 实际项目中：提交 2D 渲染命令（精灵批处理）
    // 示例：Renderer2D::Get().DrawSprite(m_spriteName, transform->GetPosition(),
    //     transform->GetRotation(), transform->GetScale(), m_color);
    // 精灵批处理会将相同纹理的绘制调用合并，减少 GPU Draw Call
}

// 序列化为 JSON 字符串（包含精灵、材质、可见性、排序层、翻转、颜色等）
std::string RenderComponent::Serialize() const
{
    std::ostringstream oss;
    oss << "{"
        << "\"sprite\":\""   << m_spriteName   << "\","
        << "\"material\":\"" << m_materialName << "\","
        << "\"visible\":"    << (m_visible ? "true" : "false") << ","
        << "\"sortingLayer\":" << m_sortingLayer << ","
        << "\"orderInLayer\":" << m_orderInLayer << ","
        << "\"flipX\":"      << (m_flipX ? "true" : "false") << ","
        << "\"flipY\":"      << (m_flipY ? "true" : "false") << ","
        << "\"color\":["     << m_colorR << "," << m_colorG << ","
                             << m_colorB << "," << m_colorA << "]"
        << "}";
    return oss.str();
}

// 调试信息：输出精灵名称、排序层、可见性等关键状态
std::string RenderComponent::GetDebugInfo() const
{
    return "Sprite='" + m_spriteName + "'"
         + " Layer=" + std::to_string(m_sortingLayer)
         + " Order=" + std::to_string(m_orderInLayer)
         + " Visible=" + (m_visible ? "true" : "false");
}

// 加载渲染资源（纹理加载、Shader编译等）
// 当前为占位实现，实际项目中应从资源管理器加载
void RenderComponent::LoadResources()
{
    // 实际项目中：
    // m_texture = ResourceManager::Get().LoadTexture(m_spriteName);
    // m_shader  = ResourceManager::Get().LoadShader(m_materialName);
    m_resourcesLoaded = true;
}

// 释放渲染资源
// 实际项目中应减少纹理引用计数或从GPU释放
void RenderComponent::UnloadResources()
{
    // 实际项目中：
    // ResourceManager::Get().ReleaseTexture(m_spriteName);
    m_resourcesLoaded = false;
}