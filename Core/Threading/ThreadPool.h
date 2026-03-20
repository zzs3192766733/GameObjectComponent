#pragma once
// ============================================================
//  ThreadPool — 轻量级线程池
//
//  设计参考：
//    - UE: FQueuedThreadPool / FTaskGraphInterface
//    - Unity: JobSystem (C# Job System)
//    - Intel TBB task_group
//
//  特性：
//    - 固定线程数（默认 hardware_concurrency）
//    - 基于 std::function 的任务提交
//    - 支持 std::future 获取异步结果
//    - 支持 WaitAll 等待所有任务完成
//    - RAII 设计：析构时自动停止所有工作线程
//
//  使用示例：
//    ThreadPool pool(4);
//    auto f = pool.Submit([]{ return 42; });
//    int result = f.get();  // 阻塞等待结果
// ============================================================

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <iostream>

class ThreadPool
{
public:
    // --------------------------------------------------------
    //  构造 / 析构
    // --------------------------------------------------------

    // 创建线程池，threadCount=0 时使用 hardware_concurrency
    explicit ThreadPool(uint32_t threadCount = 0)
        : m_stopping(false)
        , m_activeTasks(0)
    {
        if (threadCount == 0)
            threadCount = std::max(2u, std::thread::hardware_concurrency());

        m_workers.reserve(threadCount);
        for (uint32_t i = 0; i < threadCount; ++i)
        {
            m_workers.emplace_back([this, i]() { WorkerLoop(i); });
        }
    }

    // RAII：析构时停止所有线程
    ~ThreadPool()
    {
        Shutdown();
    }

    // 禁止拷贝和移动（线程池是唯一所有者）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // --------------------------------------------------------
    //  提交任务（带返回值）
    //  Submit(callable) → std::future<ReturnType>
    //  参考 UE: FTaskGraphInterface::GetInstance().CreateTask(...)
    // --------------------------------------------------------
    template<typename Func, typename... Args>
    auto Submit(Func&& func, Args&&... args)
        -> std::future<typename std::invoke_result<Func, Args...>::type>
    {
        using ReturnType = typename std::invoke_result<Func, Args...>::type;

        // 将 callable 包装为 std::packaged_task
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );

        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping)
                throw std::runtime_error("ThreadPool: 无法向已停止的线程池提交任务");

            ++m_activeTasks;
            m_tasks.emplace([task]() { (*task)(); });
        }
        m_condition.notify_one();

        return result;
    }

    // --------------------------------------------------------
    //  提交无返回值的轻量任务
    //  SubmitTask(callable) → void
    //  比 Submit 少一次 packaged_task 的开销
    // --------------------------------------------------------
    void SubmitTask(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping)
                throw std::runtime_error("ThreadPool: 无法向已停止的线程池提交任务");

            ++m_activeTasks;
            m_tasks.emplace(std::move(task));
        }
        m_condition.notify_one();
    }

    // --------------------------------------------------------
    //  等待所有已提交的任务完成
    //  参考 UE: FTaskGraphInterface::WaitUntilTasksComplete
    // --------------------------------------------------------
    void WaitAll()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_doneCondition.wait(lock, [this]() {
            return m_activeTasks == 0 && m_tasks.empty();
        });
    }

    // --------------------------------------------------------
    //  查询接口
    // --------------------------------------------------------
    uint32_t GetThreadCount() const { return static_cast<uint32_t>(m_workers.size()); }
    uint32_t GetPendingTaskCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<uint32_t>(m_tasks.size());
    }
    uint32_t GetActiveTaskCount() const { return m_activeTasks.load(); }
    bool     IsStopping() const { return m_stopping.load(); }

    // --------------------------------------------------------
    //  手动关闭（析构也会自动调用）
    // --------------------------------------------------------
    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopping) return;
            m_stopping = true;
        }
        m_condition.notify_all();

        for (auto& worker : m_workers)
        {
            if (worker.joinable())
                worker.join();
        }
        m_workers.clear();
    }

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    std::string GetDebugInfo() const
    {
        return "ThreadPool[threads=" + std::to_string(m_workers.size())
             + ", pending=" + std::to_string(GetPendingTaskCount())
             + ", active=" + std::to_string(m_activeTasks.load()) + "]";
    }

private:
    // --------------------------------------------------------
    //  工作线程循环
    // --------------------------------------------------------
    void WorkerLoop(uint32_t /*threadIndex*/)
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_condition.wait(lock, [this]() {
                    return m_stopping || !m_tasks.empty();
                });

                if (m_stopping && m_tasks.empty())
                    return;

                task = std::move(m_tasks.front());
                m_tasks.pop();
            }

            // 执行任务
            task();

            // 修复 Bug#3：将 --m_activeTasks 放入 mutex 保护下，
            //   确保 WaitAll 中 predicate 检查 m_activeTasks 和 m_tasks.empty() 的原子性一致，
            //   消除 activeTasks 递减与新任务入队之间的竞态窗口
            {
                std::lock_guard<std::mutex> doneLock(m_mutex);
                --m_activeTasks;
            }
            m_doneCondition.notify_all();
        }
    }

    // --------------------------------------------------------
    //  成员变量
    // --------------------------------------------------------
    std::vector<std::thread>            m_workers;        // 工作线程
    std::queue<std::function<void()>>   m_tasks;          // 任务队列
    mutable std::mutex                  m_mutex;          // 队列锁
    std::condition_variable             m_condition;      // 有新任务时唤醒工作线程
    std::condition_variable             m_doneCondition;  // WaitAll 等待条件
    std::atomic<bool>                   m_stopping;       // 停止标志
    std::atomic<uint32_t>               m_activeTasks;    // 正在执行 + 队列中的任务数
};
