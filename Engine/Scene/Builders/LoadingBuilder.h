
#pragma once
#include "SceneBuilder.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include <iostream>

// ============================================================
//  LoadingBuilder（Loading 过渡场景构建器）
//
//  构建异步加载时的过渡画面：进度条背景、进度条、提示文字
//  在异步加载期间以 Additive 模式叠加显示
//
//  Loading 场景内的组件可通过以下方式实时获取加载进度：
//    float progress = SceneManager::Get().GetAsyncLoadProgress();
//
//  参考 Unity 的 Loading Screen 实现方式：
//    通常使用 Additive 场景 + AsyncOperation.progress 驱动 UI
//  参考 UE 的 ILoadingScreenModule：
//    通过模块化的 Loading Screen Widget 实现过渡画面
// ============================================================
class LoadingBuilder : public SceneBuilder
{
public:
    std::string GetSceneName() const override { return "__Loading__"; }

    // 标记为 Loading 场景（SceneManager 会特殊处理）
    bool IsLoadingScene() const override { return true; }

    void OnBuild(Scene& scene) override
    {
        // 背景
        GameObjectID bgID = scene.CreateGameObject("LoadingBG");
        {
            auto* bg = scene.GetGameObject(bgID);
            auto* tf = bg->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            tf->SetScale(16.0f, 9.0f);
            auto* render = bg->AddComponent<RenderComponent>();
            render->SetSortingLayer(100); // 确保在最上层
            render->SetVisible(true);
        }

        // 进度条
        GameObjectID barID = scene.CreateGameObject("ProgressBar");
        {
            auto* bar = scene.GetGameObject(barID);
            auto* tf = bar->AddComponent<TransformComponent>();
            tf->SetPosition(-5.0f, -3.0f);
            tf->SetScale(0.0f, 0.3f); // 初始宽度=0，随进度增长
            auto* render = bar->AddComponent<RenderComponent>();
            render->SetSortingLayer(101);
            render->SetVisible(true);
        }

        // "Loading..." 提示文字
        GameObjectID textID = scene.CreateGameObject("LoadingText");
        {
            auto* text = scene.GetGameObject(textID);
            auto* tf = text->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            auto* render = text->AddComponent<RenderComponent>();
            render->SetSortingLayer(101);
            render->SetVisible(true);
        }

        std::cout << "  [Loading] 已构建 " << scene.GetGameObjectCount()
                  << " 个对象（进度条 + 背景 + 文字）\n";
    }

    // Loading 场景内容少，使用默认的 OnBuildSteps（一步完成）即可
};
