
#include "ChildObjectComponent.h"
#include "GameObject.h"
#include "Scene.h"
#include <algorithm>
#include <iostream>

// ============================================================
//  ChildObjectComponent 实现
//  子对象容器组件基类 — 参考 UE UChildActorComponent
// ============================================================

// --------------------------------------------------------
//  生命周期回调
// --------------------------------------------------------

void ChildObjectComponent::OnDetach()
{
    // 安全检查：如果场景不可用（场景析构中），只清空 ID 列表
    // 不尝试通过 Scene API 销毁子对象（Scene 析构时会自动清理所有对象）
    Scene* scene = GetOwnerScene();
    if (!scene || !scene->IsRunning())
    {
        m_childIDs.clear();
        return;
    }
    DestroyAllChildren();
}

void ChildObjectComponent::OnStop()
{
    // 默认空实现，子类可重写
}

// --------------------------------------------------------
//  子对象创建
// --------------------------------------------------------

GameObjectID ChildObjectComponent::CreateChild(const std::string& name,
                                                ChildInitializer initializer)
{
    // 安全检查：必须有所属 GameObject
    GameObject* owner = GetOwner();
    if (!owner)
    {
        std::cerr << "[ChildObjectComponent] 错误: 组件未附加到 GameObject\n";
        return INVALID_GAME_OBJECT_ID;
    }

    // 安全检查：必须有所属场景
    Scene* scene = GetOwnerScene();
    if (!scene)
    {
        std::cerr << "[ChildObjectComponent] 错误: 所属 GameObject 不在任何场景中\n";
        return INVALID_GAME_OBJECT_ID;
    }

    // 容量检查（如果设置了上限）
    if (m_maxChildren > 0 && m_childIDs.size() >= static_cast<size_t>(m_maxChildren))
    {
        std::cerr << "[ChildObjectComponent] 警告: 已达到子对象上限 ("
                  << m_maxChildren << ")\n";
        return INVALID_GAME_OBJECT_ID;
    }

    // 通过 Scene API 创建子对象
    GameObjectID childID = CreateChildInScene(scene, name);
    if (childID == INVALID_GAME_OBJECT_ID)
    {
        std::cerr << "[ChildObjectComponent] 错误: 创建子对象失败\n";
        return INVALID_GAME_OBJECT_ID;
    }

    // 建立父子关系（通过 Scene 的场景图管理）
    SetParentInScene(scene, childID, owner->GetID());

    // 记录到管理列表
    m_childIDs.push_back(childID);

    // 获取子对象指针，执行初始化回调
    GameObject* child = GetChildFromScene(scene, childID);
    if (child)
    {
        // 用户自定义初始化（添加组件、设置属性等）
        if (initializer)
            initializer(child);

        // 虚函数回调（子类可重写）
        OnChildCreated(child);
    }

    return childID;
}

std::vector<GameObjectID> ChildObjectComponent::CreateChildren(int count,
                                                                 const std::string& namePrefix,
                                                                 ChildInitializer initializer)
{
    std::vector<GameObjectID> result;
    result.reserve(count);
    for (int i = 0; i < count; ++i)
    {
        std::string name = namePrefix + "_" + std::to_string(i);
        GameObjectID id = CreateChild(name, initializer);
        if (id != INVALID_GAME_OBJECT_ID)
            result.push_back(id);
    }
    return result;
}

// --------------------------------------------------------
//  子对象销毁
// --------------------------------------------------------

bool ChildObjectComponent::DestroyChild(GameObjectID childID)
{
    // 检查是否是我们管理的子对象
    auto it = std::find(m_childIDs.begin(), m_childIDs.end(), childID);
    if (it == m_childIDs.end())
    {
        std::cerr << "[ChildObjectComponent] 警告: ID=" << childID
                  << " 不是本组件管理的子对象\n";
        return false;
    }

    Scene* scene = GetOwnerScene();
    if (!scene) return false;

    // 虚函数回调（子类可重写，在销毁前做清理）
    GameObject* child = GetChildFromScene(scene, childID);
    if (child)
        OnChildDestroyed(child);

    // 从管理列表中移除
    m_childIDs.erase(it);

    // 通过 Scene API 销毁对象（Scene 会递归处理子对象的子对象）
    return DestroyChildInScene(scene, childID);
}

void ChildObjectComponent::DestroyAllChildren()
{
    Scene* scene = GetOwnerScene();
    if (!scene)
    {
        m_childIDs.clear();
        return;
    }

    // 先拷贝列表（因为销毁过程可能间接修改 m_childIDs）
    // 参考 Scene 中 DestroyGameObjectImmediate 的安全模式
    std::vector<GameObjectID> copy = m_childIDs;
    m_childIDs.clear();  // 先清空，避免重复搜索

    for (GameObjectID id : copy)
    {
        // 虚函数回调
        GameObject* child = GetChildFromScene(scene, id);
        if (child)
            OnChildDestroyed(child);

        // 通过 Scene API 销毁
        DestroyChildInScene(scene, id);
    }
}

// --------------------------------------------------------
//  子对象查询
// --------------------------------------------------------

GameObject* ChildObjectComponent::GetChildAt(size_t index) const
{
    if (index >= m_childIDs.size()) return nullptr;
    Scene* scene = GetOwnerScene();
    if (!scene) return nullptr;
    return GetChildFromScene(scene, m_childIDs[index]);
}

GameObject* ChildObjectComponent::FindChildByName(const std::string& name) const
{
    Scene* scene = GetOwnerScene();
    if (!scene) return nullptr;

    for (GameObjectID id : m_childIDs)
    {
        GameObject* child = GetChildFromScene(scene, id);
        if (child && child->GetName() == name)
            return child;
    }
    return nullptr;
}

bool ChildObjectComponent::HasChild(GameObjectID id) const
{
    return std::find(m_childIDs.begin(), m_childIDs.end(), id) != m_childIDs.end();
}

void ChildObjectComponent::ForEachChild(const std::function<void(GameObject*)>& func) const
{
    Scene* scene = GetOwnerScene();
    if (!scene) return;

    // 快照遍历，回调中可安全调用 CreateChild/DestroyChild
    std::vector<GameObjectID> snapshot = m_childIDs;
    for (GameObjectID id : snapshot)
    {
        GameObject* child = GetChildFromScene(scene, id);
        if (child)
            func(child);
    }
}

// --------------------------------------------------------
//  可重写的虚函数钩子（默认空实现）
// --------------------------------------------------------

void ChildObjectComponent::OnChildCreated(GameObject* child)
{
    (void)child;
}

void ChildObjectComponent::OnChildDestroyed(GameObject* child)
{
    (void)child;
}

// --------------------------------------------------------
//  序列化 / 调试
// --------------------------------------------------------

std::string ChildObjectComponent::GetTypeName() const
{
    return "ChildObjectComponent";
}

std::string ChildObjectComponent::GetDebugInfo() const
{
    return "ChildCount=" + std::to_string(m_childIDs.size())
         + " MaxChildren=" + std::to_string(m_maxChildren);
}

std::string ChildObjectComponent::Serialize() const
{
    std::string result = "{ \"childCount\": " + std::to_string(m_childIDs.size());
    result += ", \"maxChildren\": " + std::to_string(m_maxChildren);
    result += ", \"childIDs\": [";
    for (size_t i = 0; i < m_childIDs.size(); ++i)
    {
        if (i > 0) result += ", ";
        result += std::to_string(m_childIDs[i]);
    }
    result += "] }";
    return result;
}

// --------------------------------------------------------
//  Scene 交互代理方法
//  通过 GameObject::GetScene() 间接访问 Scene API
//  隔离了对 Scene.h 的直接依赖
// --------------------------------------------------------

Scene* ChildObjectComponent::GetOwnerScene() const
{
    GameObject* owner = GetOwner();
    return owner ? owner->GetScene() : nullptr;
}

GameObjectID ChildObjectComponent::CreateChildInScene(Scene* scene, const std::string& name)
{
    return scene->CreateGameObject(name);
}

bool ChildObjectComponent::SetParentInScene(Scene* scene, GameObjectID childID, GameObjectID parentID)
{
    return scene->SetParent(childID, parentID);
}

GameObject* ChildObjectComponent::GetChildFromScene(Scene* scene, GameObjectID id) const
{
    return scene->GetGameObject(id);
}

bool ChildObjectComponent::DestroyChildInScene(Scene* scene, GameObjectID childID)
{
    return scene->DestroyGameObject(childID);
}
