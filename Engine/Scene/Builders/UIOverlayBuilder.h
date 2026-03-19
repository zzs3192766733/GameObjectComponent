
#pragma once
#include "SceneBuilder.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include "GameTags.h"
#include <iostream>

// ============================================================
//  UIOverlayBuilder（UI 叠加层场景构建器）
//
//  构建 HUD 元素：血条、分数、小地图
//  通常以 Additive 模式叠加到游戏关卡之上
//
//  参考 UE 的 UMG Widget Blueprint：
//    HUD 元素在 UE 中通过 Widget 系统实现，
//    在本引擎中简化为 Additive 场景中的 GameObject
// ============================================================
class UIOverlayBuilder : public SceneBuilder
{
public:
    std::string GetSceneName() const override { return "UIOverlay"; }

    void OnBuild(Scene& scene) override
    {
        // 血条
        GameObjectID healthBarID = scene.CreateGameObject("HealthBar");
        {
            GameObject* obj = scene.GetGameObject(healthBarID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(-6.0f, 4.0f);
            auto* render = obj->AddComponent<RenderComponent>();
            render->SetSortingLayer(10);
            render->SetVisible(true);
            obj->AddTag(TAG_UI_HUD);
        }

        // 分数显示
        GameObjectID scoreTextID = scene.CreateGameObject("ScoreText");
        {
            GameObject* obj = scene.GetGameObject(scoreTextID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(6.0f, 4.0f);
            auto* render = obj->AddComponent<RenderComponent>();
            render->SetSortingLayer(10);
            render->SetVisible(true);
            obj->AddTag(TAG_UI_HUD);
        }

        // 小地图
        GameObjectID minimapID = scene.CreateGameObject("Minimap");
        {
            GameObject* obj = scene.GetGameObject(minimapID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(6.0f, -3.0f);
            auto* render = obj->AddComponent<RenderComponent>();
            render->SetSortingLayer(10);
            render->SetVisible(true);
            obj->AddTag(TAG_UI_HUD);
        }

        std::cout << "  [UIOverlay] 已构建 " << scene.GetGameObjectCount() << " 个对象\n";
    }

    // UI 内容少，使用默认的 OnBuildSteps（一步完成）即可
};
