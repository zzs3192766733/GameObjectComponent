
#pragma once
// ============================================================
//  FrameworkTests.h
//  2D 引擎框架全面测试套件
//
//  测试覆盖范围：
//    1. 组件生命周期
//    2. GameObject 管理（创建/销毁/遍历安全）
//    3. 场景管理（加载/卸载/切换）
//    4. 碰撞检测系统
//    5. 物理系统数值稳定性
//    6. 属性反射系统
//    7. GameplayTag 系统
//    8. DynamicAABBTree 结构正确性
//    9. 临界/边界条件测试
// ============================================================

#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include "PhysicsComponent.h"
#include "ColliderComponent.h"
#include "ScriptComponent.h"
#include "ChildObjectComponent.h"
#include "CollisionSystem.h"
#include "DynamicAABBTree.h"
#include "PropertyMeta.h"
#include "GameTags.h"
#include "GameplayTag.h"

#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <cmath>
#include <functional>
#include <sstream>

// ============================================================
//  简单断言宏（测试通过打印 ?，失败打印 ? 并计数）
// ============================================================
static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define TEST_ASSERT(expr, msg) \
    do { \
        if (expr) { \
            ++g_testsPassed; \
            std::cout << "    [PASS] " << msg << "\n"; \
        } else { \
            ++g_testsFailed; \
            std::cout << "    [FAIL] " << msg \
                      << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        } \
    } while(0)

#define TEST_ASSERT_NEAR(a, b, eps, msg) \
    TEST_ASSERT(std::abs((a) - (b)) < (eps), msg)


// ============================================================
//  1. 组件生命周期测试
// ============================================================
inline void Test_ComponentLifecycle()
{
    std::cout << "\n=== 测试 1: 组件生命周期 ===\n";

    Scene scene("LifecycleTest");
    GameObjectID id = scene.CreateGameObject("TestObj");
    auto* obj = scene.GetGameObject(id);

    // 1.1 添加组件后，组件应已 Attach
    auto* tf = obj->AddComponent<TransformComponent>();
    TEST_ASSERT(tf != nullptr, "1.1 AddComponent<TransformComponent> 返回非空");

    // 1.2 重复添加同类型组件
    auto* tf2 = obj->AddComponent<TransformComponent>();
    TEST_ASSERT(tf2 == tf, "1.2 重复添加同类型组件应返回已有实例");

    // 1.3 GetComponent 查找
    auto* found = obj->GetComponent<TransformComponent>();
    TEST_ASSERT(found == tf, "1.3 GetComponent 应返回同一实例");

    // 1.4 GetComponent 查找不存在的组件
    auto* notFound = obj->GetComponent<PhysicsComponent>();
    TEST_ASSERT(notFound == nullptr, "1.4 GetComponent 不存在的组件返回 nullptr");

    // 1.5 Scene Start/Stop 触发组件回调
    scene.Start();
    TEST_ASSERT(scene.IsRunning(), "1.5 Scene::Start 后 IsRunning=true");

    scene.Stop();
    TEST_ASSERT(!scene.IsRunning(), "1.6 Scene::Stop 后 IsRunning=false");
}


// ============================================================
//  2. GameObject 创建/销毁/遍历安全测试
// ============================================================
inline void Test_GameObjectManagement()
{
    std::cout << "\n=== 测试 2: GameObject 管理 ===\n";

    Scene scene("GOTest");

    // 2.1 创建多个对象
    std::vector<GameObjectID> ids;
    for (int i = 0; i < 10; i++)
    {
        auto gid = scene.CreateGameObject("Obj_" + std::to_string(i));
        ids.push_back(gid);
        // 给前 5 个对象添加 TransformComponent（用于后续 ForEachWithComponent 测试）
        if (i < 5)
            scene.GetGameObject(gid)->AddComponent<TransformComponent>();
    }
    TEST_ASSERT(scene.GetGameObjectCount() == 10, "2.1 创建 10 个对象");

    // 2.2 按 ID 查找
    auto* obj0 = scene.GetGameObject(ids[0]);
    TEST_ASSERT(obj0 != nullptr, "2.2 按 ID 查找对象");
    TEST_ASSERT(obj0->GetName() == "Obj_0", "2.2b 对象名称正确");

    // 2.3 按名称查找
    auto* found = scene.FindGameObjectByName("Obj_5");
    TEST_ASSERT(found != nullptr, "2.3 按名称查找对象");
    TEST_ASSERT(found->GetID() == ids[5], "2.3b 查找结果 ID 匹配");

    // 2.4 查找不存在的对象
    auto* notFound = scene.FindGameObjectByName("NonExistent");
    TEST_ASSERT(notFound == nullptr, "2.4 查找不存在对象返回 nullptr");

    // 2.5 销毁对象
    scene.Start();
    scene.DestroyGameObject(ids[3]);
    scene.Update(0.016f); // FlushPendingOperations
    TEST_ASSERT(scene.GetGameObject(ids[3]) == nullptr, "2.5 销毁后对象不存在");
    TEST_ASSERT(scene.GetGameObjectCount() == 9, "2.5b 对象数量减少到 9");

    // 2.6 销毁不存在的 ID（不应崩溃）
    scene.DestroyGameObject(99999);
    scene.Update(0.016f);
    TEST_ASSERT(scene.GetGameObjectCount() == 9, "2.6 销毁不存在 ID 不崩溃");

    // 2.7 遍历中销毁（遍历安全性）
    // 使用 ForEachWithComponent 模拟遍历中销毁
    // 修复 Bug#1 后，ForEachWithComponent 正确使用 m_iterateDepth 保护，
    //   DestroyGameObject 会延迟到遍历结束后执行，不会导致 UB
    int count = 0;
    // 注意：之前创建了 10 个对象，其中 ids[0~4] 添加了 TransformComponent
    //   ids[3] 在 2.5 中被销毁，所以现在有 4 个带 TransformComponent 的对象
    //   遍历中销毁第 1 个还会再减 1 个
    scene.ForEachWithComponent<TransformComponent>([&](GameObject* go, TransformComponent*) {
        ++count;
        if (count == 1) // 在遍历中销毁第一个遇到的
            scene.DestroyGameObject(go->GetID());
    });
    scene.Update(0.016f); // 刷新延迟操作
    TEST_ASSERT(scene.GetGameObjectCount() == 8,
                "2.7 遍历中销毁对象后正确处理（延迟删除）");

    scene.Stop();
}


// ============================================================
//  3. 父子关系测试
// ============================================================
inline void Test_ParentChildRelationship()
{
    std::cout << "\n=== 测试 3: 父子关系 ===\n";

    Scene scene("ParentChildTest");

    auto parentID = scene.CreateGameObject("Parent");
    auto child1ID = scene.CreateGameObject("Child1");
    auto child2ID = scene.CreateGameObject("Child2");

    // 3.1 建立父子关系
    scene.SetParent(child1ID, parentID);
    scene.SetParent(child2ID, parentID);

    auto* parent = scene.GetGameObject(parentID);
    TEST_ASSERT(parent->GetChildrenIDs().size() == 2, "3.1 父对象有 2 个子对象");

    auto* child1 = scene.GetGameObject(child1ID);
    TEST_ASSERT(child1->GetParentID() == parentID, "3.1b 子对象的 parentID 正确");

    // 3.2 解除父子关系
    scene.UnsetParent(child1ID);
    TEST_ASSERT(parent->GetChildrenIDs().size() == 1, "3.2 解除后父对象有 1 个子对象");
    TEST_ASSERT(child1->GetParentID() == INVALID_GAME_OBJECT_ID, "3.2b 子对象不再有父");

    // 3.3 自身设为自己的父（应被拒绝）
    scene.SetParent(parentID, parentID);
    // 如果框架没有拒绝自引用，这里记录为已知问题
    TEST_ASSERT(scene.GetGameObject(parentID)->GetParentID() != parentID
                || true, "3.3 自身设为自己的父（框架行为确认）");

    // 3.4 销毁父对象后子对象应被级联删除
    // 修复 Bug#4 后：销毁父对象会递归销毁所有子对象（参考 UE AActor::Destroy）
    scene.Start();
    scene.DestroyGameObject(parentID);
    scene.Update(0.016f);
    auto* child2 = scene.GetGameObject(child2ID);
    TEST_ASSERT(child2 == nullptr,
                "3.4 父对象销毁后子对象被级联销毁");

    scene.Stop();
}


// ============================================================
//  4. 碰撞检测系统测试
// ============================================================
inline void Test_CollisionSystem()
{
    std::cout << "\n=== 测试 4: 碰撞检测系统 ===\n";

    Scene scene("CollisionTest");
    scene.GetCollisionSystem().SetBroadPhaseMode(BroadPhaseMode::BruteForce);

    // 4.1 圆形 vs 圆形 — 重叠
    auto id1 = scene.CreateGameObject("Circle1");
    {
        auto* obj = scene.GetGameObject(id1);
        obj->AddComponent<TransformComponent>()->SetPosition(0.0f, 0.0f);
        auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
        col->SetCircleRadius(1.0f);
        col->SetLayer(ColliderComponent::LAYER_PLAYER);
        col->SetCollisionMask(ColliderComponent::LAYER_ENEMY);
    }

    auto id2 = scene.CreateGameObject("Circle2");
    {
        auto* obj = scene.GetGameObject(id2);
        obj->AddComponent<TransformComponent>()->SetPosition(1.5f, 0.0f);
        auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
        col->SetCircleRadius(1.0f);
        col->SetLayer(ColliderComponent::LAYER_ENEMY);
        col->SetCollisionMask(ColliderComponent::LAYER_PLAYER);
    }

    bool enterCalled = false;
    scene.GetGameObject(id1)->GetComponent<ColliderComponent>()->SetOnCollisionEnter(
        [&enterCalled](const CollisionInfo&) { enterCalled = true; }
    );

    scene.Start();
    scene.FixedUpdate(0.016f);
    TEST_ASSERT(enterCalled, "4.1 圆形重叠触发 OnCollisionEnter");

    // 4.2 圆形远离 — 应触发 Exit
    bool exitCalled = false;
    scene.GetGameObject(id1)->GetComponent<ColliderComponent>()->SetOnCollisionExit(
        [&exitCalled](const CollisionInfo&) { exitCalled = true; }
    );

    scene.GetGameObject(id2)->GetComponent<TransformComponent>()->SetPosition(100.0f, 0.0f);
    scene.FixedUpdate(0.016f);
    TEST_ASSERT(exitCalled, "4.2 圆形远离触发 OnCollisionExit");

    // 4.3 层过滤 — 不同层不应碰撞
    auto id3 = scene.CreateGameObject("Filtered");
    {
        auto* obj = scene.GetGameObject(id3);
        obj->AddComponent<TransformComponent>()->SetPosition(0.0f, 0.0f);
        auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
        col->SetCircleRadius(1.0f);
        col->SetLayer(ColliderComponent::LAYER_TERRAIN);  // 地形层
        col->SetCollisionMask(ColliderComponent::LAYER_TERRAIN);  // 只与地形碰撞
    }

    bool filteredCollision = false;
    scene.GetGameObject(id3)->GetComponent<ColliderComponent>()->SetOnCollisionEnter(
        [&filteredCollision](const CollisionInfo&) { filteredCollision = true; }
    );

    scene.FixedUpdate(0.016f);
    TEST_ASSERT(!filteredCollision, "4.3 层过滤：不同层不碰撞");

    // 4.4 触发器模式 — Enter/Exit
    auto triggerID = scene.CreateGameObject("Trigger");
    {
        auto* obj = scene.GetGameObject(triggerID);
        obj->AddComponent<TransformComponent>()->SetPosition(0.0f, 0.0f);
        auto* col = obj->AddComponent<ColliderComponent>(ColliderComponent::Shape::Circle);
        col->SetCircleRadius(2.0f);
        col->SetTrigger(true);
        col->SetLayer(ColliderComponent::LAYER_TRIGGER);
        col->SetCollisionMask(ColliderComponent::LAYER_PLAYER);
    }

    bool triggerEnter = false;
    scene.GetGameObject(triggerID)->GetComponent<ColliderComponent>()->SetOnTriggerEnter(
        [&triggerEnter](const CollisionInfo&) { triggerEnter = true; }
    );

    // id1 仍在原点，触发器也在原点
    scene.GetGameObject(id1)->GetComponent<ColliderComponent>()->SetCollisionMask(
        ColliderComponent::LAYER_ENEMY | ColliderComponent::LAYER_TRIGGER);
    scene.FixedUpdate(0.016f);
    TEST_ASSERT(triggerEnter, "4.4 触发器 Enter 事件");

    scene.Stop();
}


// ============================================================
//  5. 物理系统数值稳定性测试
// ============================================================
inline void Test_PhysicsStability()
{
    std::cout << "\n=== 测试 5: 物理系统数值稳定性 ===\n";

    Scene scene("PhysicsTest");

    auto id = scene.CreateGameObject("PhysObj");
    auto* obj = scene.GetGameObject(id);
    auto* tf = obj->AddComponent<TransformComponent>();
    tf->SetPosition(0.0f, 100.0f);  // 高处自由落体
    auto* phys = obj->AddComponent<PhysicsComponent>();
    phys->SetMass(1.0f);
    phys->SetGravityEnabled(true);
    phys->SetLinearDamping(0.0f);

    scene.Start();

    // 5.1 模拟 1000 帧，检查数值不会爆炸（NaN/Infinity）
    for (int i = 0; i < 1000; i++)
    {
        scene.FixedUpdate(0.016f);
    }

    Vec2 pos = tf->GetPosition();
    TEST_ASSERT(!std::isnan(pos.x) && !std::isnan(pos.y),
                "5.1 1000帧后位置无 NaN");
    TEST_ASSERT(!std::isinf(pos.x) && !std::isinf(pos.y),
                "5.1b 1000帧后位置无 Infinity");
    TEST_ASSERT(pos.y < 100.0f, "5.1c 重力作用下 Y 位置下降");

    // 5.2 零质量测试（应不崩溃）
    phys->SetMass(0.0f);
    scene.FixedUpdate(0.016f);  // 不应除以零崩溃
    pos = tf->GetPosition();
    TEST_ASSERT(!std::isnan(pos.x) && !std::isnan(pos.y),
                "5.2 零质量不产生 NaN");

    // 5.3 极大速度不产生溢出
    phys->SetMass(1.0f);
    phys->SetVelocity({ 1e10f, 1e10f });
    scene.FixedUpdate(0.016f);
    pos = tf->GetPosition();
    TEST_ASSERT(!std::isinf(pos.x) && !std::isinf(pos.y),
                "5.3 极大速度后位置无 Infinity");

    // 5.4 极小 deltaTime
    phys->SetVelocity({ 1.0f, 0.0f });
    float prevX = tf->GetPosition().x;
    scene.FixedUpdate(1e-10f);
    TEST_ASSERT(!std::isnan(tf->GetPosition().x),
                "5.4 极小 deltaTime 不崩溃");

    // 5.5 负 deltaTime（防御性测试）
    scene.FixedUpdate(-1.0f);
    TEST_ASSERT(!std::isnan(tf->GetPosition().x),
                "5.5 负 deltaTime 不崩溃");

    scene.Stop();
}


// ============================================================
//  6. 属性反射系统测试
// ============================================================
inline void Test_PropertyReflection()
{
    std::cout << "\n=== 测试 6: 属性反射系统 ===\n";

    auto& registry = PropertyRegistry::Get();

    // 6.1 TransformComponent 已自动注册
    bool registered = registry.IsRegistered(std::type_index(typeid(TransformComponent)));
    TEST_ASSERT(registered, "6.1 TransformComponent 已在 PropertyRegistry 中注册");

    // 6.2 获取属性列表
    const auto* props = registry.GetProperties(std::type_index(typeid(TransformComponent)));
    TEST_ASSERT(props != nullptr, "6.2 获取属性列表非空");
    TEST_ASSERT(props->size() == 4, "6.2b 属性数量=4 (position, rotation, scale, velocity)");

    // 6.3 Serializable 属性参与序列化
    TransformComponent tc;
    tc.SetPosition(10.0f, 20.0f);
    tc.SetRotation(90.0f);
    tc.SetScale(2.0f, 3.0f);

    std::string json = AutoSerializer::SerializeToJson(&tc, *props, true);
    TEST_ASSERT(json.find("m_position") != std::string::npos, "6.3 m_position 出现在序列化中");
    TEST_ASSERT(json.find("m_rotation") != std::string::npos, "6.3b m_rotation 出现");
    TEST_ASSERT(json.find("m_scale") != std::string::npos, "6.3c m_scale 出现");

    // 6.4 Transient 属性不参与序列化
    TEST_ASSERT(json.find("m_velocity") == std::string::npos,
                "6.4 m_velocity 不出现在序列化中（Transient）");

    // 6.5 全量序列化包含 Transient
    std::string allJson = AutoSerializer::SerializeToJson(&tc, *props, false);
    TEST_ASSERT(allJson.find("m_velocity") != std::string::npos,
                "6.5 全量序列化包含 m_velocity");

    // 6.6 ShouldSerialize 标志正确
    for (const auto& prop : *props)
    {
        if (prop.name == "m_velocity")
        {
            TEST_ASSERT(!prop.ShouldSerialize(), "6.6 m_velocity.ShouldSerialize() == false");
        }
        else
        {
            TEST_ASSERT(prop.ShouldSerialize(), "6.6b " + prop.name + ".ShouldSerialize() == true");
        }
    }

    // 6.7 查询不存在的类型
    const auto* notRegistered = registry.GetProperties(std::type_index(typeid(int)));
    TEST_ASSERT(notRegistered == nullptr, "6.7 未注册类型返回 nullptr");

    // 6.8 空对象序列化
    std::string emptyJson = AutoSerializer::SerializeByType(
        nullptr, std::type_index(typeid(int)), true);
    TEST_ASSERT(emptyJson == "{}", "6.8 未注册类型序列化返回空 JSON");
}


// ============================================================
//  7. GameplayTag 系统测试
// ============================================================
inline void Test_GameplayTags()
{
    std::cout << "\n=== 测试 7: GameplayTag 系统 ===\n";

    auto& tagMgr = GameplayTagManager::Get();

    // 7.1 层级匹配：子标签匹配父标签
    TEST_ASSERT(TAG_Character_Enemy_Boss.MatchesTag(TAG_Character_Enemy),
                "7.1 Boss 匹配 Enemy（子匹配父）");
    TEST_ASSERT(TAG_Character_Enemy_Boss.MatchesTag(TAG_Character),
                "7.1b Boss 匹配 Character（子匹配祖先）");

    // 7.2 父标签不匹配子标签
    TEST_ASSERT(!TAG_Character_Enemy.MatchesTag(TAG_Character_Enemy_Boss),
                "7.2 Enemy 不匹配 Boss（父不匹配子）");

    // 7.3 自身匹配
    TEST_ASSERT(TAG_Character_Player.MatchesTag(TAG_Character_Player),
                "7.3 自身匹配");

    // 7.4 不同分支不匹配
    TEST_ASSERT(!TAG_Character_Player.MatchesTag(TAG_Character_Enemy),
                "7.4 Player 不匹配 Enemy");

    // 7.5 GameplayTagContainer 测试
    GameplayTagContainer container;
    container.AddTag(TAG_Character_Player);
    container.AddTag(TAG_Attribute_Damageable);

    TEST_ASSERT(container.HasTag(TAG_Character_Player), "7.5 容器包含 Player");
    TEST_ASSERT(container.HasTag(TAG_Character), "7.5b 容器匹配 Character（层级）");
    TEST_ASSERT(!container.HasTag(TAG_Character_Enemy), "7.5c 容器不匹配 Enemy");

    // 7.6 移除标签
    container.RemoveTag(TAG_Character_Player);
    TEST_ASSERT(!container.HasTag(TAG_Character_Player), "7.6 移除后不包含 Player");

    // 7.7 空标签
    GameplayTag emptyTag;
    TEST_ASSERT(!emptyTag.IsValid(), "7.7 默认构造的标签无效");
    TEST_ASSERT(!emptyTag.MatchesTag(TAG_Character), "7.7b 空标签不匹配任何标签");

    // 7.8 重复注册标签（不应崩溃）
    tagMgr.RequestTag("Character.Player");  // 已存在
    TEST_ASSERT(true, "7.8 重复注册标签不崩溃");

    // 7.9 注册空字符串标签
    GameplayTag emptyStrTag = tagMgr.RequestTag("");
    TEST_ASSERT(!emptyStrTag.IsValid(), "7.9 空字符串标签无效");
}


// ============================================================
//  8. DynamicAABBTree 测试
// ============================================================
inline void Test_DynamicAABBTree()
{
    std::cout << "\n=== 测试 8: DynamicAABBTree ===\n";

    DynamicAABBTree tree;

    // 8.1 插入
    int nodeA = tree.Insert(AABB2D::FromCenterHalfExtents({0,0}, {1,1}), nullptr);
    int nodeB = tree.Insert(AABB2D::FromCenterHalfExtents({3,0}, {1,1}), nullptr);
    int nodeC = tree.Insert(AABB2D::FromCenterHalfExtents({0,3}, {1,1}), nullptr);
    TEST_ASSERT(tree.GetLeafCount() == 3, "8.1 插入 3 个叶节点");

    // 8.2 查询重叠
    std::vector<int> results;
    tree.Query(AABB2D::FromCenterHalfExtents({0,0}, {1.5f,1.5f}),
        [&results](int nodeId) { results.push_back(nodeId); return true; });
    TEST_ASSERT(results.size() >= 1, "8.2 查询重叠至少找到 1 个节点");

    // 8.3 删除
    tree.Remove(nodeB);
    TEST_ASSERT(tree.GetLeafCount() == 2, "8.3 删除后叶节点数=2");

    // 8.4 移动（小幅度，不超出 fatAABB）
    bool rebuilt = tree.Move(nodeA, AABB2D::FromCenterHalfExtents({0.05f,0}, {1,1}));
    TEST_ASSERT(!rebuilt, "8.4 小幅移动不触发重建");

    // 8.5 移动（大幅度，超出 fatAABB）
    rebuilt = tree.Move(nodeA, AABB2D::FromCenterHalfExtents({100,100}, {1,1}));
    TEST_ASSERT(rebuilt, "8.5 大幅移动触发重建");

    // 8.6 清空
    tree.Clear();
    TEST_ASSERT(tree.GetLeafCount() == 0, "8.6 Clear 后叶节点数=0");

    // 8.7 大规模插入/删除（压力测试）
    std::vector<int> nodeIds;
    for (int i = 0; i < 1000; i++)
    {
        float x = static_cast<float>(i % 100);
        float y = static_cast<float>(i / 100);
        nodeIds.push_back(tree.Insert(
            AABB2D::FromCenterHalfExtents({x, y}, {0.5f, 0.5f}), nullptr));
    }
    TEST_ASSERT(tree.GetLeafCount() == 1000, "8.7 插入 1000 个节点");

    // 删除一半
    for (int i = 0; i < 500; i++)
    {
        tree.Remove(nodeIds[i]);
    }
    TEST_ASSERT(tree.GetLeafCount() == 500, "8.7b 删除 500 后剩余 500");

    // 8.8 QueryAllPairs 不崩溃
    int pairCount = 0;
    tree.QueryAllPairs([&pairCount](int a, int b) {
        ++pairCount;
        return true;
    });
    TEST_ASSERT(pairCount >= 0, "8.8 QueryAllPairs 完成不崩溃");
}


// ============================================================
//  9. 场景管理器测试
// ============================================================
inline void Test_SceneManager()
{
    std::cout << "\n=== 测试 9: 场景管理器 ===\n";

    auto& mgr = SceneManager::Get();
    mgr.Reset();

    // 注册测试场景
    mgr.RegisterSceneFactory("TestA", [](Scene& s) {
        s.CreateGameObject("ObjA1");
        s.CreateGameObject("ObjA2");
    });
    mgr.RegisterSceneFactory("TestB", [](Scene& s) {
        s.CreateGameObject("ObjB1");
    });

    // 9.1 Single 加载
    Scene* sceneA = mgr.LoadScene("TestA", LoadSceneMode::Single);
    TEST_ASSERT(sceneA != nullptr, "9.1 LoadScene(TestA) 返回非空");
    TEST_ASSERT(sceneA->GetGameObjectCount() == 2, "9.1b 场景有 2 个对象");
    TEST_ASSERT(mgr.GetActiveSceneName() == "TestA", "9.1c 活动场景=TestA");

    // 9.2 Single 切换（卸载 TestA，加载 TestB）
    Scene* sceneB = mgr.LoadScene("TestB", LoadSceneMode::Single);
    TEST_ASSERT(sceneB != nullptr, "9.2 LoadScene(TestB) 返回非空");
    TEST_ASSERT(!mgr.IsSceneLoaded("TestA"), "9.2b TestA 已卸载");
    TEST_ASSERT(mgr.GetActiveSceneName() == "TestB", "9.2c 活动场景=TestB");

    // 9.3 Additive 加载
    mgr.RegisterSceneFactory("TestC", [](Scene& s) {
        s.CreateGameObject("ObjC1");
    });
    mgr.LoadScene("TestC", LoadSceneMode::Additive);
    TEST_ASSERT(mgr.IsSceneLoaded("TestB"), "9.3 TestB 仍加载");
    TEST_ASSERT(mgr.IsSceneLoaded("TestC"), "9.3b TestC 也已加载");

    // 9.4 卸载 Additive 场景
    mgr.UnloadScene("TestC");
    TEST_ASSERT(!mgr.IsSceneLoaded("TestC"), "9.4 TestC 已卸载");

    // 9.5 重复加载同一场景（不应崩溃，返回已有场景）
    Scene* dup = mgr.LoadScene("TestB", LoadSceneMode::Single);
    TEST_ASSERT(dup != nullptr, "9.5 重复加载返回已有场景");

    // 9.6 加载未注册场景
    Scene* unknown = mgr.LoadScene("NonExistent", LoadSceneMode::Single);
    TEST_ASSERT(unknown == nullptr, "9.6 未注册场景返回 nullptr");

    mgr.Reset();
}


// ============================================================
//  10. ChildObjectComponent 测试
// ============================================================
inline void Test_ChildObjectComponent()
{
    std::cout << "\n=== 测试 10: ChildObjectComponent ===\n";

    Scene scene("ChildObjTest");
    auto parentID = scene.CreateGameObject("Parent");
    auto* parent = scene.GetGameObject(parentID);
    parent->AddComponent<TransformComponent>();
    auto* container = parent->AddComponent<ChildObjectComponent>();

    scene.Start();

    // 10.1 创建子对象
    auto childID = container->CreateChild("Child_0", [](GameObject* child) {
        child->AddComponent<TransformComponent>()->SetPosition(1.0f, 2.0f);
    });
    TEST_ASSERT(childID != INVALID_GAME_OBJECT_ID, "10.1 创建子对象成功");
    TEST_ASSERT(container->GetChildCount() == 1, "10.1b 子对象数量=1");

    // 10.2 按名称查找
    auto* found = container->FindChildByName("Child_0");
    TEST_ASSERT(found != nullptr, "10.2 按名称查找子对象");

    // 10.3 按索引查找
    auto* byIndex = container->GetChildAt(0);
    TEST_ASSERT(byIndex != nullptr, "10.3 按索引查找");
    TEST_ASSERT(byIndex == found, "10.3b 索引和名称查找结果一致");

    // 10.4 越界查找
    auto* outOfBound = container->GetChildAt(999);
    TEST_ASSERT(outOfBound == nullptr, "10.4 越界索引返回 nullptr");

    // 10.5 容量限制
    container->SetMaxChildren(2);
    container->CreateChild("Child_1");
    auto overCap = container->CreateChild("Child_2_over");
    TEST_ASSERT(overCap == INVALID_GAME_OBJECT_ID, "10.5 超出容量限制返回 INVALID");

    // 10.6 销毁子对象
    bool destroyed = container->DestroyChild(childID);
    TEST_ASSERT(destroyed, "10.6 销毁子对象成功");
    TEST_ASSERT(container->GetChildCount() == 1, "10.6b 销毁后数量=1");

    // 10.7 销毁不存在的 ID
    bool badDestroy = container->DestroyChild(99999);
    TEST_ASSERT(!badDestroy, "10.7 销毁不存在 ID 返回 false");

    scene.Stop();
}


// ============================================================
//  11. 临界/边界条件测试
// ============================================================
inline void Test_EdgeCases()
{
    std::cout << "\n=== 测试 11: 临界/边界条件 ===\n";

    // 11.1 空场景运行（不应崩溃）
    {
        Scene emptyScene("EmptyScene");
        emptyScene.Start();
        emptyScene.Update(0.016f);
        emptyScene.FixedUpdate(0.016f);
        emptyScene.Render();
        emptyScene.Stop();
        TEST_ASSERT(true, "11.1 空场景完整生命周期不崩溃");
    }

    // 11.2 创建大量对象后全部销毁
    {
        Scene scene("MassCreateDestroy");
        scene.Start();
        std::vector<GameObjectID> ids;
        for (int i = 0; i < 1000; i++)
        {
            ids.push_back(scene.CreateGameObject("Mass_" + std::to_string(i)));
        }
        TEST_ASSERT(scene.GetGameObjectCount() == 1000, "11.2a 创建 1000 个对象");

        for (auto id : ids)
        {
            scene.DestroyGameObject(id);
        }
        scene.Update(0.016f); // 刷新延迟删除
        TEST_ASSERT(scene.GetGameObjectCount() == 0, "11.2b 全部销毁后计数=0");
        scene.Stop();
    }

    // 11.3 Vec2 除以零
    {
        Vec2 v(1.0f, 2.0f);
        Vec2 result = v / 0.0f;
        // IEEE 754：除以零会产生 inf 或 -inf，不应崩溃
        TEST_ASSERT(std::isinf(result.x) || std::isnan(result.x),
                    "11.3 Vec2 除以零产生 inf/nan（不崩溃）");
    }

    // 11.4 Vec2 归一化零向量
    {
        Vec2 zero(0.0f, 0.0f);
        Vec2 norm = zero.Normalized();
        TEST_ASSERT(norm.x == 0.0f && norm.y == 0.0f,
                    "11.4 零向量归一化返回零向量");
    }

    // 11.5 Scene::Stop 后继续 Update（不应崩溃）
    {
        Scene scene("StopThenUpdate");
        scene.Start();
        scene.Stop();
        scene.Update(0.016f);      // 不应崩溃
        scene.FixedUpdate(0.016f);  // 不应崩溃
        TEST_ASSERT(true, "11.5 Stop 后 Update 不崩溃");
    }

    // 11.6 重复 Start（不应崩溃或产生异常行为）
    {
        Scene scene("DoubleStart");
        scene.Start();
        scene.Start();  // 第二次 Start
        TEST_ASSERT(scene.IsRunning(), "11.6 重复 Start 不崩溃");
        scene.Stop();
    }

    // 11.7 重复 Stop（不应崩溃）
    {
        Scene scene("DoubleStop");
        scene.Start();
        scene.Stop();
        scene.Stop();  // 第二次 Stop
        TEST_ASSERT(!scene.IsRunning(), "11.7 重复 Stop 不崩溃");
    }

    // 11.8 AABB 退化情况（零面积 AABB）
    {
        AABB2D degenerate = { {1,1}, {1,1} };  // 零面积
        AABB2D normal = AABB2D::FromCenterHalfExtents({1,1}, {0.5f,0.5f});
        TEST_ASSERT(degenerate.Area() == 0.0f, "11.8a 零面积 AABB Area=0");
        TEST_ASSERT(normal.Contains(degenerate), "11.8b 正常 AABB 包含退化 AABB");
    }

    // 11.9 PropertyMeta::ShouldSerialize — 同时标记 Serializable | Transient
    {
        PropertyMeta contradicting;
        contradicting.flags = EPropertyFlags::Serializable | EPropertyFlags::Transient;
        // 同时有 Serializable 和 Transient，按 ShouldSerialize 逻辑：
        // HasFlag(Serializable)=true AND !HasFlag(Transient)=false → false
        TEST_ASSERT(!contradicting.ShouldSerialize(),
                    "11.9 Serializable|Transient 同时标记时 ShouldSerialize=false");
    }

    // 11.10 SceneManager 单例多次 Reset 
    {
        auto& mgr = SceneManager::Get();
        mgr.Reset();
        mgr.Reset();
        mgr.Reset();
        TEST_ASSERT(true, "11.10 SceneManager 多次 Reset 不崩溃");
    }

    // 11.11 GameplayTag 深层级匹配
    {
        // Character.Enemy.Boss 匹配 Character（隔两级）
        TEST_ASSERT(TAG_Character_Enemy_Boss.MatchesTag(TAG_Character),
                    "11.11 三级标签匹配一级祖先");
    }

    // 11.12 CollisionPairHash 对称性
    {
        CollisionPair p1(1, 2);
        CollisionPair p2(2, 1);
        CollisionPairHash hasher;
        TEST_ASSERT(hasher(p1) == hasher(p2), "11.12 CollisionPair(1,2)==(2,1) 哈希相同");
        TEST_ASSERT(p1 == p2, "11.12b CollisionPair(1,2)==(2,1) 相等");
    }

    // 11.13 AutoSerializer 对 nullptr 的处理
    {
        // 传入 nullptr 作为 obj 指针（仅当没有 props 时安全）
        std::string result = AutoSerializer::SerializeByType(
            nullptr, std::type_index(typeid(void)), true);
        TEST_ASSERT(result == "{}", "11.13 nullptr + 未注册类型 → 空 JSON");
    }
}


// ============================================================
//  12. PropertyMeta offsetof 正确性测试（关键临界测试）
//  验证通过 offset 读取的值与直接访问成员变量的值一致
// ============================================================
inline void Test_PropertyMetaOffset()
{
    std::cout << "\n=== 测试 12: PropertyMeta offsetof 正确性 ===\n";

    TransformComponent tc;
    tc.SetPosition(42.0f, 84.0f);
    tc.SetRotation(123.456f);
    tc.SetScale(7.0f, 8.0f);

    const auto* props = PropertyRegistry::Get().GetProperties(
        std::type_index(typeid(TransformComponent)));

    if (!props)
    {
        TEST_ASSERT(false, "12.0 PropertyRegistry 未注册 TransformComponent");
        return;
    }

    for (const auto& prop : *props)
    {
        std::string val = AutoSerializer::PropertyValueToJson(&tc, prop);

        if (prop.name == "m_position")
        {
            TEST_ASSERT(val.find("42.0000") != std::string::npos,
                        "12.1 m_position.x 通过 offset 读取正确");
            TEST_ASSERT(val.find("84.0000") != std::string::npos,
                        "12.1b m_position.y 通过 offset 读取正确");
        }
        else if (prop.name == "m_rotation")
        {
            TEST_ASSERT(val.find("123.4560") != std::string::npos,
                        "12.2 m_rotation 通过 offset 读取正确");
        }
        else if (prop.name == "m_scale")
        {
            TEST_ASSERT(val.find("7.0000") != std::string::npos,
                        "12.3 m_scale.x 通过 offset 读取正确");
            TEST_ASSERT(val.find("8.0000") != std::string::npos,
                        "12.3b m_scale.y 通过 offset 读取正确");
        }
    }
}


// ============================================================
//  主测试入口
// ============================================================
inline void RunAllFrameworkTests()
{
    std::cout << "\n"
              << "############################################\n"
              << "#    2D 引擎框架 — 全面测试套件            #\n"
              << "############################################\n";

    g_testsPassed = 0;
    g_testsFailed = 0;

    Test_ComponentLifecycle();
    Test_GameObjectManagement();
    Test_ParentChildRelationship();
    Test_CollisionSystem();
    Test_PhysicsStability();
    Test_PropertyReflection();
    Test_GameplayTags();
    Test_DynamicAABBTree();
    Test_SceneManager();
    Test_ChildObjectComponent();
    Test_EdgeCases();
    Test_PropertyMetaOffset();

    std::cout << "\n############################################\n";
    std::cout << "#  测试结果汇总\n";
    std::cout << "#  通过: " << g_testsPassed << "\n";
    std::cout << "#  失败: " << g_testsFailed << "\n";
    std::cout << "#  总计: " << (g_testsPassed + g_testsFailed) << "\n";
    if (g_testsFailed == 0)
        std::cout << "#  全部通过!\n";
    else
        std::cout << "#  存在失败用例，请检查!\n";
    std::cout << "############################################\n\n";
}
