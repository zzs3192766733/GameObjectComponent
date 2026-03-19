#include "PropertyMeta.h"


// ============================================================
//  PropertyRegistry 单例实现
// ============================================================

PropertyRegistry& PropertyRegistry::Get()
{
    static PropertyRegistry instance;
    return instance;
}

void PropertyRegistry::Register(std::type_index typeIdx, std::vector<PropertyMeta> props)
{
    m_registry[typeIdx] = std::move(props);
}

const std::vector<PropertyMeta>* PropertyRegistry::GetProperties(std::type_index typeIdx) const
{
    auto it = m_registry.find(typeIdx);
    if (it != m_registry.end())
        return &it->second;
    return nullptr;
}

bool PropertyRegistry::IsRegistered(std::type_index typeIdx) const
{
    return m_registry.find(typeIdx) != m_registry.end();
}

size_t PropertyRegistry::GetRegisteredTypeCount() const
{
    return m_registry.size();
}


// ============================================================
//  AutoSerializer 实现
// ============================================================

// 将单个属性值转为 JSON 值字符串
std::string AutoSerializer::PropertyValueToJson(
    const void* obj,
    const PropertyMeta& prop)
{
    // 通过 base 指针 + offset 计算属性在内存中的地址
    const char* base = static_cast<const char*>(obj);
    const void* addr = base + prop.offset;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);

    switch (prop.type)
    {
    case EPropertyType::Float:
        oss << *static_cast<const float*>(addr);
        break;

    case EPropertyType::Int32:
        oss << *static_cast<const int32_t*>(addr);
        break;

    case EPropertyType::UInt32:
        oss << *static_cast<const uint32_t*>(addr);
        break;

    case EPropertyType::Bool:
        oss << (*static_cast<const bool*>(addr) ? "true" : "false");
        break;

    case EPropertyType::String:
    {
        // std::string 需要加引号转义
        const auto& str = *static_cast<const std::string*>(addr);
        oss << "\"" << str << "\"";
        break;
    }

    case EPropertyType::Vec2:
    {
        // Vec2 是 { float x, float y } 布局
        const float* f = static_cast<const float*>(addr);
        oss << "{\"x\":" << f[0] << ",\"y\":" << f[1] << "}";
        break;
    }

    default:
        oss << "\"<unknown>\"";
        break;
    }

    return oss.str();
}

// 将对象的已注册属性序列化为 JSON 字符串
std::string AutoSerializer::SerializeToJson(
    const void* obj,
    const std::vector<PropertyMeta>& props,
    bool onlySerializable)
{
    std::ostringstream oss;
    oss << "{";

    bool first = true;
    for (const auto& prop : props)
    {
        // 如果只序列化标记的属性，跳过非序列化的
        if (onlySerializable && !prop.ShouldSerialize())
            continue;

        if (!first) oss << ",";
        first = false;

        // 输出 "属性名": 值
        oss << "\"" << prop.name << "\":" << PropertyValueToJson(obj, prop);
    }

    oss << "}";
    return oss.str();
}

// 根据类型索引从 PropertyRegistry 获取属性列表并序列化
std::string AutoSerializer::SerializeByType(
    const void* obj,
    std::type_index typeIdx,
    bool onlySerializable)
{
    const auto* props = PropertyRegistry::Get().GetProperties(typeIdx);
    if (!props)
        return "{}";
    return SerializeToJson(obj, *props, onlySerializable);
}

// 获取属性标志的可读字符串
std::string AutoSerializer::FlagsToString(EPropertyFlags flags)
{
    std::string result;
    if (HasFlag(flags, EPropertyFlags::Serializable))
        result += "Serializable ";
    if (HasFlag(flags, EPropertyFlags::Transient))
        result += "Transient ";
    if (HasFlag(flags, EPropertyFlags::EditorVisible))
        result += "EditorVisible ";
    if (HasFlag(flags, EPropertyFlags::ReadOnly))
        result += "ReadOnly ";
    if (HasFlag(flags, EPropertyFlags::Replicated))
        result += "Replicated ";
    if (result.empty())
        result = "None";
    return result;
}

// 生成属性列表的调试报告
std::string AutoSerializer::GeneratePropertyReport(
    const void* obj,
    const std::vector<PropertyMeta>& props)
{
    std::ostringstream oss;
    oss << "属性反射报告（共 " << props.size() << " 个属性）:\n";
    oss << std::string(60, '-') << "\n";
    oss << std::left
        << std::setw(20) << "属性名"
        << std::setw(10) << "类型"
        << std::setw(20) << "标志"
        << std::setw(10) << "值"
        << "\n";
    oss << std::string(60, '-') << "\n";

    // 类型名映射
    auto typeName = [](EPropertyType t) -> const char*
    {
        switch (t)
        {
        case EPropertyType::Float:   return "float";
        case EPropertyType::Int32:   return "int32";
        case EPropertyType::UInt32:  return "uint32";
        case EPropertyType::Bool:    return "bool";
        case EPropertyType::String:  return "string";
        case EPropertyType::Vec2:    return "Vec2";
        default:                     return "unknown";
        }
    };

    for (const auto& prop : props)
    {
        oss << std::left
            << std::setw(20) << prop.name
            << std::setw(10) << typeName(prop.type)
            << std::setw(20) << FlagsToString(prop.flags)
            << PropertyValueToJson(obj, prop)
            << (prop.ShouldSerialize() ? "" : "  [跳过]")
            << "\n";
    }

    oss << std::string(60, '-') << "\n";
    return oss.str();
}
