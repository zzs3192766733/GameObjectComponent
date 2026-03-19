
#pragma once
#include "Scene.h"
#include "AsyncSceneOperation.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>

// ============================================================
//  SceneBuilder（场景构建器基类）
//
//  设计动机：
//    将散落在外部的场景工厂函数（BuildMainMenuScene、BuildGameLevelSteps 等）
//    内聚到各自的 Builder 子类中，实现"每个场景自己知道自己怎么构建"
//
//  设计参考：
//    - UE 的 AGameModeBase：定义关卡规则和内容的载体，每种关卡有不同子类
//    - Unity 的 ScriptableObject：可作为场景数据的容器，子类化定义不同场景
//    - Builder 模式（GoF）：将复杂对象的构建逻辑封装在独立的构建器中
//
//  职责分离：
//    Scene     = 纯容器（管理 GameObject 集合，驱动生命周期）
//    SceneBuilder = 构建器（知道如何填充场景内容）
//    SceneManager = 调度器（管理场景的加载/卸载/切换）
//
//  使用方式：
//    1. 继承 SceneBuilder，实现 GetSceneName() + OnBuild()
//    2. 可选覆盖 OnBuildSteps() 支持异步分步加载
//    3. 调用 SceneManager::RegisterSceneBuilder() 注册
//
//  示例：
//    class GameLevelBuilder : public SceneBuilder {
//        std::string GetSceneName() const override { return "GameLevel"; }
//        void OnBuild(Scene& scene) override { /* 创建关卡内容 */ }
//        void OnBuildSteps(std::vector<LoadStep>& steps, Scene& scene) override { ... }
//    };
//    sceneMgr.RegisterSceneBuilder(std::make_unique<GameLevelBuilder>());
// ============================================================
class SceneBuilder
{
public:
    virtual ~SceneBuilder() = default;

    // --------------------------------------------------------
    //  场景名称（用于在 SceneManager 中注册和查找）
    //  每个 Builder 子类必须返回唯一的场景名称
    //  参考 UE AGameModeBase 绑定到特定 Level 的方式
    // --------------------------------------------------------
    virtual std::string GetSceneName() const = 0;

    // --------------------------------------------------------
    //  同步构建（一口气构建全部内容）
    //  调用时机：SceneManager::LoadScene() 同步加载时
    //  子类在此方法中创建 GameObject、添加组件、配置碰撞等
    //
    //  参数 scene 是由 SceneManager 创建的空场景，
    //  Builder 负责往里面填充内容
    // --------------------------------------------------------
    virtual void OnBuild(Scene& scene) = 0;

    // --------------------------------------------------------
    //  分步构建（异步加载用）
    //  调用时机：SceneManager::LoadSceneAsync() 异步加载时
    //  子类可覆盖此方法，将构建过程拆分为多个 LoadStep
    //
    //  默认实现：把 OnBuild 包装成一个步骤（不分帧，一步完成）
    //  如需分帧加载，子类应覆盖此方法，添加多个细粒度步骤
    //
    //  参考 Unity 协程中的 yield return 分帧逻辑
    //  参考 UE Streaming Level 的分步加载机制
    // --------------------------------------------------------
    virtual void OnBuildSteps(std::vector<LoadStep>& steps, Scene& scene)
    {
        // 默认行为：整个 OnBuild 作为单一步骤
        // 对于简单场景（如 Loading、UI），不需要分帧
        steps.push_back([this, &scene]() { OnBuild(scene); });
    }

    // --------------------------------------------------------
    //  是否为 Loading 场景构建器
    //  Loading 场景在 SceneManager 中有特殊处理逻辑
    //  默认为 false，LoadingBuilder 子类覆盖返回 true
    // --------------------------------------------------------
    virtual bool IsLoadingScene() const { return false; }
};
