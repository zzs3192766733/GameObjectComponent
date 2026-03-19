
#include "PerfettoTracer.h"

#ifdef _WIN32
    #include <Windows.h>   // GetCurrentProcessId, GetCurrentThreadId
#else
    #include <unistd.h>    // getpid
    #include <pthread.h>   // pthread_self (macOS/Linux)
#endif

// ============================================================
//  PerfettoTracer 实现
// ============================================================

// --------------------------------------------------------
//  单例访问
// --------------------------------------------------------
PerfettoTracer& PerfettoTracer::Get()
{
    static PerfettoTracer instance;
    return instance;
}

// --------------------------------------------------------
//  析构：确保未关闭的会话被写入
// --------------------------------------------------------
PerfettoTracer::~PerfettoTracer()
{
    if (m_sessionActive)
    {
        EndSession();
    }
}

// --------------------------------------------------------
//  会话管理
// --------------------------------------------------------
void PerfettoTracer::BeginSession(const std::string& outputName)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_sessionActive)
    {
        std::cerr << "[PerfettoTracer] 警告: 会话已在进行中，先结束当前会话\n";
        // 在锁内直接写入（不再递归加锁）
        FlushToFile();
        m_events.clear();
        m_pendingStack.clear();
    }

    m_outputName = outputName;
    m_outputFilePath = outputName + ".json";
    m_sessionActive = true;
    m_events.clear();
    m_pendingStack.clear();

    // 记录会话开始时间
    m_sessionStart = std::chrono::high_resolution_clock::now();

    // 预留空间减少 realloc（典型游戏帧约 1000~5000 个事件）
    m_events.reserve(10000);

    std::cout << "[PerfettoTracer] 开始分析会话 → '" << m_outputFilePath << "'\n";
}

void PerfettoTracer::EndSession()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive)
    {
        std::cerr << "[PerfettoTracer] 警告: 没有活跃的分析会话\n";
        return;
    }

    // 写入 JSON 文件
    FlushToFile();

    size_t eventCount = m_events.size();
    m_events.clear();
    m_pendingStack.clear();
    m_sessionActive = false;

    std::cout << "[PerfettoTracer] 会话结束，共记录 " << eventCount << " 个事件"
              << " → '" << m_outputFilePath << "'\n";
    std::cout << "[PerfettoTracer] 请在 https://ui.perfetto.dev/ 中打开查看火焰图\n";
}

bool PerfettoTracer::IsSessionActive() const
{
    return m_sessionActive;
}

// --------------------------------------------------------
//  事件记录
// --------------------------------------------------------
void PerfettoTracer::WriteEvent(const TraceEvent& event)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive) return;

    m_events.push_back(event);
}

void PerfettoTracer::BeginEvent(const std::string& name, const std::string& category)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive) return;

    PendingEvent pending;
    pending.name = name;
    pending.category = category;
    pending.startTimestamp_us = GetTimestampUs();
    pending.threadID = GetCurrentThreadID();
    m_pendingStack.push_back(std::move(pending));
}

void PerfettoTracer::EndEvent()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (!m_sessionActive || m_pendingStack.empty()) return;

    // 弹出最近的 BeginEvent
    PendingEvent pending = std::move(m_pendingStack.back());
    m_pendingStack.pop_back();

    // 构造 Complete Event
    TraceEvent event;
    event.name = std::move(pending.name);
    event.category = std::move(pending.category);
    event.phase = 'X';
    event.timestamp_us = pending.startTimestamp_us;
    event.duration_us = GetTimestampUs() - pending.startTimestamp_us;
    event.processID = GetCurrentProcessID();
    event.threadID = pending.threadID;

    m_events.push_back(std::move(event));
}

// --------------------------------------------------------
//  时间基准
// --------------------------------------------------------
int64_t PerfettoTracer::GetTimestampUs() const
{
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        now - m_sessionStart);
    return elapsed.count();
}

// --------------------------------------------------------
//  统计
// --------------------------------------------------------
size_t PerfettoTracer::GetEventCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_events.size();
}

const std::string& PerfettoTracer::GetOutputFilePath() const
{
    return m_outputFilePath;
}

// --------------------------------------------------------
//  JSON 序列化 — Chrome Trace Event Format
//
//  格式规范：
//    {
//      "traceEvents": [
//        { "name": "...", "cat": "...", "ph": "X",
//          "ts": 12345, "dur": 678, "pid": 1, "tid": 1 },
//        ...
//      ],
//      "displayTimeUnit": "ms",
//      "otherData": { "version": "Mini2DEngine Profiler v1.0" }
//    }
//
//  参考：
//    - Chrome Trace Event Format 文档
//    - Perfetto Trace Processor 兼容的 JSON 格式
//    - UE Trace 的输出格式（.utrace → JSON 转换）
// --------------------------------------------------------
void PerfettoTracer::FlushToFile()
{
    // 注意：调用时已在 mutex 保护下，不需要再加锁

    std::ofstream file(m_outputFilePath);
    if (!file.is_open())
    {
        std::cerr << "[PerfettoTracer] 错误: 无法打开文件 '" << m_outputFilePath << "'\n";
        return;
    }

    // 手动构建 JSON（避免引入第三方 JSON 库）
    file << "{\n";
    file << "  \"traceEvents\": [\n";

    for (size_t i = 0; i < m_events.size(); ++i)
    {
        const auto& e = m_events[i];

        file << "    {";
        file << "\"name\":\"" << EscapeJsonString(e.name) << "\",";
        file << "\"cat\":\"" << EscapeJsonString(e.category) << "\",";
        file << "\"ph\":\"" << e.phase << "\",";
        file << "\"ts\":" << e.timestamp_us << ",";
        file << "\"dur\":" << e.duration_us << ",";
        file << "\"pid\":" << e.processID << ",";
        file << "\"tid\":" << e.threadID;
        file << "}";

        if (i + 1 < m_events.size())
            file << ",";
        file << "\n";
    }

    file << "  ],\n";
    file << "  \"displayTimeUnit\": \"ms\",\n";
    file << "  \"otherData\": {\n";
    file << "    \"version\": \"Mini2DEngine PerfettoTracer v1.0\"\n";
    file << "  }\n";
    file << "}\n";

    file.close();
}

// --------------------------------------------------------
//  辅助：JSON 字符串转义
//  处理 \ " \n \t 等特殊字符
// --------------------------------------------------------
std::string PerfettoTracer::EscapeJsonString(const std::string& input)
{
    std::string output;
    output.reserve(input.size() + 8);

    for (char c : input)
    {
        switch (c)
        {
        case '"':  output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\n': output += "\\n";  break;
        case '\r': output += "\\r";  break;
        case '\t': output += "\\t";  break;
        default:   output += c;      break;
        }
    }
    return output;
}

// --------------------------------------------------------
//  平台相关：线程 ID / 进程 ID
// --------------------------------------------------------
uint32_t PerfettoTracer::GetCurrentThreadID()
{
    // std::this_thread::get_id() → hash → uint32_t
    auto id = std::this_thread::get_id();
    std::hash<std::thread::id> hasher;
    return static_cast<uint32_t>(hasher(id));
}

uint32_t PerfettoTracer::GetCurrentProcessID()
{
#ifdef _WIN32
    return static_cast<uint32_t>(GetCurrentProcessId());
#else
    return static_cast<uint32_t>(getpid());
#endif
}

// ============================================================
//  ProfileScope 实现
// ============================================================

ProfileScope::ProfileScope(const char* name, const char* category)
    : m_name(name)
    , m_category(category)
    , m_startTimestamp_us(0)
    , m_threadID(0)
{
#if ENABLE_PROFILING
    auto& tracer = PerfettoTracer::Get();
    if (tracer.IsSessionActive())
    {
        m_startTimestamp_us = tracer.GetTimestampUs();
        // 获取线程 ID
        auto id = std::this_thread::get_id();
        std::hash<std::thread::id> hasher;
        m_threadID = static_cast<uint32_t>(hasher(id));
    }
#endif
}

ProfileScope::~ProfileScope()
{
#if ENABLE_PROFILING
    auto& tracer = PerfettoTracer::Get();
    if (tracer.IsSessionActive())
    {
        TraceEvent event;
        event.name = m_name;
        event.category = m_category;
        event.phase = 'X';
        event.timestamp_us = m_startTimestamp_us;
        event.duration_us = tracer.GetTimestampUs() - m_startTimestamp_us;
#ifdef _WIN32
        event.processID = static_cast<uint32_t>(GetCurrentProcessId());
#else
        event.processID = static_cast<uint32_t>(getpid());
#endif
        event.threadID = m_threadID;
        tracer.WriteEvent(event);
    }
#endif
}
