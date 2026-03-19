
#pragma once
#include <cstdint>
#include <string>
#include <atomic>

// ============================================================
//  核心类型定义
//  设计B：场景图模式 - 所有权明确，通过ID引用对象
// ============================================================

// GameObject 唯一ID类型（0 = 无效ID）
using GameObjectID = uint64_t;
constexpr GameObjectID INVALID_GAME_OBJECT_ID = 0;

// Component 类型ID（通过模板静态变量实现，每种类型唯一）
using ComponentTypeID = uint32_t;
constexpr ComponentTypeID INVALID_COMPONENT_TYPE_ID = 0;

// ============================================================
//  组件类型ID生成器
//  每种 Component 子类在首次调用时自动分配唯一ID
//
//  ⚠️ 注意 #9（SIOF 风险）：
//  - 当前实现使用 static local 变量，在单一可执行文件中安全
//  - 如果将来引入 DLL/插件系统，每个编译单元的 static local 可能
//    得到不同的 ID（因为模板实例化是 per-TU 的）
//  - 解决方案：引入全局注册表或在 DLL 边界使用显式注册
// ============================================================
class ComponentTypeRegistry
{
public:
    // 获取下一个可用的类型ID
    static ComponentTypeID NextTypeID()
    {
        static std::atomic<ComponentTypeID> counter{ 1 };
        return counter.fetch_add(1, std::memory_order_relaxed);
    }

    // 获取指定类型T的唯一类型ID（每种类型只生成一次）
    template<typename T>
    static ComponentTypeID GetTypeID()
    {
        static ComponentTypeID id = NextTypeID();
        return id;
    }
};

// ============================================================
//  GameObject ID 生成器
// ============================================================
class GameObjectIDGenerator
{
public:
    static GameObjectID Generate()
    {
        static std::atomic<GameObjectID> counter{ 1 };
        return counter.fetch_add(1, std::memory_order_relaxed);
    }
};
