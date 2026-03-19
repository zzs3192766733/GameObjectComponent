#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <algorithm>
#include <sstream>
#include <iostream>
#include <cassert>

// ============================================================
//  GameplayTag 系统
//  参考 UE 的 FGameplayTag / FGameplayTagContainer / UGameplayTagsManager 设计
//
//  核心设计思想：
//  1. 层级标签：用点号分隔层级，如 "Enemy.Boss.Fire"
//     - "Enemy.Boss.Fire".MatchesTag("Enemy") → true（子标签匹配父标签）
//     - "Enemy".MatchesTag("Enemy.Boss.Fire") → false（父标签不匹配子标签）
//  2. ID 化：所有标签字符串在 Manager 中注册为唯一 uint32_t ID
//     运行时比较走 ID（O(1)），不做字符串比较
//  3. 父标签预缓存：注册时预计算所有父标签 ID，MatchesTag 只需查表
//  4. 容器化：GameplayTagContainer 持有多个标签，支持 HasTag/HasAll/HasAny 批量查询
//
//  使用示例：
//    auto& mgr = GameplayTagManager::Get();
//    GameplayTag enemy     = mgr.RequestTag("Enemy");
//    GameplayTag bossFire  = mgr.RequestTag("Enemy.Boss.Fire");
//    bossFire.MatchesTag(enemy);  // true
//    enemy.MatchesTag(bossFire);  // false
// ============================================================

// 标签内部 ID 类型（0 = 无效标签）
using TagID = uint32_t;
constexpr TagID INVALID_TAG_ID = 0;

// 前向声明
class GameplayTagManager;

// ============================================================
//  GameplayTag（轻量值类型，仅持有一个 ID）
//  对标 UE 的 FGameplayTag
//  - 大小仅 4 字节，可自由拷贝/传值
//  - 所有语义查询委托给 GameplayTagManager
// ============================================================
class GameplayTag
{
public:
    // 默认构造：无效标签
    GameplayTag() : m_id(INVALID_TAG_ID) {}

    // 是否有效
    bool IsValid() const { return m_id != INVALID_TAG_ID; }

    // 获取内部 ID（用于哈希/序列化）
    TagID GetID() const { return m_id; }

    // 获取标签全名（如 "Enemy.Boss.Fire"），通过 Manager 反查
    const std::string& GetTagName() const;

    // --------------------------------------------------------
    //  匹配查询（核心 API）
    // --------------------------------------------------------

    // 层级匹配：检查 this 是否"属于" other 的子标签（或完全相等）
    // "Enemy.Boss.Fire".MatchesTag("Enemy")       → true
    // "Enemy.Boss.Fire".MatchesTag("Enemy.Boss")   → true
    // "Enemy".MatchesTag("Enemy.Boss.Fire")        → false
    // 任一方无效 → false
    bool MatchesTag(const GameplayTag& other) const;

    // 精确匹配：仅当两者完全相同才返回 true
    bool MatchesTagExact(const GameplayTag& other) const
    {
        return IsValid() && other.IsValid() && m_id == other.m_id;
    }

    // 获取直接父标签（"Enemy.Boss.Fire" → "Enemy.Boss"）
    GameplayTag RequestDirectParent() const;

    // 获取所有父标签（包括自身）
    // "Enemy.Boss.Fire" → { "Enemy.Boss.Fire", "Enemy.Boss", "Enemy" }
    std::vector<GameplayTag> GetParentTags() const;

    // --------------------------------------------------------
    //  运算符
    // --------------------------------------------------------
    bool operator==(const GameplayTag& other) const { return m_id == other.m_id; }
    bool operator!=(const GameplayTag& other) const { return m_id != other.m_id; }
    bool operator<(const GameplayTag& other)  const { return m_id < other.m_id; }

    // 用于序列化/调试
    std::string ToString() const;

private:
    // 仅 Manager 可以通过 ID 构造
    explicit GameplayTag(TagID id) : m_id(id) {}

    TagID m_id;

    friend class GameplayTagManager;
};

// 哈希支持（用于 unordered_set / unordered_map）
struct GameplayTagHash
{
    size_t operator()(const GameplayTag& tag) const
    {
        return std::hash<TagID>{}(tag.GetID());
    }
};

// ============================================================
//  GameplayTagContainer（标签容器，持有多个标签）
//  对标 UE 的 FGameplayTagContainer
//  - 内部用 unordered_set 存储标签 ID，查询 O(1)
//  - 支持 HasTag（层级匹配）、HasTagExact（精确匹配）
//  - 支持 HasAll / HasAny 批量查询
// ============================================================
class GameplayTagContainer
{
public:
    GameplayTagContainer() = default;

    // 从单个标签构造
    explicit GameplayTagContainer(const GameplayTag& tag)
    {
        AddTag(tag);
    }

    // 从初始化列表构造
    GameplayTagContainer(std::initializer_list<GameplayTag> tags)
    {
        for (const auto& tag : tags)
            AddTag(tag);
    }

    // --------------------------------------------------------
    //  增删操作
    // --------------------------------------------------------

    // 添加标签（同时缓存其所有父标签 ID，加速 HasTag 层级查询）
    void AddTag(const GameplayTag& tag);

    // 移除标签
    void RemoveTag(const GameplayTag& tag);

    // 批量追加另一个容器的所有标签
    void AppendTags(const GameplayTagContainer& other);

    // 清空所有标签
    void Reset();

    // --------------------------------------------------------
    //  查询操作
    // --------------------------------------------------------

    // 层级匹配：容器中是否有标签匹配 tag（包括子标签）
    // 例如容器有 "Enemy.Boss.Fire"，HasTag("Enemy") → true
    bool HasTag(const GameplayTag& tag) const;

    // 精确匹配：容器中是否恰好有这个标签
    bool HasTagExact(const GameplayTag& tag) const;

    // 是否拥有 other 中的所有标签（层级匹配）
    // other 为空 → 返回 true（空集是任何集合的子集）
    bool HasAll(const GameplayTagContainer& other) const;

    // 是否拥有 other 中的所有标签（精确匹配）
    bool HasAllExact(const GameplayTagContainer& other) const;

    // 是否拥有 other 中的任一标签（层级匹配）
    // other 为空 → 返回 false
    bool HasAny(const GameplayTagContainer& other) const;

    // 是否拥有 other 中的任一标签（精确匹配）
    bool HasAnyExact(const GameplayTagContainer& other) const;

    // --------------------------------------------------------
    //  信息查询
    // --------------------------------------------------------

    // 显式标签数量（不含缓存的父标签）
    size_t Num() const { return m_explicitTags.size(); }

    // 是否为空
    bool IsEmpty() const { return m_explicitTags.empty(); }

    // 获取所有显式标签（用于遍历/序列化）
    const std::unordered_set<TagID>& GetExplicitTags() const { return m_explicitTags; }

    // --------------------------------------------------------
    //  运算符
    // --------------------------------------------------------
    bool operator==(const GameplayTagContainer& other) const
    {
        return m_explicitTags == other.m_explicitTags;
    }

    bool operator!=(const GameplayTagContainer& other) const
    {
        return !(*this == other);
    }

    // --------------------------------------------------------
    //  迭代支持
    // --------------------------------------------------------
    using Iterator = std::unordered_set<TagID>::const_iterator;
    Iterator begin() const { return m_explicitTags.begin(); }
    Iterator end()   const { return m_explicitTags.end(); }

    // --------------------------------------------------------
    //  调试
    // --------------------------------------------------------
    std::string ToString() const;

private:
    // 用户显式添加的标签 ID 集合
    std::unordered_set<TagID> m_explicitTags;

    // 父标签缓存：包含所有显式标签及其父标签的 ID
    // 用于 HasTag 的 O(1) 层级匹配查询
    // 例如添加 "Enemy.Boss.Fire" 后，此集合包含：
    //   { ID("Enemy.Boss.Fire"), ID("Enemy.Boss"), ID("Enemy") }
    std::unordered_set<TagID> m_parentTagCache;

    // 重建父标签缓存（在增删操作后调用）
    void RebuildParentCache();
};

// ============================================================
//  GameplayTagManager（全局单例，标签注册中心）
//  对标 UE 的 UGameplayTagsManager
//  - 所有标签字符串在这里注册为唯一 ID
//  - 注册时预计算并缓存父标签关系
//  - 运行时所有查询通过 ID 完成，不做字符串操作
//
//  使用方式：
//    auto& mgr = GameplayTagManager::Get();
//    GameplayTag tag = mgr.RequestTag("Enemy.Boss.Fire");
// ============================================================
class GameplayTagManager
{
public:
    // 获取全局唯一实例
    static GameplayTagManager& Get()
    {
        static GameplayTagManager instance;
        return instance;
    }

    // --------------------------------------------------------
    //  标签注册与请求
    // --------------------------------------------------------

    // 请求一个标签（如果不存在则自动注册）
    // 这是获取 GameplayTag 的唯一入口
    // tagName 格式："Category.SubCategory.Name"
    // 会自动注册所有中间层级的父标签
    GameplayTag RequestTag(const std::string& tagName)
    {
        if (tagName.empty())
            return GameplayTag();

        // 验证标签格式是否合法（防止注册 "..Enemy" 或 "Enemy..Boss" 等非法格式）
        if (!IsValidTagString(tagName))
        {
            std::cerr << "[GameplayTagManager] 警告: 标签格式非法: '" << tagName << "'，已忽略\n";
            return GameplayTag();
        }

        // 已注册 → 直接返回
        auto it = m_nameToID.find(tagName);
        if (it != m_nameToID.end())
            return GameplayTag(it->second);

        // 首次注册：先注册所有父标签（确保父标签 ID 存在）
        RegisterTagHierarchy(tagName);

        return GameplayTag(m_nameToID[tagName]);
    }

    // 查找标签（不自动注册，找不到返回无效标签）
    GameplayTag FindTag(const std::string& tagName) const
    {
        auto it = m_nameToID.find(tagName);
        if (it != m_nameToID.end())
            return GameplayTag(it->second);
        return GameplayTag();
    }

    // 验证标签字符串格式是否合法
    // 合法格式：由字母、数字、下划线组成的段，用点号分隔
    static bool IsValidTagString(const std::string& tagString)
    {
        if (tagString.empty()) return false;
        if (tagString.front() == '.' || tagString.back() == '.') return false;

        bool lastWasDot = false;
        for (char c : tagString)
        {
            if (c == '.')
            {
                if (lastWasDot) return false; // 连续点号
                lastWasDot = true;
            }
            else
            {
                if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                    return false;
                lastWasDot = false;
            }
        }
        return true;
    }

    // --------------------------------------------------------
    //  ID ? 名称 互查
    // --------------------------------------------------------

    // 通过 ID 获取标签全名
    const std::string& GetTagName(TagID id) const
    {
        auto it = m_idToName.find(id);
        if (it != m_idToName.end())
            return it->second;
        static const std::string empty;
        return empty;
    }

    // 通过 ID 获取 GameplayTag
    GameplayTag GetTagByID(TagID id) const
    {
        if (m_idToName.count(id))
            return GameplayTag(id);
        return GameplayTag();
    }

    // --------------------------------------------------------
    //  层级关系查询
    // --------------------------------------------------------

    // 获取标签的所有父标签 ID（包括自身）
    // "Enemy.Boss.Fire" → { ID("Enemy.Boss.Fire"), ID("Enemy.Boss"), ID("Enemy") }
    const std::vector<TagID>& GetParentTagIDs(TagID id) const
    {
        auto it = m_parentTags.find(id);
        if (it != m_parentTags.end())
            return it->second;
        static const std::vector<TagID> empty;
        return empty;
    }

    // 获取标签的直接父标签 ID
    TagID GetDirectParentID(TagID id) const
    {
        auto it = m_directParent.find(id);
        if (it != m_directParent.end())
            return it->second;
        return INVALID_TAG_ID;
    }

    // MatchesTag 核心逻辑（由 GameplayTag::MatchesTag 委托调用）
    // 检查 childID 的标签是否"属于" parentID 的层级
    // 即 childID 的父标签链中是否包含 parentID
    bool TagMatchesTag(TagID childID, TagID parentID) const
    {
        if (childID == INVALID_TAG_ID || parentID == INVALID_TAG_ID)
            return false;

        // 完全相等
        if (childID == parentID)
            return true;

        // 检查 childID 的父标签缓存中是否包含 parentID
        auto it = m_parentTags.find(childID);
        if (it != m_parentTags.end())
        {
            for (TagID pid : it->second)
            {
                if (pid == parentID)
                    return true;
            }
        }
        return false;
    }

    // --------------------------------------------------------
    //  批量注册（游戏启动时调用）
    // --------------------------------------------------------

    // 批量注册标签列表
    void RegisterTags(const std::vector<std::string>& tagNames)
    {
        for (const auto& name : tagNames)
            RequestTag(name);
    }

    // --------------------------------------------------------
    //  调试
    // --------------------------------------------------------

    // 获取已注册的标签总数
    size_t GetRegisteredTagCount() const { return m_nameToID.size(); }

    // 打印所有已注册的标签及其层级关系
    void PrintAllTags() const
    {
        std::cout << "=== GameplayTagManager: " << m_nameToID.size() << " 个已注册标签 ===\n";

        // 按名称排序输出
        std::vector<std::pair<std::string, TagID>> sorted(m_nameToID.begin(), m_nameToID.end());
        std::sort(sorted.begin(), sorted.end());

        for (const auto& [name, id] : sorted)
        {
            std::cout << "  [" << id << "] " << name;
            // 输出父标签链
            auto pit = m_directParent.find(id);
            if (pit != m_directParent.end() && pit->second != INVALID_TAG_ID)
                std::cout << "  (parent: " << GetTagName(pit->second) << ")";
            std::cout << "\n";
        }
    }

    // 重置管理器（主要用于测试）
    // 修复 #4：同时重置 NativeGameplayTag 链表中所有节点的 initialized 标志
    // 这样 Reset() 后再调用 InitializeNativeTags() 时，所有原生标签会重新解析
    // 实现放在 NativeGameplayTag 定义之后（避免不完整类型错误）
    inline void Reset();

private:
    GameplayTagManager() = default;
    GameplayTagManager(const GameplayTagManager&) = delete;
    GameplayTagManager& operator=(const GameplayTagManager&) = delete;

    // 注册一个标签及其所有父标签
    void RegisterTagHierarchy(const std::string& tagName)
    {
        // 解析层级："Enemy.Boss.Fire" → ["Enemy", "Enemy.Boss", "Enemy.Boss.Fire"]
        std::vector<std::string> hierarchy;
        std::string current;
        for (size_t i = 0; i < tagName.size(); ++i)
        {
            current += tagName[i];
            if (tagName[i] == '.' || i == tagName.size() - 1)
            {
                // 去除末尾的点号
                std::string segment = current;
                if (segment.back() == '.')
                    segment.pop_back();
                if (!segment.empty())
                    hierarchy.push_back(segment);
            }
        }

        // 逐级注册
        TagID parentID = INVALID_TAG_ID;
        for (const auto& h : hierarchy)
        {
            TagID id;
            auto it = m_nameToID.find(h);
            if (it == m_nameToID.end())
            {
                // 新标签，分配 ID
                id = m_nextID++;
                m_nameToID[h] = id;
                m_idToName[id] = h;
            }
            else
            {
                id = it->second;
            }

            // 设置直接父标签
            if (m_directParent.find(id) == m_directParent.end())
                m_directParent[id] = parentID;

            // 构建父标签链（包括自身）
            if (m_parentTags.find(id) == m_parentTags.end())
            {
                std::vector<TagID> parents;
                parents.push_back(id); // 包含自身
                if (parentID != INVALID_TAG_ID)
                {
                    // 继承父标签的所有祖先
                    auto pit = m_parentTags.find(parentID);
                    if (pit != m_parentTags.end())
                    {
                        for (TagID ancestorID : pit->second)
                            parents.push_back(ancestorID);
                    }
                }
                m_parentTags[id] = std::move(parents);
            }

            parentID = id;
        }
    }

    // 名称 → ID
    std::unordered_map<std::string, TagID> m_nameToID;

    // ID → 名称
    std::unordered_map<TagID, std::string> m_idToName;

    // ID → 父标签链（包含自身，从自身到最顶层祖先）
    // 例如 "Enemy.Boss.Fire" → { ID(Fire), ID(Boss), ID(Enemy) }
    std::unordered_map<TagID, std::vector<TagID>> m_parentTags;

    // ID → 直接父标签 ID（"Enemy.Boss.Fire" → ID("Enemy.Boss")）
    std::unordered_map<TagID, TagID> m_directParent;

    // 下一个可用 ID
    TagID m_nextID = 1;

    // NativeGameplayTag 自注册链表头指针
    // 所有通过 DEFINE_GAMEPLAY_TAG 宏创建的 NativeGameplayTag 会在
    // 全局构造阶段自动链入此链表，InitializeNativeTags() 统一注册
    friend class NativeGameplayTag;
    class NativeGameplayTag* m_nativeTagHead = nullptr;

public:
    // 注册原生头指针（由 NativeGameplayTag 构造函数调用）
    void RegisterNativeTagNode(class NativeGameplayTag* node);

    // 初始化所有原生标签（在 main 开始时或首次使用前调用）
    // 遍历链表将所有 NativeGameplayTag 注册到 Manager
    void InitializeNativeTags();
};

// ============================================================
//  GameplayTag 内联实现（依赖 GameplayTagManager）
// ============================================================

inline const std::string& GameplayTag::GetTagName() const
{
    return GameplayTagManager::Get().GetTagName(m_id);
}

inline bool GameplayTag::MatchesTag(const GameplayTag& other) const
{
    return GameplayTagManager::Get().TagMatchesTag(m_id, other.m_id);
}

inline GameplayTag GameplayTag::RequestDirectParent() const
{
    TagID parentID = GameplayTagManager::Get().GetDirectParentID(m_id);
    return GameplayTagManager::Get().GetTagByID(parentID);
}

inline std::vector<GameplayTag> GameplayTag::GetParentTags() const
{
    std::vector<GameplayTag> result;
    const auto& parentIDs = GameplayTagManager::Get().GetParentTagIDs(m_id);
    for (TagID pid : parentIDs)
        result.push_back(GameplayTagManager::Get().GetTagByID(pid));
    return result;
}

inline std::string GameplayTag::ToString() const
{
    if (!IsValid()) return "<Invalid>";
    return GetTagName();
}

// ============================================================
//  NativeGameplayTag（原生标签自注册器）
//  对标 UE 的 FNativeGameplayTag
//
//  核心思想：利用 C++ 全局/静态变量的构造函数，在 main() 之前
//  自动将标签注册到 GameplayTagManager 的链表中。
//  随后在 InitializeNativeTags() 中统一完成真正的注册。
//
//  使用方式（通过宏）：
//    // 头文件中声明（跨编译单元可见）
//    DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Player);
//
//    // 源文件中定义（自动注册）
//    DEFINE_GAMEPLAY_TAG(TAG_Character_Player, "Character.Player");
//
//    // 仅在本文件内使用的局部标签
//    DEFINE_GAMEPLAY_TAG_STATIC(TAG_Internal_Debug, "Debug.Internal");
//
//    // 带注释的标签（注释仅用于文档，不影响运行时）
//    DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Enemy, "Character.Enemy",
//                                "敌人角色标签，所有敌人的基础标签");
// ============================================================
class NativeGameplayTag
{
public:
    // 构造函数：在全局构造阶段将自身链入 Manager 的链表
    NativeGameplayTag(const char* tagName, const char* comment = "")
        : m_tagName(tagName)
        , m_comment(comment)
        , m_next(nullptr)
        , m_initialized(false)
    {
        // 将自身链入 Manager 的原生标签链表
        // 注意：此时 Manager 的单例已可用（static local 保证初始化顺序）
        GameplayTagManager::Get().RegisterNativeTagNode(this);
    }

    // 隐式转换为 GameplayTag（惰性初始化）
    operator GameplayTag() const
    {
        EnsureInitialized();
        return m_resolvedTag;
    }

    // 获取底层 GameplayTag
    const GameplayTag& GetTag() const
    {
        EnsureInitialized();
        return m_resolvedTag;
    }

    // 直接暴露常用查询 API，避免用户需要先转换
    bool IsValid() const { return GetTag().IsValid(); }
    TagID GetID() const { return GetTag().GetID(); }
    const std::string& GetTagName() const { return GetTag().GetTagName(); }
    bool MatchesTag(const GameplayTag& other) const { return GetTag().MatchesTag(other); }
    bool MatchesTagExact(const GameplayTag& other) const { return GetTag().MatchesTagExact(other); }

    // 比较运算符（支持与 GameplayTag 互相比较）
    bool operator==(const GameplayTag& other) const { return GetTag() == other; }
    bool operator!=(const GameplayTag& other) const { return GetTag() != other; }
    bool operator==(const NativeGameplayTag& other) const { return GetTag() == other.GetTag(); }
    bool operator!=(const NativeGameplayTag& other) const { return GetTag() != other.GetTag(); }

    // 调试信息
    const char* GetNativeName() const { return m_tagName; }
    const char* GetComment() const { return m_comment; }

private:
    // 确保标签已解析（惰性初始化 + 线程安全考虑预留）
    void EnsureInitialized() const
    {
        if (!m_initialized)
        {
            // 尝试从 Manager 解析标签
            m_resolvedTag = GameplayTagManager::Get().RequestTag(m_tagName);
            m_initialized = true;
        }
    }

    const char* m_tagName;               // 标签名称字符串（编译期常量）
    const char* m_comment;               // 注释说明
    mutable GameplayTag m_resolvedTag;   // 解析后的标签（惰性填充）
    mutable bool m_initialized;          // 是否已完成解析

    // 链表指针（Manager 维护的单链表）
    NativeGameplayTag* m_next;

    friend class GameplayTagManager;
};

// ============================================================
//  GameplayTagManager 原生标签支持的内联实现
// ============================================================

inline void GameplayTagManager::RegisterNativeTagNode(NativeGameplayTag* node)
{
    // 头插法链入链表（全局构造阶段调用，单线程安全）
    node->m_next = m_nativeTagHead;
    m_nativeTagHead = node;
}

inline void GameplayTagManager::InitializeNativeTags()
{
    // 遍历链表，将所有原生标签注册到 Manager
    NativeGameplayTag* current = m_nativeTagHead;
    int count = 0;
    while (current)
    {
        // 调用 RequestTag 完成真正的注册
        current->m_resolvedTag = RequestTag(current->m_tagName);
        current->m_initialized = true;
        ++count;
        current = current->m_next;
    }
    if (count > 0)
    {
        std::cout << "[GameplayTagManager] 已初始化 " << count << " 个原生标签\n";
    }
}

// 修复 #4：Reset 实现放在 NativeGameplayTag 完整定义之后
inline void GameplayTagManager::Reset()
{
    // 先重置所有 NativeGameplayTag 的 initialized 标志
    NativeGameplayTag* current = m_nativeTagHead;
    while (current)
    {
        current->m_initialized = false;
        current->m_resolvedTag = GameplayTag();  // 重置为无效标签
        current = current->m_next;
    }

    m_nameToID.clear();
    m_idToName.clear();
    m_parentTags.clear();
    m_directParent.clear();
    m_nextID = 1;
}

// ============================================================
//  声明式标签宏（对标 UE 的 UE_DEFINE_GAMEPLAY_TAG 系列）
//
//  用法示例：
//
//  === 跨编译单元共享 ===
//
//  // GameTags.h（头文件中声明）
//  #pragma once
//  #include "GameplayTag.h"
//  DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Player);
//  DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Enemy);
//
//  // GameTags.cpp（源文件中定义）
//  #include "GameTags.h"
//  DEFINE_GAMEPLAY_TAG(TAG_Character_Player, "Character.Player");
//  DEFINE_GAMEPLAY_TAG(TAG_Character_Enemy, "Character.Enemy");
//
//  // 任意 .cpp 中使用
//  #include "GameTags.h"
//  if (myTag.MatchesTag(TAG_Character_Player)) { ... }
//
//  === 仅在本文件内使用 ===
//
//  // MySystem.cpp
//  DEFINE_GAMEPLAY_TAG_STATIC(TAG_Internal, "Debug.Internal");
//
// ============================================================

// 声明 extern 原生标签变量（放在 .h 中，供其他编译单元引用）
#define DECLARE_GAMEPLAY_TAG_EXTERN(TagName) \
    extern NativeGameplayTag TagName

// 定义并注册原生标签（放在 .cpp 中，全局可见）
#define DEFINE_GAMEPLAY_TAG(TagName, TagString) \
    NativeGameplayTag TagName(TagString)

// 定义并注册原生标签（仅本编译单元可见，static 限定）
#define DEFINE_GAMEPLAY_TAG_STATIC(TagName, TagString) \
    static NativeGameplayTag TagName(TagString)

// 带注释的版本（注释仅用于文档/编辑器，运行时不使用）
#define DEFINE_GAMEPLAY_TAG_COMMENT(TagName, TagString, Comment) \
    NativeGameplayTag TagName(TagString, Comment)

// ============================================================
//  GameplayTagContainer 内联实现
// ============================================================

inline void GameplayTagContainer::AddTag(const GameplayTag& tag)
{
    if (!tag.IsValid()) return;
    if (m_explicitTags.insert(tag.GetID()).second)
    {
        // 新标签添加成功，缓存其所有父标签 ID
        const auto& parentIDs = GameplayTagManager::Get().GetParentTagIDs(tag.GetID());
        for (TagID pid : parentIDs)
            m_parentTagCache.insert(pid);
    }
}

inline void GameplayTagContainer::RemoveTag(const GameplayTag& tag)
{
    if (!tag.IsValid()) return;
    if (m_explicitTags.erase(tag.GetID()))
    {
        // 标签被移除，需要重建父标签缓存
        RebuildParentCache();
    }
}

inline void GameplayTagContainer::AppendTags(const GameplayTagContainer& other)
{
    for (TagID id : other.m_explicitTags)
        AddTag(GameplayTagManager::Get().GetTagByID(id));
}

inline void GameplayTagContainer::Reset()
{
    m_explicitTags.clear();
    m_parentTagCache.clear();
}

inline bool GameplayTagContainer::HasTag(const GameplayTag& tag) const
{
    if (!tag.IsValid()) return false;
    // O(1) 查找：检查父标签缓存中是否包含目标 ID
    // 这是 UE 的核心优化：不需要遍历容器中每个标签再逐一检查层级
    return m_parentTagCache.count(tag.GetID()) > 0;
}

inline bool GameplayTagContainer::HasTagExact(const GameplayTag& tag) const
{
    if (!tag.IsValid()) return false;
    return m_explicitTags.count(tag.GetID()) > 0;
}

inline bool GameplayTagContainer::HasAll(const GameplayTagContainer& other) const
{
    if (other.IsEmpty()) return true;
    for (TagID id : other.m_explicitTags)
    {
        if (m_parentTagCache.count(id) == 0)
            return false;
    }
    return true;
}

inline bool GameplayTagContainer::HasAllExact(const GameplayTagContainer& other) const
{
    if (other.IsEmpty()) return true;
    for (TagID id : other.m_explicitTags)
    {
        if (m_explicitTags.count(id) == 0)
            return false;
    }
    return true;
}

inline bool GameplayTagContainer::HasAny(const GameplayTagContainer& other) const
{
    if (other.IsEmpty()) return false;
    for (TagID id : other.m_explicitTags)
    {
        if (m_parentTagCache.count(id) > 0)
            return true;
    }
    return false;
}

inline bool GameplayTagContainer::HasAnyExact(const GameplayTagContainer& other) const
{
    if (other.IsEmpty()) return false;
    for (TagID id : other.m_explicitTags)
    {
        if (m_explicitTags.count(id) > 0)
            return true;
    }
    return false;
}

inline void GameplayTagContainer::RebuildParentCache()
{
    m_parentTagCache.clear();
    auto& mgr = GameplayTagManager::Get();
    for (TagID id : m_explicitTags)
    {
        const auto& parentIDs = mgr.GetParentTagIDs(id);
        for (TagID pid : parentIDs)
            m_parentTagCache.insert(pid);
    }
}

inline std::string GameplayTagContainer::ToString() const
{
    if (IsEmpty()) return "{}";

    auto& mgr = GameplayTagManager::Get();
    std::string result = "{ ";
    bool first = true;
    for (TagID id : m_explicitTags)
    {
        if (!first) result += ", ";
        result += mgr.GetTagName(id);
        first = false;
    }
    result += " }";
    return result;
}
