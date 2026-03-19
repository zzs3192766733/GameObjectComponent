
#pragma once
#include "SceneBuilder.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include "PhysicsComponent.h"
#include "ColliderComponent.h"
#include "ScriptComponent.h"
#include "CollisionSystem.h"
#include "GameTags.h"
#include <iostream>

// ============================================================
//  GameLevelBuilder（游戏关卡场景构建器）
//
//  构建游戏关卡：玩家、敌人、地面、碰撞体、触发器等
//  同时支持分步构建（OnBuildSteps），用于异步加载时分帧处理
//
//  参考 UE AGameModeBase 子类化：
//    每种关卡类型（竞技场、副本、开放世界）对应不同 GameMode
//    GameMode 决定了关卡中的 Actor 组合和游戏规则
// ============================================================
class GameLevelBuilder : public SceneBuilder
{
public:
    std::string GetSceneName() const override { return "GameLevel"; }

    // --------------------------------------------------------
    //  同步构建（LoadScene 同步加载时调用）
    // --------------------------------------------------------
    void OnBuild(Scene& scene) override
    {
        // 配置碰撞系统
        scene.GetCollisionSystem().SetBroadPhaseMode(BroadPhaseMode::BruteForce);

        // --- 玩家 ---
        GameObjectID playerID = scene.CreateGameObject("Player");
        {
            GameObject* player = scene.GetGameObject(playerID);
            auto* tf = player->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            auto* render = player->AddComponent<RenderComponent>();
            render->SetSortingLayer(1);
            render->SetVisible(true);
            auto* physics = player->AddComponent<PhysicsComponent>();
            physics->SetMass(70.0f);
            physics->SetGravityEnabled(true);
            auto* col = player->AddComponent<ColliderComponent>(ColliderComponent::Shape::Capsule);
            col->SetCapsuleRadius(0.3f);
            col->SetCapsuleHalfHeight(0.6f);
            col->SetLayer(ColliderComponent::LAYER_PLAYER);
            col->SetCollisionMask(ColliderComponent::LAYER_ENEMY | ColliderComponent::LAYER_TERRAIN);
            player->AddComponent<ScriptComponent>()->SetScriptName("PlayerController");
            player->AddTag(TAG_Character_Player);
            player->AddTag(TAG_Attribute_Damageable);
        }

        // --- 敌人 ---
        GameObjectID enemyID = scene.CreateGameObject("Enemy");
        {
            GameObject* enemy = scene.GetGameObject(enemyID);
            auto* tf = enemy->AddComponent<TransformComponent>();
            tf->SetPosition(5.0f, 0.0f);
            auto* render = enemy->AddComponent<RenderComponent>();
            render->SetSortingLayer(1);
            render->SetVisible(true);
            auto* physics = enemy->AddComponent<PhysicsComponent>();
            physics->SetMass(80.0f);
            physics->SetGravityEnabled(true);
            auto* col = enemy->AddComponent<ColliderComponent>(ColliderComponent::Shape::Box);
            col->SetBoxHalfExtents(0.4f, 0.6f);
            col->SetLayer(ColliderComponent::LAYER_ENEMY);
            col->SetCollisionMask(ColliderComponent::LAYER_PLAYER | ColliderComponent::LAYER_PROJECTILE);
            enemy->AddComponent<ScriptComponent>()->SetScriptName("EnemyAI");
            enemy->AddTag(TAG_Character_Enemy_Boss);
            enemy->AddTag(TAG_Attribute_Damageable);
        }

        // --- 地面 ---
        GameObjectID groundID = scene.CreateGameObject("Ground");
        {
            GameObject* ground = scene.GetGameObject(groundID);
            auto* tf = ground->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, -5.0f);
            tf->SetScale(20.0f, 1.0f);
            auto* render = ground->AddComponent<RenderComponent>();
            render->SetSortingLayer(0);
            render->SetVisible(true);
            auto* col = ground->AddComponent<ColliderComponent>(ColliderComponent::Shape::Box);
            col->SetBoxHalfExtents(10.0f, 0.5f);
            col->SetLayer(ColliderComponent::LAYER_TERRAIN);
            col->SetCollisionMask(ColliderComponent::LAYER_PLAYER | ColliderComponent::LAYER_ENEMY);
            ground->AddTag(TAG_Environment_Terrain);
            ground->AddTag(TAG_Attribute_Static);
        }

        // --- 圆形碰撞体 A（演示碰撞事件流）---
        GameObjectID circleAID = scene.CreateGameObject("CircleA");
        {
            GameObject* obj = scene.GetGameObject(circleAID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
            col->SetCircleRadius(1.0f);
            col->SetLayer(ColliderComponent::LAYER_PLAYER);
            col->SetCollisionMask(ColliderComponent::LAYER_ENEMY | ColliderComponent::LAYER_TERRAIN | ColliderComponent::LAYER_TRIGGER);
            col->SetOnCollisionEnter([](const CollisionInfo& info) {
                std::cout << "    [CircleA] 碰撞开始! 对方ID=" << info.otherID << "\n";
            });
            col->SetOnCollisionStay([](const CollisionInfo& info) {
                std::cout << "    [CircleA] 碰撞持续... 对方ID=" << info.otherID << "\n";
            });
            col->SetOnCollisionExit([](const CollisionInfo& info) {
                std::cout << "    [CircleA] 碰撞结束! 对方ID=" << info.otherID << "\n";
            });
        }

        // --- 矩形 B ---
        GameObjectID boxBID = scene.CreateGameObject("BoxB");
        {
            GameObject* obj = scene.GetGameObject(boxBID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(1.2f, 0.0f);
            auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Box);
            col->SetBoxHalfExtents(0.5f, 0.5f);
            col->SetLayer(ColliderComponent::LAYER_ENEMY);
            col->SetCollisionMask(ColliderComponent::LAYER_PLAYER);
            col->SetOnCollisionEnter([](const CollisionInfo& info) {
                std::cout << "    [BoxB] 碰撞开始! 对方ID=" << info.otherID << "\n";
            });
        }

        // --- 触发器 C ---
        GameObjectID triggerCID = scene.CreateGameObject("TriggerC");
        {
            GameObject* obj = scene.GetGameObject(triggerCID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(0.5f, 0.0f);
            auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
            col->SetCircleRadius(0.8f);
            col->SetTrigger(true);
            col->SetLayer(ColliderComponent::LAYER_TRIGGER);
            col->SetCollisionMask(ColliderComponent::LAYER_PLAYER);
            col->SetOnTriggerEnter([](const CollisionInfo& info) {
                std::cout << "    [TriggerC] 触发器进入! 对方ID=" << info.otherID << "\n";
            });
            col->SetOnTriggerExit([](const CollisionInfo& info) {
                std::cout << "    [TriggerC] 触发器离开! 对方ID=" << info.otherID << "\n";
            });
        }

        // 设置父子关系
        scene.SetParent(enemyID, playerID);

        std::cout << "  [GameLevel] 已构建 " << scene.GetGameObjectCount() << " 个对象\n";
    }

    // --------------------------------------------------------
    //  分步构建（LoadSceneAsync 异步加载时调用）
    //  将构建过程拆分为 6 个 LoadStep，每帧执行帧预算数量的步骤
    //  参考 Unity 协程的 yield return 分帧逻辑
    // --------------------------------------------------------
    void OnBuildSteps(std::vector<LoadStep>& steps, Scene& scene) override
    {
        // 步骤1：配置碰撞系统
        steps.push_back([&scene]() {
            std::cout << "    [异步] 步骤1: 配置碰撞系统\n";
            scene.GetCollisionSystem().SetBroadPhaseMode(BroadPhaseMode::BruteForce);
        });

        // 步骤2：创建玩家
        steps.push_back([&scene]() {
            std::cout << "    [异步] 步骤2: 创建玩家\n";
            GameObjectID playerID = scene.CreateGameObject("Player");
            auto* player = scene.GetGameObject(playerID);
            auto* tf = player->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            auto* render = player->AddComponent<RenderComponent>();
            render->SetSortingLayer(1);
            render->SetVisible(true);
            auto* physics = player->AddComponent<PhysicsComponent>();
            physics->SetMass(70.0f);
            physics->SetGravityEnabled(true);
            auto* col = player->AddComponent<ColliderComponent>(ColliderComponent::Shape::Capsule);
            col->SetCapsuleRadius(0.3f);
            col->SetCapsuleHalfHeight(0.6f);
            col->SetLayer(ColliderComponent::LAYER_PLAYER);
            col->SetCollisionMask(ColliderComponent::LAYER_ENEMY | ColliderComponent::LAYER_TERRAIN);
            player->AddComponent<ScriptComponent>()->SetScriptName("PlayerController");
            player->AddTag(TAG_Character_Player);
            player->AddTag(TAG_Attribute_Damageable);
        });

        // 步骤3：创建敌人
        steps.push_back([&scene]() {
            std::cout << "    [异步] 步骤3: 创建敌人\n";
            GameObjectID enemyID = scene.CreateGameObject("Enemy");
            auto* enemy = scene.GetGameObject(enemyID);
            auto* tf = enemy->AddComponent<TransformComponent>();
            tf->SetPosition(5.0f, 0.0f);
            auto* render = enemy->AddComponent<RenderComponent>();
            render->SetSortingLayer(1);
            render->SetVisible(true);
            auto* physics = enemy->AddComponent<PhysicsComponent>();
            physics->SetMass(80.0f);
            physics->SetGravityEnabled(true);
            auto* col = enemy->AddComponent<ColliderComponent>(ColliderComponent::Shape::Box);
            col->SetBoxHalfExtents(0.4f, 0.6f);
            col->SetLayer(ColliderComponent::LAYER_ENEMY);
            col->SetCollisionMask(ColliderComponent::LAYER_PLAYER | ColliderComponent::LAYER_PROJECTILE);
            enemy->AddComponent<ScriptComponent>()->SetScriptName("EnemyAI");
            enemy->AddTag(TAG_Character_Enemy_Boss);
            enemy->AddTag(TAG_Attribute_Damageable);
        });

        // 步骤4：创建地面
        steps.push_back([&scene]() {
            std::cout << "    [异步] 步骤4: 创建地面\n";
            GameObjectID groundID = scene.CreateGameObject("Ground");
            auto* ground = scene.GetGameObject(groundID);
            auto* tf = ground->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, -5.0f);
            tf->SetScale(20.0f, 1.0f);
            auto* render = ground->AddComponent<RenderComponent>();
            render->SetSortingLayer(0);
            render->SetVisible(true);
            auto* col = ground->AddComponent<ColliderComponent>(ColliderComponent::Shape::Box);
            col->SetBoxHalfExtents(10.0f, 0.5f);
            col->SetLayer(ColliderComponent::LAYER_TERRAIN);
            col->SetCollisionMask(ColliderComponent::LAYER_PLAYER | ColliderComponent::LAYER_ENEMY);
            ground->AddTag(TAG_Environment_Terrain);
            ground->AddTag(TAG_Attribute_Static);
        });

        // 步骤5：创建碰撞演示对象 CircleA
        steps.push_back([&scene]() {
            std::cout << "    [异步] 步骤5: 创建碰撞体 CircleA\n";
            GameObjectID circleAID = scene.CreateGameObject("CircleA");
            auto* obj = scene.GetGameObject(circleAID);
            auto* tf = obj->AddComponent<TransformComponent>();
            tf->SetPosition(0.0f, 0.0f);
            auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
            col->SetCircleRadius(1.0f);
            col->SetLayer(ColliderComponent::LAYER_PLAYER);
            col->SetCollisionMask(ColliderComponent::LAYER_ENEMY | ColliderComponent::LAYER_TERRAIN | ColliderComponent::LAYER_TRIGGER);
        });

        // 步骤6：创建 BoxB + TriggerC
        steps.push_back([&scene]() {
            std::cout << "    [异步] 步骤6: 创建 BoxB + TriggerC\n";
            GameObjectID boxBID = scene.CreateGameObject("BoxB");
            {
                auto* obj = scene.GetGameObject(boxBID);
                auto* tf = obj->AddComponent<TransformComponent>();
                tf->SetPosition(1.2f, 0.0f);
                auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Box);
                col->SetBoxHalfExtents(0.5f, 0.5f);
                col->SetLayer(ColliderComponent::LAYER_ENEMY);
                col->SetCollisionMask(ColliderComponent::LAYER_PLAYER);
            }
            GameObjectID triggerCID = scene.CreateGameObject("TriggerC");
            {
                auto* obj = scene.GetGameObject(triggerCID);
                auto* tf = obj->AddComponent<TransformComponent>();
                tf->SetPosition(0.5f, 0.0f);
                auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
                col->SetCircleRadius(0.8f);
                col->SetTrigger(true);
                col->SetLayer(ColliderComponent::LAYER_TRIGGER);
                col->SetCollisionMask(ColliderComponent::LAYER_PLAYER);
            }
        });

        std::cout << "  [GameLevel 分步构建器] 已生成 " << steps.size() << " 个加载步骤\n";
    }
};
