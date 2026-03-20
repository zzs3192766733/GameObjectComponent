#pragma once
// ============================================================
//  TaskFlow — 任务流编排系统
//
//  设计参考：
//    - UE: FTaskGraph / FGraphEvent / FBaseGraphTask
//    - Unity: JobSystem + JobHandle.Complete()
//    - taskflow 库: tf::Taskflow / tf::Executor
//
//  核心概念：
//    TaskNode     — 单个任务节点（同步/异步），可携带名称和状态
//    TaskFlow     — 任务流图（DAG），由多个 TaskNode 组成
//    TaskExecutor — 执行器，驱动 TaskFlow 在 ThreadPool 上运行
//
//  三种编排模式：
//    1. Sequential（顺序）：A → B → C，严格按序执行
//    2. Parallel（并行）  ：A | B | C，同时执行，全部完成后继续
//    3. Mixed（混合）     ：Sequential + Parallel 任意嵌套
//
//  使用示例：
//    TaskFlow flow("MyFlow");
//    auto& a = flow.AddTask("LoadTextures", []{ ... });
//    auto& b = flow.AddTask("LoadMeshes",   []{ ... });
//    auto& c = flow.AddTask("BuildScene",   []{ ... });
//
//    // c 依赖 a 和 b（a,b 可并行，c 等它们全完成）
//    c.DependsOn(a);
//    c.DependsOn(b);
//
//    TaskExecutor executor(4);  // 4个工作线程
//    executor.Run(flow).Wait(); // 执行并等待完成
// ============================================================

#include "ThreadPool.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cassert>
#include <algorithm>

// ============================================================
//  TaskNode — 任务节点
//  代表任务图中的一个可执行节点
//  参考 UE: FBaseGraphTask / FGraphEventRef
// ============================================================
class TaskNode
{
    friend class TaskFlow;
    friend class TaskExecutor;

public:
    // 任务状态
    enum class State : uint8_t
    {
        Pending,     // 等待依赖完成
        Ready,       // 依赖已满足，可以执行
        Running,     // 正在执行
        Completed,   // 已完成
        Failed       // 执行失败
    };

    TaskNode(uint32_t id, const std::string& name, std::function<void()> work)
        : m_id(id)
        , m_name(name)
        , m_work(std::move(work))
        , m_state(State::Pending)
        , m_pendingDeps(0)
        , m_durationMs(0.0)
    {}

    // --------------------------------------------------------
    //  依赖关系（链式调用）
    //  参考 UE: FGraphEventRef::DontCompleteUntil(...)
    //  参考 taskflow: task.precede(other) / task.succeed(other)
    // --------------------------------------------------------

    // 当前节点依赖 other（other 完成后才能执行当前节点）
    TaskNode& DependsOn(TaskNode& other)
    {
        m_dependencies.push_back(&other);
        other.m_dependents.push_back(this);
        return *this;
    }

    // 当前节点先于 other 执行（语法糖，等价于 other.DependsOn(*this)）
    TaskNode& Precedes(TaskNode& other)
    {
        other.DependsOn(*this);
        return *this;
    }

    // --------------------------------------------------------
    //  查询接口
    // --------------------------------------------------------
    uint32_t           GetID() const       { return m_id; }
    const std::string& GetName() const     { return m_name; }
    State              GetState() const    { return m_state.load(); }
    double             GetDurationMs() const { return m_durationMs; }
    bool               IsCompleted() const { return m_state.load() == State::Completed; }
    bool               IsFailed() const    { return m_state.load() == State::Failed; }

    const std::vector<TaskNode*>& GetDependencies() const { return m_dependencies; }
    const std::vector<TaskNode*>& GetDependents() const   { return m_dependents; }

    static const char* StateToString(State s)
    {
        switch (s)
        {
        case State::Pending:   return "Pending";
        case State::Ready:     return "Ready";
        case State::Running:   return "Running";
        case State::Completed: return "Completed";
        case State::Failed:    return "Failed";
        default:               return "Unknown";
        }
    }

private:
    uint32_t                    m_id;
    std::string                 m_name;
    std::function<void()>       m_work;          // 任务函数
    std::atomic<State>          m_state;
    std::atomic<int32_t>        m_pendingDeps;   // 未完成的依赖计数
    double                      m_durationMs;    // 执行耗时（毫秒）

    std::vector<TaskNode*>      m_dependencies;  // 前驱节点
    std::vector<TaskNode*>      m_dependents;    // 后继节点
};


// ============================================================
//  TaskFlow — 任务流图（DAG）
//  管理一组 TaskNode 及其依赖关系
//  参考 UE: FTaskGraphInterface 的任务拓扑
//  参考 taskflow: tf::Taskflow
// ============================================================
class TaskFlow
{
    friend class TaskExecutor;

public:
    explicit TaskFlow(const std::string& name = "Unnamed")
        : m_name(name), m_nextID(0)
    {}

    // --------------------------------------------------------
    //  添加任务节点
    // --------------------------------------------------------
    TaskNode& AddTask(const std::string& name, std::function<void()> work)
    {
        uint32_t id = m_nextID++;
        m_nodes.emplace_back(std::make_unique<TaskNode>(id, name, std::move(work)));
        return *m_nodes.back();
    }

    // --------------------------------------------------------
    //  便捷方法：创建顺序链
    //  MakeSequential(a, b, c) → a→b→c
    // --------------------------------------------------------
    template<typename... Nodes>
    void MakeSequential(TaskNode& first, TaskNode& second, Nodes&... rest)
    {
        second.DependsOn(first);
        if constexpr (sizeof...(rest) > 0)
            MakeSequential(second, rest...);
    }

    // --------------------------------------------------------
    //  便捷方法：创建并行组（全部完成后汇聚到 join 节点）
    //  MakeParallelJoin({a, b, c}, join) → a,b,c 并行 → join
    // --------------------------------------------------------
    void MakeParallelJoin(std::initializer_list<TaskNode*> parallels, TaskNode& join)
    {
        for (auto* node : parallels)
            join.DependsOn(*node);
    }

    // --------------------------------------------------------
    //  便捷方法：创建从 fork 分叉到并行组
    //  MakeForkParallel(fork, {a, b, c}) → fork → a,b,c
    // --------------------------------------------------------
    void MakeForkParallel(TaskNode& fork, std::initializer_list<TaskNode*> parallels)
    {
        for (auto* node : parallels)
            node->DependsOn(fork);
    }

    // --------------------------------------------------------
    //  查询接口
    // --------------------------------------------------------
    const std::string& GetName() const { return m_name; }
    size_t GetNodeCount() const { return m_nodes.size(); }

    TaskNode* FindTask(const std::string& name) const
    {
        for (auto& node : m_nodes)
            if (node->GetName() == name)
                return node.get();
        return nullptr;
    }

    // --------------------------------------------------------
    //  调试：生成 DOT 格式图（可用 Graphviz 可视化）
    // --------------------------------------------------------
    std::string ToDot() const
    {
        std::ostringstream oss;
        oss << "digraph \"" << m_name << "\" {\n";
        oss << "  rankdir=LR;\n";
        for (auto& node : m_nodes)
        {
            oss << "  \"" << node->GetName() << "\" [label=\"" << node->GetName()
                << "\\n" << TaskNode::StateToString(node->GetState()) << "\"];\n";
            for (auto* dep : node->GetDependents())
                oss << "  \"" << node->GetName() << "\" -> \"" << dep->GetName() << "\";\n";
        }
        oss << "}\n";
        return oss.str();
    }

    // --------------------------------------------------------
    //  调试：打印执行报告
    // --------------------------------------------------------
    std::string GetExecutionReport() const
    {
        std::ostringstream oss;
        oss << "TaskFlow '" << m_name << "' 执行报告 (" << m_nodes.size() << " 个任务):\n";
        oss << "  " << std::string(60, '-') << "\n";
        oss << "  " << std::left;

        // 表头
        char buf[256];
        snprintf(buf, sizeof(buf), "  %-20s %-12s %10s  %s\n",
            "任务名", "状态", "耗时(ms)", "依赖");
        oss << buf;
        oss << "  " << std::string(60, '-') << "\n";

        for (auto& node : m_nodes)
        {
            // 依赖列表
            std::string deps;
            for (auto* dep : node->GetDependencies())
            {
                if (!deps.empty()) deps += ",";
                deps += dep->GetName();
            }
            if (deps.empty()) deps = "(无)";

            snprintf(buf, sizeof(buf), "  %-20s %-12s %10.2f  %s\n",
                node->GetName().c_str(),
                TaskNode::StateToString(node->GetState()),
                node->GetDurationMs(),
                deps.c_str());
            oss << buf;
        }
        oss << "  " << std::string(60, '-') << "\n";
        return oss.str();
    }

    // 重置所有节点状态（允许重新执行）
    // 修复 Bug#7：Reset 后重新计算 pendingDeps 为依赖数量，
    //   确保 Reset 后节点状态与 Run() 前一致
    void Reset()
    {
        for (auto& node : m_nodes)
        {
            node->m_state = TaskNode::State::Pending;
            node->m_pendingDeps = static_cast<int32_t>(node->m_dependencies.size());
            node->m_durationMs = 0.0;
        }
    }

private:
    std::string                               m_name;
    uint32_t                                  m_nextID;
    std::vector<std::unique_ptr<TaskNode>>    m_nodes;
};


// ============================================================
//  TaskFlowFuture — 异步执行句柄
//  用于等待整个 TaskFlow 完成
//  参考 UE: FGraphEventRef
//  参考 Unity: JobHandle
// ============================================================
class TaskFlowFuture
{
    friend class TaskExecutor;

public:
    TaskFlowFuture() : m_completed(false) {}

    // 阻塞等待 Flow 完成
    void Wait()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condition.wait(lock, [this]() { return m_completed.load(); });
    }

    // 带超时的等待（返回是否在超时前完成）
    bool WaitFor(uint32_t timeoutMs)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_condition.wait_for(lock,
            std::chrono::milliseconds(timeoutMs),
            [this]() { return m_completed.load(); });
    }

    // 非阻塞查询
    bool IsCompleted() const { return m_completed.load(); }

private:
    void MarkCompleted()
    {
        m_completed = true;
        m_condition.notify_all();
    }

    std::atomic<bool>       m_completed;
    std::mutex              m_mutex;
    std::condition_variable m_condition;
};


// ============================================================
//  TaskExecutor — 任务执行器
//  驱动 TaskFlow 在 ThreadPool 上运行
//  参考 UE: FTaskGraphInterface / FNamedTaskThread
//  参考 taskflow: tf::Executor
// ============================================================
class TaskExecutor
{
public:
    // threadCount=0 时使用 hardware_concurrency
    explicit TaskExecutor(uint32_t threadCount = 0)
        : m_pool(threadCount)
    {}

    // 修复 Bug#2：析构时先 Shutdown 线程池，确保所有任务完成后再析构
    //   防止 lambda 中的 this 指针在任务未完成时悬空
    ~TaskExecutor()
    {
        m_pool.Shutdown();
    }

    // --------------------------------------------------------
    //  同步执行（阻塞直到完成）
    //  参考 taskflow: executor.run(flow).wait()
    // --------------------------------------------------------
    void RunAndWait(TaskFlow& flow)
    {
        auto future = Run(flow);
        future->Wait();
    }

    // --------------------------------------------------------
    //  异步执行（返回 Future 句柄）
    //  参考 UE: AsyncTask(ENamedThreads::AnyThread, ...)
    // --------------------------------------------------------
    std::shared_ptr<TaskFlowFuture> Run(TaskFlow& flow)
    {
        auto future = std::make_shared<TaskFlowFuture>();

        // 初始化：计算每个节点的待依赖计数
        size_t totalNodes = flow.m_nodes.size();
        if (totalNodes == 0)
        {
            future->MarkCompleted();
            return future;
        }

        auto completedCount = std::make_shared<std::atomic<size_t>>(0);

        for (auto& node : flow.m_nodes)
        {
            node->m_state = TaskNode::State::Pending;
            node->m_pendingDeps = static_cast<int32_t>(node->m_dependencies.size());
        }

        // 找出所有无依赖的根节点，直接提交执行
        for (auto& node : flow.m_nodes)
        {
            if (node->m_pendingDeps == 0)
            {
                node->m_state = TaskNode::State::Ready;
                ScheduleNode(node.get(), future, completedCount, totalNodes);
            }
        }

        return future;
    }

    // --------------------------------------------------------
    //  查询接口
    // --------------------------------------------------------
    uint32_t GetThreadCount() const { return m_pool.GetThreadCount(); }
    std::string GetDebugInfo() const { return "TaskExecutor[" + m_pool.GetDebugInfo() + "]"; }

private:
    // --------------------------------------------------------
    //  调度单个节点到线程池
    // --------------------------------------------------------
    // 修复 Bug#1：移除 &flow 引用捕获（lambda 中不需要 flow 参数），
    //   避免 TaskFlow 对象在任务完成前被销毁导致悬空引用
    void ScheduleNode(TaskNode* node,
                      std::shared_ptr<TaskFlowFuture> future,
                      std::shared_ptr<std::atomic<size_t>> completedCount,
                      size_t totalNodes)
    {
        m_pool.SubmitTask([this, node, future, completedCount, totalNodes]()
        {
            // 执行任务
            node->m_state = TaskNode::State::Running;
            auto startTime = std::chrono::high_resolution_clock::now();

            try
            {
                if (node->m_work)
                    node->m_work();

                node->m_state = TaskNode::State::Completed;
            }
            catch (const std::exception& e)
            {
                node->m_state = TaskNode::State::Failed;
                std::cerr << "[TaskFlow] 任务 '" << node->GetName()
                          << "' 执行失败: " << e.what() << "\n";
            }

            auto endTime = std::chrono::high_resolution_clock::now();
            node->m_durationMs = std::chrono::duration<double, std::milli>(
                endTime - startTime).count();

            // 通知后继节点
            for (auto* dependent : node->m_dependents)
            {
                int32_t remaining = --dependent->m_pendingDeps;
                if (remaining == 0)
                {
                    // 所有依赖已满足，调度此节点
                    dependent->m_state = TaskNode::State::Ready;
                    ScheduleNode(dependent, future, completedCount, totalNodes);
                }
            }

            // 检查是否所有节点都已完成
            size_t done = completedCount->fetch_add(1) + 1;
            if (done == totalNodes)
            {
                future->MarkCompleted();
            }
        });
    }

    ThreadPool m_pool;
};
