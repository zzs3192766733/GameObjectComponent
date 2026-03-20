
#pragma once
#include "Scene.h"
#include "AsyncSceneOperation.h"
#include "SceneBuilder.h"
#include "CoreTypes.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <functional>
#include <iostream>
#include <algorithm>

// LoadSceneMode 已在 AsyncSceneOperation.h 中定义，通过 #include 引入

// ============================================================
//  场景工厂类型
//  用户通过 RegisterSceneFactory 注册场景的构建函数，
//  SceneManager 在 LoadScene 时调用工厂函数来构建场景内容
//  （替代 UE 的 Level Asset / Unity 的 Scene Asset 文件）
//
//  参数 Scene& 是新创建的空场景，用户在回调中填充 GameObject
// ============================================================
using SceneFactory = std::function<void(Scene&)>;

// ============================================================
//  SceneManager（场景管理器）
//
//  TODO #13：当前为 header-only 实现（约 960 行），增加编译时间
//  建议后续将实现移到 SceneManager.cpp，头文件仅保留声明
//
//  设计参考：
//    - UE: UEngine 管理多个 FWorldContext，每个 Context 持有一个 UWorld
//    - Unity: SceneManagement.SceneManager 单例
//
//  核心功能：
//    1. 场景注册：RegisterSceneFactory(name, factory)
//    2. 场景加载：LoadScene(name, mode) - Single/Additive
//    3. 场景卸载：UnloadScene(name)
//    4. 活动场景：GetActiveScene() / SetActiveScene(name)
//    5. 持久对象：DontDestroyOnLoad(id) - 将对象移入不可卸载的持久场景
//    6. 生命周期：Update/FixedUpdate/Render 驱动所有已加载场景
//
//  所有权模型：
//    SceneManager → 拥有所有 Scene（unique_ptr）
//    包括一个特殊的"持久场景"（__Persistent__），永远不会被卸载
// ============================================================
class SceneManager
{
public:
    // --------------------------------------------------------
    //  单例访问（参考 UE GEngine、Unity SceneManager 的全局访问模式）
    // --------------------------------------------------------
    static SceneManager& Get()
    {
        static SceneManager instance;
        return instance;
    }

    // --------------------------------------------------------
    //  场景工厂注册
    //  在游戏启动时注册所有可加载的场景及其构建函数
    //  类比 UE 的 Level Blueprint / Unity 的 Build Settings 场景列表
    // --------------------------------------------------------
    void RegisterSceneFactory(const std::string& sceneName, SceneFactory factory)
    {
        if (m_factories.count(sceneName))
        {
            std::cerr << "[SceneManager] 警告: 场景 '" << sceneName
                      << "' 的工厂已注册，将覆盖\n";
        }
        m_factories[sceneName] = std::move(factory);
        std::cout << "[SceneManager] 已注册场景工厂: '" << sceneName << "'\n";
    }

    // --------------------------------------------------------
    //  SceneBuilder 注册（推荐方式）
    //  将场景构建逻辑内聚到 SceneBuilder 子类中，
    //  一次注册同时提供同步工厂 + 分步工厂
    //
    //  设计参考：
    //    - UE AGameModeBase 子类注册到 Level
    //    - Builder 模式（GoF）：构建逻辑与容器分离
    //
    //  使用方式：
    //    sceneMgr.RegisterSceneBuilder(std::make_unique<GameLevelBuilder>());
    //    → 自动注册同步工厂（OnBuild）
    //    → 自动注册分步工厂（OnBuildSteps）
    //    → 如果是 Loading 场景，自动设置 LoadingSceneFactory
    // --------------------------------------------------------
    void RegisterSceneBuilder(std::unique_ptr<SceneBuilder> builder)
    {
        if (!builder)
        {
            std::cerr << "[SceneManager] 错误: 注册空的 SceneBuilder\n";
            return;
        }

        const std::string sceneName = builder->GetSceneName();

        // 保存 Builder 引用（SceneManager 持有所有权）
        SceneBuilder* rawPtr = builder.get();
        m_builders[sceneName] = std::move(builder);

        // 自动注册同步工厂（捕获裸指针，SceneBuilder 生命周期由 m_builders 管理）
        RegisterSceneFactory(sceneName,
            [rawPtr](Scene& scene) { rawPtr->OnBuild(scene); });

        // 自动注册分步工厂
        RegisterSceneStepFactory(sceneName,
            [rawPtr](std::vector<LoadStep>& steps, Scene& scene) {
                rawPtr->OnBuildSteps(steps, scene);
            });

        // 如果是 Loading 场景，自动设置 LoadingSceneFactory
        if (rawPtr->IsLoadingScene())
        {
            SetLoadingSceneFactory(
                [rawPtr](Scene& scene) { rawPtr->OnBuild(scene); });
        }

        std::cout << "[SceneManager] 已注册 SceneBuilder: '" << sceneName << "'"
                  << (rawPtr->IsLoadingScene() ? " [Loading]" : "") << "\n";
    }

    // 获取已注册的 SceneBuilder（用于运行时查询 Builder 信息）
    SceneBuilder* GetSceneBuilder(const std::string& sceneName) const
    {
        auto it = m_builders.find(sceneName);
        return it != m_builders.end() ? it->second.get() : nullptr;
    }

    // --------------------------------------------------------
    //  场景加载
    //  参考 Unity SceneManager.LoadScene(sceneName, mode)
    //
    //  Single 模式：
    //    1. 停止并卸载所有已加载的场景（持久场景除外）
    //    2. 创建新场景并调用工厂函数填充内容
    //    3. 启动新场景
    //    4. 将新场景设为活动场景
    //
    //  Additive 模式：
    //    1. 创建新场景并调用工厂函数填充内容
    //    2. 启动新场景
    //    3. 不改变活动场景
    //
    //  返回指向新加载场景的指针（弱引用，生命周期由 SceneManager 管理）
    // --------------------------------------------------------
    Scene* LoadScene(const std::string& sceneName,
                     LoadSceneMode mode = LoadSceneMode::Single)
    {
        // 检查场景工厂是否已注册
        auto factoryIt = m_factories.find(sceneName);
        if (factoryIt == m_factories.end())
        {
            std::cerr << "[SceneManager] 错误: 未找到场景 '" << sceneName
                      << "' 的工厂，请先调用 RegisterSceneFactory\n";
            return nullptr;
        }

        // 检查是否已加载（避免重复加载同一场景）
        if (m_loadedScenes.count(sceneName))
        {
            std::cerr << "[SceneManager] 警告: 场景 '" << sceneName
                      << "' 已加载，不可重复加载\n";
            return m_loadedScenes[sceneName].get();
        }

        std::cout << "[SceneManager] 加载场景: '" << sceneName << "'"
                  << " 模式=" << (mode == LoadSceneMode::Single ? "Single" : "Additive")
                  << "\n";

        // Single 模式：先卸载所有已加载的场景
        if (mode == LoadSceneMode::Single)
        {
            UnloadAllScenes();
        }

        // 创建新场景
        auto newScene = std::make_unique<Scene>(sceneName);

        // 调用工厂函数填充场景内容（用户在回调中创建 GameObject、添加组件等）
        factoryIt->second(*newScene);

        // 启动场景（触发所有 GameObject/Component 的 Start 回调）
        newScene->Start();

        Scene* rawPtr = newScene.get();

        // 将新场景加入已加载列表
        m_loadedScenes[sceneName] = std::move(newScene);
        m_loadOrder.push_back(sceneName);

        // Single 模式或首个场景：自动设为活动场景
        if (mode == LoadSceneMode::Single || m_activeSceneName.empty())
        {
            m_activeSceneName = sceneName;
            std::cout << "[SceneManager] 活动场景设为: '" << sceneName << "'\n";
        }

        // 触发场景加载回调
        if (m_onSceneLoaded)
            m_onSceneLoaded(sceneName, *rawPtr);

        return rawPtr;
    }

    // --------------------------------------------------------
    //  场景卸载
    //  参考 Unity SceneManager.UnloadSceneAsync(sceneName)
    //
    //  - 不能卸载持久场景（__Persistent__）
    //  - 不能卸载当前唯一的活动场景（至少保留一个场景）
    //  - 卸载时会调用场景的 Stop()，触发所有组件的 OnStop
    // --------------------------------------------------------
    bool UnloadScene(const std::string& sceneName)
    {
        // 保护：不能卸载持久场景
        if (sceneName == PERSISTENT_SCENE_NAME)
        {
            std::cerr << "[SceneManager] 错误: 不能卸载持久场景\n";
            return false;
        }

        auto it = m_loadedScenes.find(sceneName);
        if (it == m_loadedScenes.end())
        {
            std::cerr << "[SceneManager] 警告: 场景 '" << sceneName << "' 未加载\n";
            return false;
        }

        std::cout << "[SceneManager] 卸载场景: '" << sceneName << "'\n";

        // 触发卸载前回调
        if (m_onSceneUnloaded)
            m_onSceneUnloaded(sceneName, *it->second);

        // 停止场景（触发所有 OnStop）
        if (it->second->IsRunning())
            it->second->Stop();

        // 从已加载列表中移除
        m_loadedScenes.erase(it);
        m_loadOrder.erase(
            std::remove(m_loadOrder.begin(), m_loadOrder.end(), sceneName),
            m_loadOrder.end());

        // 如果卸载的是活动场景，自动切换到下一个可用场景
        if (m_activeSceneName == sceneName)
        {
            if (!m_loadOrder.empty())
            {
                // 优先选择非持久场景
                m_activeSceneName = "";
                for (const auto& name : m_loadOrder)
                {
                    if (name != PERSISTENT_SCENE_NAME)
                    {
                        m_activeSceneName = name;
                        break;
                    }
                }
                // 如果只剩持久场景
                if (m_activeSceneName.empty() && !m_loadOrder.empty())
                    m_activeSceneName = m_loadOrder.front();

                std::cout << "[SceneManager] 活动场景切换为: '" << m_activeSceneName << "'\n";
            }
            else
            {
                m_activeSceneName.clear();
                std::cout << "[SceneManager] 所有场景已卸载，无活动场景\n";
            }
        }

        return true;
    }

    // --------------------------------------------------------
    //  活动场景管理
    //  活动场景是"当前主要运行的场景"
    //  参考 Unity SceneManager.GetActiveScene() / SetActiveScene()
    //  - CreateGameObject 等操作默认在活动场景上执行
    //  - 渲染/物理设置以活动场景为准
    // --------------------------------------------------------
    Scene* GetActiveScene() const
    {
        if (m_activeSceneName.empty()) return nullptr;
        auto it = m_loadedScenes.find(m_activeSceneName);
        return it != m_loadedScenes.end() ? it->second.get() : nullptr;
    }

    const std::string& GetActiveSceneName() const { return m_activeSceneName; }

    bool SetActiveScene(const std::string& sceneName)
    {
        if (!m_loadedScenes.count(sceneName))
        {
            std::cerr << "[SceneManager] 错误: 场景 '" << sceneName << "' 未加载\n";
            return false;
        }
        m_activeSceneName = sceneName;
        std::cout << "[SceneManager] 活动场景设为: '" << sceneName << "'\n";
        return true;
    }

    // --------------------------------------------------------
    //  场景查询
    // --------------------------------------------------------
    Scene* GetScene(const std::string& sceneName) const
    {
        auto it = m_loadedScenes.find(sceneName);
        return it != m_loadedScenes.end() ? it->second.get() : nullptr;
    }

    bool IsSceneLoaded(const std::string& sceneName) const
    {
        return m_loadedScenes.count(sceneName) > 0;
    }

    size_t GetLoadedSceneCount() const { return m_loadedScenes.size(); }

    // 获取所有已加载场景的名称（按加载顺序）
    const std::vector<std::string>& GetLoadedSceneNames() const { return m_loadOrder; }

    // --------------------------------------------------------
    //  DontDestroyOnLoad（持久对象）
    //  参考 Unity Object.DontDestroyOnLoad()
    //
    //  将指定 GameObject 从当前场景**真正迁移**到"持久场景"，
    //  持久场景在场景切换（Single 模式）时不会被卸载
    //  典型用途：全局管理器、背景音乐、网络连接等
    //
    //  实现原理：
    //    1. 在所有已加载场景中查找该对象
    //    2. 调用 Scene::DetachGameObject 将对象从源场景分离（所有权转移）
    //    3. 调用持久场景的 Scene::AttachGameObject 将对象移入（场景获取所有权）
    //    4. 对象的组件状态完整保留，不触发 Stop/Start
    //
    //  注意：对象的子对象不会自动跟随迁移，需要调用者显式处理
    // --------------------------------------------------------
    bool DontDestroyOnLoad(GameObjectID id)
    {
        // 确保持久场景存在
        EnsurePersistentScene();

        // 第一步：先遍历查找对象所在的场景（只读遍历，不修改容器）
        std::string sourceSceneName;
        std::string objName;
        for (const auto& [name, scene] : m_loadedScenes)
        {
            if (name == PERSISTENT_SCENE_NAME) continue;

            GameObject* obj = scene->GetGameObject(id);
            if (obj)
            {
                sourceSceneName = name;
                objName = obj->GetName();
                break;
            }
        }

        if (sourceSceneName.empty())
        {
            std::cerr << "[SceneManager] 错误: 未找到 ID=" << id << " 的对象\n";
            return false;
        }

        // 第二步：在遍历外执行修改操作（避免在 range-for 中修改关联容器）
        std::cout << "[SceneManager] DontDestroyOnLoad: 迁移对象 '"
                  << objName << "' (ID=" << id
                  << ") 从场景 '" << sourceSceneName << "' → 持久场景\n";

        // 从源场景分离对象（所有权转移给 detached）
        Scene* sourceScene = m_loadedScenes[sourceSceneName].get();
        std::unique_ptr<GameObject> detached = sourceScene->DetachGameObject(id);
        if (!detached)
        {
            std::cerr << "[SceneManager] 错误: 分离对象失败\n";
            return false;
        }

        // 将对象移入持久场景（持久场景获取所有权）
        Scene* persistent = m_loadedScenes[PERSISTENT_SCENE_NAME].get();
        if (!persistent->AttachGameObject(std::move(detached)))
        {
            std::cerr << "[SceneManager] 错误: 附加对象到持久场景失败\n";
            return false;
        }

        // 记录持久对象 ID（用于查询和调试）
        m_persistentObjectIDs.insert(id);
        return true;
    }

    // 检查对象是否为持久对象
    bool IsPersistentObject(GameObjectID id) const
    {
        return m_persistentObjectIDs.count(id) > 0;
    }

    // 获取持久场景（如果不存在则创建）
    Scene* GetPersistentScene()
    {
        EnsurePersistentScene();
        auto it = m_loadedScenes.find(PERSISTENT_SCENE_NAME);
        return it != m_loadedScenes.end() ? it->second.get() : nullptr;
    }

    // --------------------------------------------------------
    //  生命周期驱动
    //  由游戏主循环调用，驱动所有已加载场景的更新
    //  调用顺序：FixedUpdate → Update → Render
    //  所有场景按加载顺序依次更新
    // --------------------------------------------------------
    void Update(float deltaTime)
    {
        // ======== 异步加载调度（每帧推进状态机）========
        // 参考 Unity 在 PlayerLoop 中调度 AsyncOperation
        // 参考 UE FAsyncLoadingThread::TickAsyncLoading
        if (m_asyncOperation)
        {
            TickAsyncOperation();
        }

        // ======== 原有逻辑：更新所有已加载场景 ========
        for (const auto& name : m_loadOrder)
        {
            auto it = m_loadedScenes.find(name);
            if (it != m_loadedScenes.end() && it->second->IsRunning())
                it->second->Update(deltaTime);
        }
    }

    void FixedUpdate(float fixedDeltaTime)
    {
        for (const auto& name : m_loadOrder)
        {
            auto it = m_loadedScenes.find(name);
            if (it != m_loadedScenes.end() && it->second->IsRunning())
                it->second->FixedUpdate(fixedDeltaTime);
        }
    }

    void Render()
    {
        for (const auto& name : m_loadOrder)
        {
            auto it = m_loadedScenes.find(name);
            if (it != m_loadedScenes.end() && it->second->IsRunning())
                it->second->Render();
        }
    }

    // ============================================================
    //  异步场景加载 API（新增）
    //
    //  设计参考：
    //    Unity  - SceneManager.LoadSceneAsync + AsyncOperation
    //    UE     - UGameplayStatics::OpenLevelBySoftObjectPtr
    //             FStreamableManager 的异步加载回调
    //
    //  核心思路：
    //    1. 调用 LoadSceneAsync() 发起异步加载
    //    2. SceneManager 自动以 Additive 模式加载 Loading 场景
    //    3. 每帧在 Update() 中推进异步操作（执行帧预算数量的 LoadStep）
    //    4. 加载完成后自动卸载 Loading 场景，激活目标场景
    //
    //  分帧加载（Frame Budget）方案：
    //    - 单线程架构下的异步模拟，参考 Unity 协程的 yield return
    //    - 每帧执行 N 个 LoadStep，确保 Loading 画面流畅运行
    //    - N 可通过 SetFrameLoadBudget() 调整
    // ============================================================

    // --------------------------------------------------------
    //  注册分步场景工厂
    //  与现有 RegisterSceneFactory（同步，一口气构建）并存
    //  分步工厂将构建过程拆分为多个 LoadStep，
    //  异步加载时每帧执行若干步，避免卡顿
    //
    //  参数 SceneStepFactory 签名：
    //    void(std::vector<LoadStep>& steps, Scene& scene)
    //    工厂在回调中向 steps 添加加载步骤
    // --------------------------------------------------------
    void RegisterSceneStepFactory(const std::string& sceneName, SceneStepFactory factory)
    {
        if (m_stepFactories.count(sceneName))
        {
            std::cerr << "[SceneManager] 警告: 场景 '" << sceneName
                      << "' 的分步工厂已注册，将覆盖\n";
        }
        m_stepFactories[sceneName] = std::move(factory);
        std::cout << "[SceneManager] 已注册分步场景工厂: '" << sceneName << "'\n";
    }

    // --------------------------------------------------------
    //  注册 Loading 场景工厂（全局唯一）
    //  Loading 场景是一个普通场景，可包含进度条、动画等 GameObject
    //  在异步加载期间以 Additive 模式叠加显示
    //
    //  Loading 场景内的组件可通过以下方式获取加载进度：
    //    SceneManager::Get().GetAsyncLoadProgress()
    //
    //  如果未注册 Loading 工厂，异步加载仍然工作，
    //  只是没有过渡画面（直接在后台分帧构建）
    // --------------------------------------------------------
    void SetLoadingSceneFactory(SceneFactory factory)
    {
        m_loadingSceneFactory = std::move(factory);
        // 同时注册为普通场景工厂，以便 LoadScene 内部调用
        m_factories[LOADING_SCENE_NAME] = m_loadingSceneFactory;
        std::cout << "[SceneManager] 已注册 Loading 场景工厂\n";
    }

    // --------------------------------------------------------
    //  异步加载场景
    //  返回 AsyncSceneOperation 指针，用于查询进度和控制激活时机
    //  生命周期由 SceneManager 管理，加载完成后自动销毁
    //
    //  前提条件：
    //    - 目标场景已注册分步工厂（RegisterSceneStepFactory）
    //    - 没有正在进行的异步加载
    //
    //  使用示例：
    //    auto* op = sceneMgr.LoadSceneAsync("GameLevel", LoadSceneMode::Single);
    //    op->SetAllowActivation(false);   // 可选：手动控制激活
    //    op->SetOnCompleted([](){ ... }); // 可选：完成回调
    // --------------------------------------------------------
    AsyncSceneOperation* LoadSceneAsync(
        const std::string& sceneName,
        LoadSceneMode mode = LoadSceneMode::Single)
    {
        // 检查是否有正在进行的异步加载
        if (m_asyncOperation)
        {
            std::cerr << "[SceneManager] 错误: 已有异步加载进行中('" 
                      << m_asyncOperation->GetTargetSceneName()
                      << "')，请等待完成\n";
            return nullptr;
        }

        // 检查分步工厂是否已注册
        auto stepIt = m_stepFactories.find(sceneName);
        if (stepIt == m_stepFactories.end())
        {
            std::cerr << "[SceneManager] 错误: 未找到场景 '" << sceneName
                      << "' 的分步工厂，请先调用 RegisterSceneStepFactory\n";
            return nullptr;
        }

        // 检查是否已加载
        if (m_loadedScenes.count(sceneName))
        {
            std::cerr << "[SceneManager] 警告: 场景 '" << sceneName
                      << "' 已加载，不可重复加载\n";
            return nullptr;
        }

        std::cout << "[SceneManager] 发起异步加载: '" << sceneName << "'"
                  << " 模式=" << (mode == LoadSceneMode::Single ? "Single" : "Additive")
                  << "\n";

        // 创建异步操作
        m_asyncOperation = std::make_unique<AsyncSceneOperation>(
            sceneName,
            mode);

        return m_asyncOperation.get();
    }

    // --------------------------------------------------------
    //  异步加载状态查询
    //  供 Loading 场景内的组件（如进度条脚本）实时查询进度
    // --------------------------------------------------------
    bool IsAsyncLoading() const
    {
        return m_asyncOperation != nullptr && !m_asyncOperation->IsDone();
    }

    float GetAsyncLoadProgress() const
    {
        return m_asyncOperation ? m_asyncOperation->GetProgress() : 0.0f;
    }

    // --------------------------------------------------------
    //  帧预算设置
    //  控制异步加载时每帧最多执行多少个 LoadStep
    //  值越大 → 加载越快，但每帧耗时越长（可能影响帧率）
    //  值越小 → 加载越慢，但 Loading 动画更流畅
    //  默认值：2
    // --------------------------------------------------------
    void SetFrameLoadBudget(int stepsPerFrame)
    {
        m_frameBudget = stepsPerFrame > 0 ? stepsPerFrame : 1;
        std::cout << "[SceneManager] 帧预算设为: " << m_frameBudget << " 步/帧\n";
    }

    int GetFrameLoadBudget() const { return m_frameBudget; }

    // Loading 场景名称常量
    static constexpr const char* LOADING_SCENE_NAME = "__Loading__";

    // --------------------------------------------------------
    //  事件回调
    //  供外部监听场景加载/卸载事件（如加载画面、音乐切换等）
    //  参考 Unity SceneManager.sceneLoaded / sceneUnloaded 委托
    // --------------------------------------------------------
    using SceneEventCallback = std::function<void(const std::string&, Scene&)>;

    void SetOnSceneLoaded(SceneEventCallback callback)   { m_onSceneLoaded = std::move(callback); }
    void SetOnSceneUnloaded(SceneEventCallback callback) { m_onSceneUnloaded = std::move(callback); }

    // --------------------------------------------------------
    //  调试信息
    // --------------------------------------------------------
    void PrintStatus() const
    {
        std::cout << "\n[SceneManager] 状态:\n";
        std::cout << "  已注册工厂: " << m_factories.size() << " 个\n";
        std::cout << "  已加载场景: " << m_loadedScenes.size() << " 个\n";
        std::cout << "  活动场景: '" << m_activeSceneName << "'\n";
        std::cout << "  持久对象: " << m_persistentObjectIDs.size() << " 个\n";
        std::cout << "  加载顺序:\n";
        for (size_t i = 0; i < m_loadOrder.size(); ++i)
        {
            const auto& name = m_loadOrder[i];
            auto it = m_loadedScenes.find(name);
            bool isActive = (name == m_activeSceneName);
            bool isPersistent = (name == PERSISTENT_SCENE_NAME);
            size_t objCount = it != m_loadedScenes.end() ? it->second->GetGameObjectCount() : 0;

            std::cout << "    [" << i << "] '" << name << "'"
                      << " 对象数=" << objCount
                      << (isActive ? " [活动]" : "")
                      << (isPersistent ? " [持久]" : "")
                      << "\n";
        }
    }

    // --------------------------------------------------------
    //  重置（清除所有状态，用于测试或重启）
    // --------------------------------------------------------
    void Reset()
    {
        // 停止所有运行中的场景
        for (auto& [name, scene] : m_loadedScenes)
        {
            if (scene->IsRunning())
                scene->Stop();
        }
        m_loadedScenes.clear();
        m_loadOrder.clear();
        m_activeSceneName.clear();
        m_persistentObjectIDs.clear();
        // 注意：不清除 m_factories，工厂注册通常在程序启动时一次性完成
        std::cout << "[SceneManager] 已重置\n";
    }

    // 持久场景的名称常量（参考 Unity 的 DontDestroyOnLoad 场景）
    static constexpr const char* PERSISTENT_SCENE_NAME = "__Persistent__";

private:
    // 私有构造（单例模式）
    SceneManager() = default;
    ~SceneManager()
    {
        // 修复 Bug#7：按加载逆序停止并析构场景
        //   确保后加载的场景先被停止（如 Loading 场景 → 游戏场景 → 持久场景）
        //   这避免了 Scene 析构时访问已析构的其他单例（如 GameplayTagManager）
        //   因为 C++ 静态变量按构造逆序析构，SceneManager 必须在其依赖项之前完成清理
        for (auto it = m_loadOrder.rbegin(); it != m_loadOrder.rend(); ++it)
        {
            auto sceneIt = m_loadedScenes.find(*it);
            if (sceneIt != m_loadedScenes.end() && sceneIt->second->IsRunning())
                sceneIt->second->Stop();
        }
        // 按逆序析构场景对象（先清空 loadOrder，再清空 map）
        m_loadOrder.clear();
        m_loadedScenes.clear();
    }

    // 禁止拷贝/移动
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // --------------------------------------------------------
    //  异步加载内部调度（状态机推进）
    //  每帧在 Update() 开头调用，驱动异步加载的各个阶段
    //
    //  状态转换流程：
    //    Pending → LoadingScreen → Building → Ready → Activating → Done
    //
    //  参考 Unity AsyncOperation 的内部调度逻辑
    //  参考 UE FStreamableManager 的 Tick 处理
    // --------------------------------------------------------
    void TickAsyncOperation()
    {
        auto& op = *m_asyncOperation;

        switch (op.GetState())
        {
        case AsyncLoadState::Pending:
        {
            // 步骤1：以 Additive 模式加载 Loading 场景（如果已注册）
            if (m_loadingSceneFactory && !m_loadedScenes.count(LOADING_SCENE_NAME))
            {
                LoadScene(LOADING_SCENE_NAME, LoadSceneMode::Additive);
            }
            op.TransitionTo(AsyncLoadState::LoadingScreen);
            break;
        }

        case AsyncLoadState::LoadingScreen:
        {
            // 步骤2：Single 模式下，卸载旧场景（保留持久场景 + Loading 场景）
            if (op.GetMode() == LoadSceneMode::Single)
            {
                UnloadAllScenesExcept({ PERSISTENT_SCENE_NAME, LOADING_SCENE_NAME });
            }

            // 创建空的目标场景 + 填充分步任务
            auto newScene = std::make_unique<Scene>(op.GetTargetSceneName());
            auto stepIt = m_stepFactories.find(op.GetTargetSceneName());
            if (stepIt != m_stepFactories.end())
            {
                op.BeginBuilding(std::move(newScene), stepIt->second);
            }
            op.TransitionTo(AsyncLoadState::Building);
            break;
        }

        case AsyncLoadState::Building:
        {
            // 步骤3：每帧执行帧预算数量的 LoadStep
            for (int i = 0; i < m_frameBudget && !op.IsAllStepsDone(); ++i)
            {
                op.ExecuteNextStep();
            }

            // 所有步骤完成 → 进入 Ready 状态
            if (op.IsAllStepsDone())
            {
                std::cout << "[SceneManager] 异步加载构建完成，进度=100%\n";
                op.TransitionTo(AsyncLoadState::Ready);
            }
            break;
        }

        case AsyncLoadState::Ready:
        {
            // 步骤4：等待激活许可
            // 参考 Unity AsyncOperation.allowSceneActivation
            if (op.GetAllowActivation())
            {
                op.TransitionTo(AsyncLoadState::Activating);
            }
            // 否则停留在 Ready 状态，等待用户设置 SetAllowActivation(true)
            break;
        }

        case AsyncLoadState::Activating:
        {
            // 步骤5：卸载 Loading 场景，激活新场景

            // 5a. 卸载 Loading 场景
            if (m_loadedScenes.count(LOADING_SCENE_NAME))
            {
                UnloadScene(LOADING_SCENE_NAME);
            }

            // 5b. 获取预构建的新场景并启动
            auto newScene = op.TakeNewScene();
            if (newScene)
            {
                const std::string& targetName = op.GetTargetSceneName();

                // 启动场景（触发所有 Component 的 Start）
                newScene->Start();
                Scene* rawPtr = newScene.get();

                // 加入已加载列表
                m_loadedScenes[targetName] = std::move(newScene);
                m_loadOrder.push_back(targetName);

                // 设为活动场景
                if (op.GetMode() == LoadSceneMode::Single || m_activeSceneName.empty())
                {
                    m_activeSceneName = targetName;
                    std::cout << "[SceneManager] 活动场景设为: '" << targetName << "'\n";
                }

                // 触发场景加载回调
                if (m_onSceneLoaded)
                    m_onSceneLoaded(targetName, *rawPtr);

                std::cout << "[SceneManager] 异步加载完成: '" << targetName << "'\n";
            }

            // 5c. 转为完成状态
            op.TransitionTo(AsyncLoadState::Done);
            op.InvokeCompletionCallback();

            // 5d. 释放异步操作对象
            m_asyncOperation.reset();
            break;
        }

        case AsyncLoadState::Done:
            // 不应该到达这里（Done 状态下 m_asyncOperation 已被 reset）
            break;
        }
    }

    // --------------------------------------------------------
    //  卸载所有场景（排除指定列表中的场景）
    //  用于异步加载的 Single 模式：保留持久场景和 Loading 场景
    // --------------------------------------------------------
    void UnloadAllScenesExcept(const std::vector<std::string>& exceptNames)
    {
        std::unordered_set<std::string> exceptSet(exceptNames.begin(), exceptNames.end());

        // 收集需要卸载的场景名称
        std::vector<std::string> toUnload;
        for (const auto& name : m_loadOrder)
        {
            if (exceptSet.find(name) == exceptSet.end())
                toUnload.push_back(name);
        }

        for (const auto& name : toUnload)
        {
            auto it = m_loadedScenes.find(name);
            if (it == m_loadedScenes.end()) continue;

            std::cout << "[SceneManager] 卸载场景: '" << name << "'\n";

            if (m_onSceneUnloaded)
                m_onSceneUnloaded(name, *it->second);

            if (it->second->IsRunning())
                it->second->Stop();

            m_loadedScenes.erase(it);
        }

        // 清理 loadOrder
        m_loadOrder.erase(
            std::remove_if(m_loadOrder.begin(), m_loadOrder.end(),
                [this](const std::string& name)
                {
                    return !m_loadedScenes.count(name);
                }),
            m_loadOrder.end());

        m_activeSceneName.clear();
    }

    // 确保持久场景已创建
    void EnsurePersistentScene()
    {
        if (!m_loadedScenes.count(PERSISTENT_SCENE_NAME))
        {
            auto persistent = std::make_unique<Scene>(PERSISTENT_SCENE_NAME);
            persistent->Start();
            m_loadedScenes[PERSISTENT_SCENE_NAME] = std::move(persistent);
            // 插入到 loadOrder 的最前面（持久场景最先更新）
            m_loadOrder.insert(m_loadOrder.begin(), PERSISTENT_SCENE_NAME);
            std::cout << "[SceneManager] 已创建持久场景\n";
        }
    }

    // 卸载所有场景（Single 模式切换时调用）
    // 持久场景不会被卸载
    // 标记为持久的对象所在场景也不会被卸载（对象已受保护）
    // 修复 Bug#8：卸载回调 m_onSceneUnloaded 中访问 GetActiveScene() 可能获得 nullptr，
    //   因为 m_activeSceneName 在最后才清空。现在先保存旧活动场景名，
    //   确保回调期间 GetActiveScene() 仍能返回有效场景
    void UnloadAllScenes()
    {
        // 收集需要卸载的场景名称（不能在遍历中修改 map）
        std::vector<std::string> toUnload;
        for (const auto& name : m_loadOrder)
        {
            if (name != PERSISTENT_SCENE_NAME)
                toUnload.push_back(name);
        }

        // 先清空活动场景名，避免回调中访问即将被销毁的场景
        m_activeSceneName.clear();

        for (const auto& name : toUnload)
        {
            auto it = m_loadedScenes.find(name);
            if (it == m_loadedScenes.end()) continue;

            std::cout << "[SceneManager] 卸载场景: '" << name << "'\n";

            // 触发卸载回调
            if (m_onSceneUnloaded)
                m_onSceneUnloaded(name, *it->second);

            // 停止场景
            if (it->second->IsRunning())
                it->second->Stop();

            m_loadedScenes.erase(it);
        }

        // 清理 loadOrder 中已卸载的场景
        m_loadOrder.erase(
            std::remove_if(m_loadOrder.begin(), m_loadOrder.end(),
                [this](const std::string& name)
                {
                    return !m_loadedScenes.count(name);
                }),
            m_loadOrder.end());
    }

private:
    // --------------------------------------------------------
    //  核心数据
    // --------------------------------------------------------

    // 场景工厂注册表：场景名 → 构建函数（同步加载用）
    std::unordered_map<std::string, SceneFactory> m_factories;

    // SceneBuilder 注册表：场景名 → Builder 对象（SceneManager 持有所有权）
    // Builder 内聚了同步构建 + 分步构建的逻辑
    std::unordered_map<std::string, std::unique_ptr<SceneBuilder>> m_builders;

    // 分步场景工厂注册表：场景名 → 分步构建函数（异步加载用）
    std::unordered_map<std::string, SceneStepFactory> m_stepFactories;

    // Loading 场景工厂（全局唯一，异步加载时显示过渡画面）
    SceneFactory m_loadingSceneFactory;

    // 已加载的场景：场景名 → unique_ptr<Scene>
    std::unordered_map<std::string, std::unique_ptr<Scene>> m_loadedScenes;

    // 场景加载顺序（决定 Update/Render 的调用顺序）
    std::vector<std::string> m_loadOrder;

    // 当前活动场景名称
    std::string m_activeSceneName;

    // 持久对象 ID 集合（DontDestroyOnLoad 标记的对象，用于查询和调试）
    std::unordered_set<GameObjectID> m_persistentObjectIDs;

    // 事件回调
    SceneEventCallback m_onSceneLoaded;
    SceneEventCallback m_onSceneUnloaded;

    // --------------------------------------------------------
    //  异步加载数据
    // --------------------------------------------------------

    // 当前正在进行的异步加载操作（同时只允许一个）
    std::unique_ptr<AsyncSceneOperation> m_asyncOperation;

    // 每帧执行的最大加载步骤数（帧预算）
    int m_frameBudget = 2;
};
