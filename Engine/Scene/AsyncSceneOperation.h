#pragma once
#include "Scene.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <iostream>

// 修复 #3：将 LoadSceneMode 枚举单独定义在此处，避免循环包含
// AsyncSceneOperation 和 SceneManager 均引用此头文件
enum class LoadSceneMode
{
    Single,     // 替换模式：卸载全部已加载场景，加载新场景
    Additive    // 叠加模式：保留已有场景，额外加载新场景
};

// ============================================================
//  异步加载状态枚举
//
//  描述一次异步场景加载操作的生命周期阶段
//  参考：
//    Unity  - AsyncOperation 的 isDone / progress / allowSceneActivation
//    UE     - FAsyncLoadingThread 的加载状态
// ============================================================
enum class AsyncLoadState
{
    Pending,        // 已创建，等待开始
    LoadingScreen,  // Loading 场景已显示，准备开始构建
    Building,       // 正在分步构建目标场景（每帧执行 N 个步骤）
    Ready,          // 构建完成，等待激活许可
    Activating,     // 正在执行场景切换（卸载旧场景 + Loading，激活新场景）
    Done            // 全部完成
};

// ============================================================
//  加载步骤类型
//  每个 LoadStep 是一个小粒度的工作单元，
//  异步加载时每帧执行若干个 LoadStep，避免单帧卡顿
//
//  参考 Unity 协程中的 yield return 分帧逻辑
// ============================================================
using LoadStep = std::function<void()>;

// ============================================================
//  分步场景工厂类型
//  与现有的 SceneFactory（一口气构建）并存，
//  SceneStepFactory 将构建过程拆分为多个 LoadStep
//
//  参数1: steps 向量引用 — 工厂往里面添加加载步骤
//  参数2: scene 引用     — 正在构建的目标场景
// ============================================================
using SceneStepFactory = std::function<void(std::vector<LoadStep>&, Scene&)>;

// ============================================================
//  AsyncSceneOperation（异步场景加载操作）
//
//  描述一次从"发起异步加载"到"新场景完全就绪"的全过程
//
//  设计参考：
//    Unity  - AsyncOperation / SceneManager.LoadSceneAsync
//             进度查询 (.progress)、激活控制 (.allowSceneActivation)
//    UE     - FStreamableHandle 的进度/完成回调机制
//             UGameplayStatics::OpenLevelBySoftObjectPtr 异步关卡加载
//
//  核心机制：
//    - 状态机驱动（Pending → LoadingScreen → Building → Ready → Activating → Done）
//    - 分帧加载：Building 阶段每帧执行帧预算数量的 LoadStep
//    - 激活控制：可设置 allowActivation=false 让加载完后停在 Ready 状态
//                （例如等玩家按"任意键继续"）
//
//  所有权模型：
//    AsyncSceneOperation 由 SceneManager 拥有（unique_ptr）
//    内部持有目标场景的 unique_ptr，在 Activating 阶段移交给 SceneManager
// ============================================================
class AsyncSceneOperation
{
public:
    // --------------------------------------------------------
    //  构造
    //  targetName: 目标场景名称
    //  mode:       加载模式（Single/Additive）
    // --------------------------------------------------------
    AsyncSceneOperation(const std::string& targetName, LoadSceneMode mode)
        : m_targetSceneName(targetName)
        , m_mode(mode)
    {
    }

    ~AsyncSceneOperation() = default;

    // 禁止拷贝
    AsyncSceneOperation(const AsyncSceneOperation&) = delete;
    AsyncSceneOperation& operator=(const AsyncSceneOperation&) = delete;

    // --------------------------------------------------------
    //  公开查询 API
    // --------------------------------------------------------

    // 获取加载进度 (0.0 ~ 1.0)
    // - Pending/LoadingScreen 阶段返回 0.0
    // - Building 阶段返回 已完成步骤数 / 总步骤数
    // - Ready/Activating/Done 返回 1.0
    float GetProgress() const
    {
        switch (m_state)
        {
        case AsyncLoadState::Pending:
        case AsyncLoadState::LoadingScreen:
            return 0.0f;

        case AsyncLoadState::Building:
            if (m_totalSteps == 0) return 0.0f;
            return static_cast<float>(m_currentStep) / static_cast<float>(m_totalSteps);

        case AsyncLoadState::Ready:
        case AsyncLoadState::Activating:
        case AsyncLoadState::Done:
            return 1.0f;
        }
        return 0.0f;
    }

    // 获取当前状态
    AsyncLoadState GetState() const { return m_state; }

    // 是否全部完成
    bool IsDone() const { return m_state == AsyncLoadState::Done; }

    // --------------------------------------------------------
    //  激活控制
    //  参考 Unity AsyncOperation.allowSceneActivation
    //
    //  false: 加载完成后停在 Ready 状态，不自动切换
    //         适用于"加载完毕后等玩家按任意键"的场景
    //  true:  加载完成后立即切换（默认值）
    // --------------------------------------------------------
    void SetAllowActivation(bool allow) { m_allowActivation = allow; }
    bool GetAllowActivation() const { return m_allowActivation; }

    // --------------------------------------------------------
    //  完成回调
    //  场景切换完成后触发（状态变为 Done）
    //  参考 Unity AsyncOperation.completed 事件
    // --------------------------------------------------------
    using CompletionCallback = std::function<void()>;
    void SetOnCompleted(CompletionCallback cb) { m_onCompleted = std::move(cb); }

    // --------------------------------------------------------
    //  目标场景信息
    // --------------------------------------------------------
    const std::string& GetTargetSceneName() const { return m_targetSceneName; }
    LoadSceneMode GetMode() const { return m_mode; }

    // --------------------------------------------------------
    //  以下为 SceneManager 内部调用的方法（friend 或 public）
    //  用户代码不应直接调用
    // --------------------------------------------------------

    // 状态转换
    void TransitionTo(AsyncLoadState newState)
    {
        std::cout << "[AsyncSceneOp] 状态转换: "
                  << StateToString(m_state) << " → " << StateToString(newState) << "\n";
        m_state = newState;
    }

    // 初始化构建阶段：创建空场景 + 填充加载步骤
    void BeginBuilding(std::unique_ptr<Scene> scene, const SceneStepFactory& stepFactory)
    {
        m_newScene = std::move(scene);
        m_loadSteps.clear();
        m_currentStep = 0;

        // 调用分步工厂，让用户填充加载步骤
        stepFactory(m_loadSteps, *m_newScene);

        m_totalSteps = static_cast<int>(m_loadSteps.size());
        std::cout << "[AsyncSceneOp] 构建开始: " << m_totalSteps << " 个加载步骤\n";
    }

    // 执行下一个加载步骤
    void ExecuteNextStep()
    {
        if (m_currentStep < m_totalSteps)
        {
            m_loadSteps[m_currentStep]();
            ++m_currentStep;
        }
    }

    // 是否所有步骤都已完成
    bool IsAllStepsDone() const { return m_currentStep >= m_totalSteps; }

    // 触发完成回调
    void InvokeCompletionCallback()
    {
        if (m_onCompleted)
            m_onCompleted();
    }

    // 移交新场景的所有权给 SceneManager
    std::unique_ptr<Scene> TakeNewScene() { return std::move(m_newScene); }

private:
    // 状态名称（调试用）
    static const char* StateToString(AsyncLoadState state)
    {
        switch (state)
        {
        case AsyncLoadState::Pending:       return "Pending";
        case AsyncLoadState::LoadingScreen: return "LoadingScreen";
        case AsyncLoadState::Building:      return "Building";
        case AsyncLoadState::Ready:         return "Ready";
        case AsyncLoadState::Activating:    return "Activating";
        case AsyncLoadState::Done:          return "Done";
        }
        return "Unknown";
    }

private:
    // --------------------------------------------------------
    //  核心数据
    // --------------------------------------------------------

    // 目标场景名称
    std::string m_targetSceneName;

    // 加载模式（修复 #3：使用强类型枚举替代裸 int，提供编译期类型检查）
    LoadSceneMode m_mode = LoadSceneMode::Single;

    // 当前状态
    AsyncLoadState m_state = AsyncLoadState::Pending;

    // 是否允许加载完成后自动激活
    bool m_allowActivation = true;

    // 分步加载任务列表
    std::vector<LoadStep> m_loadSteps;

    // 当前步骤 / 总步骤
    int m_currentStep = 0;
    int m_totalSteps  = 0;

    // 预构建的新场景（Building 阶段构建，Activating 阶段移交）
    std::unique_ptr<Scene> m_newScene;

    // 完成回调
    CompletionCallback m_onCompleted;
};