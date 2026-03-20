
#pragma once
#include "SceneBuilder.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include "ScriptComponent.h"
#include "GameTags.h"
#include <iostream>

// ============================================================
//  MainMenuBuilder（主菜单场景构建器）
//
//  构建主菜单场景：背景、开始按钮、标题文字
//  同步加载即可（内容少，无需分步）
//
//  参考 UE 中 AGameModeBase 的子类化方式：
//  不同关卡使用不同的 GameMode 子类来定义规则和内容
// ============================================================
class MainMenuBuilder : public SceneBuilder
{
public:
    std::string GetSceneName() const override { return "MainMenu"; }

    void OnBuild(Scene& scene) override
    {
        // 创建一个模块对象，用来附着所有模块组件
        GameObjectID moduleID = scene.CreateGameObject("Module");
        {
            //TODO:添加组件
        }

        // 背景
        GameObjectID bgID = scene.CreateGameObject("MenuBackground");
        {
            GameObject* bg = scene.GetGameObject(bgID);
            auto* tf = bg->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            tf->SetScale(16.0f, 9.0f);
            auto* render = bg->AddComponent<RenderComponent>();
            render->SetSortingLayer(0);
            render->SetVisible(true);
            bg->AddTag(TAG_Attribute_Static);
        }

        // 开始按钮
        GameObjectID btnID = scene.CreateGameObject("StartButton");
        {
            GameObject* btn = scene.GetGameObject(btnID);
            auto* tf = btn->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, -1.0f);
            auto* render = btn->AddComponent<RenderComponent>();
            render->SetSortingLayer(1);
            render->SetVisible(true);
            auto* script = btn->AddComponent<ScriptComponent>();
            script->SetScriptName("StartButtonScript");
            btn->AddTag(TAG_UI_HUD);
        }

        // 标题文字
        GameObjectID titleID = scene.CreateGameObject("TitleText");
        {
            GameObject* title = scene.GetGameObject(titleID);
            auto* tf = title->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 2.0f);
            auto* render = title->AddComponent<RenderComponent>();
            render->SetSortingLayer(2);
            render->SetVisible(true);
            title->AddTag(TAG_UI_HUD);
        }

        std::cout << "  [MainMenu] 已构建 " << scene.GetGameObjectCount() << " 个对象\n";
    }

    // 主菜单内容少，使用默认的 OnBuildSteps（一步完成）即可
};
