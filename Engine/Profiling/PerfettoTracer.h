
#pragma once

// ============================================================
//  PerfettoTracer — 轻量级函数性能分析器
//
//  功能：记录函数/作用域的执行时间，生成 Perfetto/Chrome Trace Event
//        Format JSON 文件，可直接在以下工具中打开查看火焰图：
//          - Google Perfetto UI:  https://ui.perfetto.dev/
//          - Chrome DevTools:     chrome://tracing
//
//  设计参考：
//    - UE: FPlatformTime::Cycles64() + FScopedDurationTimer
//    - UE: Unreal Insights 的 Trace Event 机制
//    - Unity: Unity Profiler 的 Profiler.BeginSample/EndSample
//    - Chrome Trace Event Format:
//        https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
//
//  使用方式：
//    方式一：作用域自动计时
//      void MyFunction() {
//          PROFILE_FUNCTION();  // 自动使用 __FUNCTION__ 作为名称
//          // ... 函数体 ...
//      }
//
//    方式二：自定义名称
//      {
//          PROFILE_SCOPE("CustomName");
//          // ... 代码块 ...
//      }
//
//    方式三：手动控制
//      PerfettoTracer::Get().BeginEvent("MyEvent", "MyCategory");
//      // ... 代码 ...
//      PerfettoTracer::Get().EndEvent();
//
//    启动/停止/导出：
//      PerfettoTracer::Get().BeginSession("profile_output");
//      // ... 程序运行 ...
//      PerfettoTracer::Get().EndSession(); // 自动写入 profile_output.json
//
//  输出格式：
//    Trace Event Format (JSON Array Format)
//    每个事件包含：name, cat, ph, ts, dur, pid, tid
//    直接兼容 Perfetto UI 和 Chrome Tracing
//
//  线程安全：
//    使用 std::mutex 保护事件容器，支持多线程同时记录
//    时间戳使用 std::chrono::high_resolution_clock（微秒精度）
//
//  性能开销：
//    - 开启 profiling: 每个事件约 100-200ns 开销（互斥锁 + 时间查询）
//    - 关闭 profiling: 宏展开为空，零开销
//    - 通过 ENABLE_PROFILING 宏控制编译期开关
// ============================================================

#include <string>
#include <vector>
#include <chrono>
#include <mutex>
#include <thread>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

// ============================================================
//  编译期开关
//  #define ENABLE_PROFILING 0  可在 Release 构建中完全关闭 Profiling
//  默认在 Debug 模式开启，Release 模式关闭
// ============================================================
#ifndef ENABLE_PROFILING
    #ifdef _DEBUG
        #define ENABLE_PROFILING 1
    #else
        #define ENABLE_PROFILING 0
    #endif
#endif

// ============================================================
//  Trace Event 数据结构
//  对应 Chrome Trace Event Format 的字段
//  参考：https://docs.google.com/document/d/1CvAClvFfyA5R-PhYUmn5OOQtYMH4h6I0nSsKchNAySU
// ============================================================
struct TraceEvent
{
    std::string name;       // 事件名称（函数名/作用域名）
    std::string category;   // 分类标签（用于 Perfetto UI 中的分组过滤）
    
    // 'B' = Begin, 'E' = End, 'X' = Complete (Duration)
    // 我们使用 'X'（Complete）格式：包含 ts + dur，一条记录描述完整事件
    char phase = 'X';

    // 时间戳和持续时间（微秒，Perfetto 要求微秒单位）
    int64_t timestamp_us = 0;  // 事件起始时间（相对于 session 开始）
    int64_t duration_us = 0;   // 持续时间

    // 进程 ID 和线程 ID（Perfetto 用于分 Track 显示）
    uint32_t processID = 0;
    uint32_t threadID = 0;
};

// ============================================================
//  PerfettoTracer — 性能分析器单例
// ============================================================
class PerfettoTracer
{
public:
    // 单例访问
    static PerfettoTracer& Get();

    // --------------------------------------------------------
    //  会话管理
    // --------------------------------------------------------

    // 开始分析会话
    // outputName: 输出文件名（不含扩展名），最终生成 <outputName>.json
    void BeginSession(const std::string& outputName = "profile_result");

    // 结束会话并写入 JSON 文件
    void EndSession();

    // 会话是否活跃
    bool IsSessionActive() const;

    // --------------------------------------------------------
    //  事件记录 API
    // --------------------------------------------------------

    // 记录一个完整事件（Duration Event, phase='X'）
    // 通常由 ProfileScope RAII 对象调用，不需要手动使用
    void WriteEvent(const TraceEvent& event);

    // 手动 Begin/End 事件（phase='B'/'E'）
    // 用于不方便使用 RAII 的场景
    void BeginEvent(const std::string& name, const std::string& category = "Default");
    void EndEvent();

    // --------------------------------------------------------
    //  时间基准
    // --------------------------------------------------------

    // 获取当前时间戳（微秒，相对于 session 开始）
    int64_t GetTimestampUs() const;

    // --------------------------------------------------------
    //  统计信息
    // --------------------------------------------------------

    // 获取已记录的事件总数
    size_t GetEventCount() const;

    // 获取输出文件路径
    const std::string& GetOutputFilePath() const;

private:
    PerfettoTracer() = default;
    ~PerfettoTracer();

    // 禁止拷贝/移动
    PerfettoTracer(const PerfettoTracer&) = delete;
    PerfettoTracer& operator=(const PerfettoTracer&) = delete;

    // 写入 JSON 文件
    void FlushToFile();

    // JSON 字符串转义（处理特殊字符）
    static std::string EscapeJsonString(const std::string& input);

    // 获取当前线程 ID（转换为 uint32_t）
    static uint32_t GetCurrentThreadID();

    // 获取当前进程 ID
    static uint32_t GetCurrentProcessID();

    // --------------------------------------------------------
    //  数据
    // --------------------------------------------------------
    std::string m_outputName;
    std::string m_outputFilePath;
    bool m_sessionActive = false;

    // 会话开始时间（用于计算相对时间戳）
    std::chrono::high_resolution_clock::time_point m_sessionStart;

    // 事件缓冲区（线程安全）
    std::vector<TraceEvent> m_events;
    mutable std::mutex m_mutex;

    // 手动 Begin/End 用的栈（per-thread 简化为单线程栈）
    struct PendingEvent
    {
        std::string name;
        std::string category;
        int64_t startTimestamp_us;
        uint32_t threadID;
    };
    std::vector<PendingEvent> m_pendingStack;
};

// ============================================================
//  ProfileScope — RAII 作用域计时器
//
//  构造时记录开始时间，析构时自动计算持续时间并写入事件
//  参考 UE 的 FScopedDurationTimer / TRACE_CPUPROFILER_EVENT_SCOPE
//  参考 Unity 的 ProfilerMarker.Auto()
//
//  使用方式：
//    {
//        ProfileScope scope("MyFunction", "Engine");
//        // ... 被测代码 ...
//    } // 析构时自动写入事件
// ============================================================
class ProfileScope
{
public:
    ProfileScope(const char* name, const char* category = "Default");
    ~ProfileScope();

    // 禁止拷贝/移动
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    const char* m_name;
    const char* m_category;
    int64_t m_startTimestamp_us;
    uint32_t m_threadID;
};

// ============================================================
//  便捷宏
//
//  PROFILE_SCOPE(name)     — 自定义名称的作用域计时
//  PROFILE_SCOPE_CAT(name, category) — 带分类的作用域计时
//  PROFILE_FUNCTION()      — 使用 __FUNCTION__ 自动命名
//
//  当 ENABLE_PROFILING == 0 时，宏展开为空语句，零开销
//
//  参考：
//    UE:    TRACE_CPUPROFILER_EVENT_SCOPE(Name)
//    Unity: Profiler.BeginSample("Name") / EndSample()
// ============================================================

// 辅助宏：拼接行号生成唯一变量名，避免同一作用域多次使用冲突
#define PROFILE_CONCAT_IMPL(a, b) a##b
#define PROFILE_CONCAT(a, b) PROFILE_CONCAT_IMPL(a, b)
#define PROFILE_UNIQUE_VAR(prefix) PROFILE_CONCAT(prefix, __LINE__)

#if ENABLE_PROFILING

    // 自定义名称的作用域计时
    #define PROFILE_SCOPE(name) \
        ProfileScope PROFILE_UNIQUE_VAR(_profileScope_)(name)

    // 带分类的作用域计时
    #define PROFILE_SCOPE_CAT(name, category) \
        ProfileScope PROFILE_UNIQUE_VAR(_profileScope_)(name, category)

    // 使用函数名自动命名
    #define PROFILE_FUNCTION() \
        ProfileScope PROFILE_UNIQUE_VAR(_profileScope_)(__FUNCTION__, "Function")

#else

    #define PROFILE_SCOPE(name)                 ((void)0)
    #define PROFILE_SCOPE_CAT(name, category)   ((void)0)
    #define PROFILE_FUNCTION()                  ((void)0)

#endif
