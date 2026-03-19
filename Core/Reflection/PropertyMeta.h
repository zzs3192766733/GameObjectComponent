#pragma once
#include <string>
#include <vector>
#include <cstddef>    // offsetof
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <typeindex>
#include <sstream>
#include <iomanip>

// ============================================================
//  属性元数据系统（Property Reflection System）
//
//  设计灵感：
//    - UE 的 UPROPERTY() 宏 + UHT 反射系统
//    - C# 的 [SerializeField] / [NonSerialized] 属性标签
//
//  本实现使用 宏标记 + 手动注册 方案（不需要外部代码生成工具）
//  核心思路：
//    1. 用 MPROPERTY() 宏声明"哪些属性要序列化"
//    2. 每个组件在 RegisterProperties() 中集中注册属性元信息
//    3. AutoSerializer 根据元信息自动序列化/反序列化
//
//  使用方式：
//    class MyComponent : public Component {
//        MPROPERTY(Serializable)    float m_health;     // 参与序列化
//        MPROPERTY(Transient)       float m_cachedVal;  // 不序列化
//        /* 不加标记 */             float m_internal;   // 完全不可见
//    };
// ============================================================


// ============================================================
//  属性标志（对应 C# [SerializeField]、UE UPROPERTY 的参数）
// ============================================================
enum class EPropertyFlags : uint32_t
{
    None           = 0,
    Serializable   = 1 << 0,    // 参与序列化（对应 UE SaveGame / C# [SerializeField]）
    Transient      = 1 << 1,    // 不序列化（对应 UE Transient / C# [NonSerialized]）
    EditorVisible  = 1 << 2,    // 在编辑器中可见（预留，未来扩展）
    ReadOnly       = 1 << 3,    // 编辑器中只读（预留）
    Replicated     = 1 << 4,    // 网络同步（预留，对应 UE Replicated）
};

// 位运算支持
inline EPropertyFlags operator|(EPropertyFlags a, EPropertyFlags b)
{
    return static_cast<EPropertyFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool HasFlag(EPropertyFlags flags, EPropertyFlags test)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}


// ============================================================
//  属性值类型枚举
//  用于运行时识别属性的内存布局，以实现通用的读写
// ============================================================
enum class EPropertyType : uint8_t
{
    Float,          // float
    Int32,          // int32_t
    UInt32,         // uint32_t
    Bool,           // bool
    String,         // std::string
    Vec2,           // Vec2（自定义类型：2个 float）
    Unknown,
};


// ============================================================
//  PropertyMeta — 单个属性的元数据描述
//  对应 UE 的 FProperty / C# 的 FieldInfo
// ============================================================
struct PropertyMeta
{
    std::string     name;       // 属性名（如 "m_position"）
    EPropertyType   type;       // 值类型
    EPropertyFlags  flags;      // 标志位
    size_t          offset;     // 在对象中的字节偏移（由 offsetof 宏计算）
    size_t          size;       // 属性大小（字节）
    std::string     category;   // 分类名（可选，用于编辑器分组）

    // 检查是否应该序列化
    bool ShouldSerialize() const
    {
        return HasFlag(flags, EPropertyFlags::Serializable)
            && !HasFlag(flags, EPropertyFlags::Transient);
    }

    // 检查是否在编辑器中可见
    bool IsEditorVisible() const
    {
        return HasFlag(flags, EPropertyFlags::EditorVisible);
    }
};


// ============================================================
//  MPROPERTY 标记宏
//
//  用法（在类中声明成员变量前加上）：
//    MPROPERTY(Serializable)         float m_health;
//    MPROPERTY(Transient)            float m_cachedDamage;
//    MPROPERTY(Serializable | EditorVisible)  Vec2 m_position;
//
//  注意：这些宏本身不做任何代码注入（和 UE UPROPERTY() 一样），
//        真正的注册逻辑在 RegisterProperties() 中通过 REGISTER_PROPERTY 宏完成。
//        MPROPERTY 仅作为**可读性标记**，让开发者一眼看出哪些属性被反射。
// ============================================================
#define MPROPERTY(...)   /* 属性标记：仅作为可读性注解，实际注册在 RegisterProperties() 中 */


// ============================================================
//  REGISTER_PROPERTY 宏 — 在 RegisterProperties() 中使用
//  自动推断类名、属性名、偏移量和大小
//
//  用法：
//    static std::vector<PropertyMeta> RegisterProperties() {
//        std::vector<PropertyMeta> props;
//        REGISTER_PROPERTY(props, MyComponent, m_health, EPropertyType::Float,
//                          EPropertyFlags::Serializable, "Combat");
//        return props;
//    }
// ============================================================
#define REGISTER_PROPERTY(list, Class, Member, Type, Flags, Category) \
    list.push_back(PropertyMeta{                            \
        #Member,                                            \
        Type,                                               \
        Flags,                                              \
        offsetof(Class, Member),                            \
        sizeof(decltype(Class::Member)),                    \
        Category                                            \
    })


// ============================================================
//  PropertyRegistry — 全局属性注册表
//  参考 UE 的 UClass/FProperty 注册表
//  每种组件类型注册一次属性列表，后续通过 type_index 查询
// ============================================================
class PropertyRegistry
{
public:
    // 单例访问
    static PropertyRegistry& Get();

    // 注册某个类型的属性列表
    void Register(std::type_index typeIdx, std::vector<PropertyMeta> props);

    // 查询某个类型的属性列表
    const std::vector<PropertyMeta>* GetProperties(std::type_index typeIdx) const;

    // 检查某个类型是否已注册
    bool IsRegistered(std::type_index typeIdx) const;

    // 获取已注册的类型数量
    size_t GetRegisteredTypeCount() const;

private:
    PropertyRegistry() = default;

    // type_index -> 属性列表
    std::unordered_map<std::type_index, std::vector<PropertyMeta>> m_registry;
};


// ============================================================
//  REGISTER_COMPONENT_PROPERTIES 宏
//  在 .cpp 文件的全局作用域中使用，自动注册属性到全局注册表
//
//  用法（在 TransformComponent.cpp 中）：
//    REGISTER_COMPONENT_PROPERTIES(TransformComponent)
//
//  展开后等价于在程序启动时（main 之前）调用 RegisterProperties()
//  并存入 PropertyRegistry 全局单例
// ============================================================
#define REGISTER_COMPONENT_PROPERTIES(ClassName)                             \
    namespace {                                                              \
        struct ClassName##_PropertyAutoRegister {                             \
            ClassName##_PropertyAutoRegister() {                              \
                auto props = ClassName::RegisterProperties();                 \
                PropertyRegistry::Get().Register(                            \
                    std::type_index(typeid(ClassName)), std::move(props));    \
            }                                                                \
        };                                                                   \
        static ClassName##_PropertyAutoRegister s_##ClassName##_autoReg;      \
    }


// ============================================================
//  AutoSerializer — 通用自动序列化器
//  根据 PropertyMeta 列表，自动将对象的标记属性序列化为 JSON
//  参考 UE FStructuredArchive / Unity JsonUtility
// ============================================================
class AutoSerializer
{
public:
    // 将对象的已注册属性序列化为 JSON 字符串
    // obj:     组件对象指针（const void*，通过 offset 访问属性）
    // props:   属性元数据列表
    // onlySerializable: 若为 true，只输出标记为 Serializable 的属性
    static std::string SerializeToJson(
        const void* obj,
        const std::vector<PropertyMeta>& props,
        bool onlySerializable = true);

    // 根据类型索引从 PropertyRegistry 获取属性列表并序列化
    static std::string SerializeByType(
        const void* obj,
        std::type_index typeIdx,
        bool onlySerializable = true);

    // 将单个属性值转为 JSON 值字符串
    static std::string PropertyValueToJson(
        const void* obj,
        const PropertyMeta& prop);

    // 获取属性标志的可读字符串（调试用）
    static std::string FlagsToString(EPropertyFlags flags);

    // 生成属性列表的调试报告（显示所有属性及其元信息）
    static std::string GeneratePropertyReport(
        const void* obj,
        const std::vector<PropertyMeta>& props);
};
