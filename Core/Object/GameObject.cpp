#include "GameObject.h"
#include <iostream>

// ============================================================
//  GameObject 析构函数
//  按逆序调用 OnDetach，确保后添加的组件先被清理
//  （后添加的组件可能依赖先添加的组件，逆序保证依赖不断裂）
// ============================================================
GameObject::~GameObject()
{
    // 按逆序移除组件（保证依赖关系正确析构）
    // 例如：ScriptComponent 可能在 OnDetach 中引用 TransformComponent，
    //       如果 TransformComponent 先被销毁，ScriptComponent 就会访问悬空指针
    for (auto it = m_componentOrder.rbegin(); it != m_componentOrder.rend(); ++it)
    {
        auto found = m_components.find(*it);
        if (found != m_components.end())
        {
            found->second->OnDetach();
            found->second->ClearOwner();  // 修复 Bug#6：析构时清空 owner 引用，防止悬空指针
        }
    }
    m_components.clear();
    m_componentOrder.clear();
}

// ============================================================
//  父子关系
//  注意：这里只维护 GameObject 自身的子节点列表，
//  父子关系的一致性（双向绑定）由 Scene::SetParent 统一管理
// ============================================================
void GameObject::AddChildID(GameObjectID childID)
{
    m_childrenIDs.push_back(childID);
}

void GameObject::RemoveChildID(GameObjectID childID)
{
    auto it = std::find(m_childrenIDs.begin(), m_childrenIDs.end(), childID);
    if (it != m_childrenIDs.end())
        m_childrenIDs.erase(it);
}

// ============================================================
//  生命周期驱动
//  所有生命周期方法使用 IterateComponents（快照遍历），
//  确保回调中添加/移除组件不会破坏迭代器。
//  非活跃对象（m_active == false）跳过 Start/Update/FixedUpdate/Render，
//  但 Stop 不检查 m_active，确保所有组件都能正确清理。
// ============================================================
void GameObject::Start()
{
    if (!m_active) return;
    m_hasStarted = true;  // 标记为已启动，后续添加的组件会自动追赶 OnStart
    // 遍历所有组件，调用 OnStart（仅对启用的组件）
    IterateComponents([](Component* comp)
    {
        if (comp->IsEnabled()) comp->OnStart();
    });
}

void GameObject::Update(float deltaTime)
{
    if (!m_active) return;
    // 每帧更新：TransformComponent 积分位置，ScriptComponent 执行逻辑等
    IterateComponents([deltaTime](Component* comp)
    {
        if (comp->IsEnabled()) comp->OnUpdate(deltaTime);
    });
}

void GameObject::FixedUpdate(float fixedDeltaTime)
{
    if (!m_active) return;
    // 固定时间步长更新：PhysicsComponent 在此做速度积分和位置更新
    // 固定步长保证物理模拟的确定性（不受帧率波动影响）
    IterateComponents([fixedDeltaTime](Component* comp)
    {
        if (comp->IsEnabled()) comp->OnFixedUpdate(fixedDeltaTime);
    });
}

void GameObject::Render()
{
    if (!m_active) return;
    IterateComponents([](Component* comp)
    {
        if (comp->IsEnabled()) comp->OnRender();
    });
}

void GameObject::Stop()
{
    // Stop 不检查 m_active，确保所有组件都能收到停止通知并释放资源
    // 使用快照遍历保护，与 Start/Update/Render 保持一致
    IterateComponents([](Component* comp)
    {
        comp->OnStop();
    });
    m_hasStarted = false;  // 重置启动标志，下次 Start 时组件会重新收到 OnStart
}


// ============================================================
//  ForEachComponent —— 安全遍历（使用快照保护）
//  修复：原实现不使用快照，回调中添加/删除组件会导致未定义行为
//  现在复用 IterateComponents 的快照遍历机制，确保安全
// ============================================================
void GameObject::ForEachComponent(const std::function<void(Component*)>& func) const
{
    // 拷贝快照，避免遍历期间容器修改导致迭代器失效
    std::vector<ComponentTypeID> snapshot = m_componentOrder;

    for (auto typeID : snapshot)
    {
        auto it = m_components.find(typeID);
        if (it != m_components.end())
            func(it->second.get());
    }
}

// ============================================================
//  序列化
//  手动拼接 JSON 字符串（未使用第三方 JSON 库）
//  按 m_componentOrder 顺序输出组件，保证反序列化时顺序一致
// ============================================================
std::string GameObject::Serialize() const
{
    std::string result = "{\n";
    result += "  \"id\": " + std::to_string(m_id) + ",\n";
    result += "  \"name\": \"" + m_name + "\",\n";
    result += "  \"active\": " + std::string(m_active ? "true" : "false") + ",\n";
    result += "  \"tags\": \"" + m_tags.ToString() + "\",\n";
    result += "  \"layer\": " + std::to_string(m_layer) + ",\n";
    result += "  \"parentID\": " + std::to_string(m_parentID) + ",\n";
    result += "  \"components\": [\n";

    bool first = true;
    for (auto typeID : m_componentOrder)
    {
        auto it = m_components.find(typeID);
        if (it != m_components.end())
        {
            if (!first) result += ",\n";
            result += "    {\n";
            result += "      \"type\": \"" + it->second->GetTypeName() + "\",\n";
            result += "      \"enabled\": " + std::string(it->second->IsEnabled() ? "true" : "false") + ",\n";
            result += "      \"data\": " + it->second->Serialize() + "\n";
            result += "    }";
            first = false;
        }
    }
    result += "\n  ]\n}";
    return result;
}

// ============================================================
//  调试信息
//  输出对象的完整状态：ID、名称、标签、层级、组件列表等
//  便于开发阶段快速定位问题
// ============================================================
void GameObject::PrintDebugInfo() const
{
    std::cout << "[GameObject] ID=" << m_id
              << " Name='" << m_name << "'"
              << " Active=" << (m_active ? "true" : "false")
              << " Tags=" << m_tags.ToString()
              << " Layer=" << m_layer
              << " Components=" << m_components.size()
              << " Children=" << m_childrenIDs.size()
              << "\n";

    for (auto typeID : m_componentOrder)
    {
        auto it = m_components.find(typeID);
        if (it != m_components.end())
        {
            std::cout << "  [Component] TypeID=" << typeID
                      << " Type='" << it->second->GetTypeName() << "'"
                      << " Enabled=" << (it->second->IsEnabled() ? "true" : "false")
                      << " | " << it->second->GetDebugInfo() << "\n";
        }
    }
}

// ============================================================
//  内部工具：延迟操作
// ============================================================

// 检查某个组件类型是否在本帧遍历期间被标记为“待移除”
// 修复：使用 unordered_set 替代线性搜索，O(n) → O(1)
bool GameObject::IsPendingRemove(ComponentTypeID typeID) const
{
    return m_pendingComponentRemoveSet.count(typeID) > 0;
}

// 将遍历期间缓冲的增删操作一次性提交
// 执行顺序：先移除后添加（防止先添加后又立即移除的组件"闪现"一帧）
void GameObject::FlushPendingComponents()
{
    // 1. 先处理待移除（swap 技巧：清空原列表的同时获取副本，避免递归问题）
    std::vector<ComponentTypeID> toRemove;
    toRemove.swap(m_pendingComponentRemove);
    m_pendingComponentRemoveSet.clear();  // 同步清空 set
    for (ComponentTypeID typeID : toRemove)
        RemoveComponentImmediate(typeID);

    // 2. 再处理待添加（新组件从下一帧开始参与 Update/FixedUpdate/Render）
    std::vector<ComponentTypeID> toAdd;
    toAdd.swap(m_pendingComponentAdd);
    for (ComponentTypeID typeID : toAdd)
        m_componentOrder.push_back(typeID);
}

// 立即移除一个组件（非遍历期间调用）
// 步骤：OnDetach回调 → 从map中删除 → 从顺序列表中删除 → 清理待添加列表
bool GameObject::RemoveComponentImmediate(ComponentTypeID typeID)
{
    auto it = m_components.find(typeID);
    if (it == m_components.end()) return false;

    // 先调用 OnDetach 让组件有机会清理资源（如取消注册回调、释放纹理等）
    it->second->OnDetach();
    it->second->ClearOwner();  // 修复 Bug#6：移除时清空 owner 引用，防止悬空指针
    m_components.erase(it);

    // 从顺序列表中移除（保持 Update 顺序的一致性）
    auto orderIt = std::find(m_componentOrder.begin(), m_componentOrder.end(), typeID);
    if (orderIt != m_componentOrder.end())
        m_componentOrder.erase(orderIt);

    // 同时从待添加队列中清除（防止先 Add 后 Remove 的情况：
    // 组件在遍历中被添加到 pending，随后又被 Remove，
    // 如果不清理 pending，FlushPendingComponents 会尝试添加一个已删除的组件）
    auto pendIt = std::find(m_pendingComponentAdd.begin(), m_pendingComponentAdd.end(), typeID);
    if (pendIt != m_pendingComponentAdd.end())
        m_pendingComponentAdd.erase(pendIt);

    return true;
}