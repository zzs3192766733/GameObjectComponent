// ============================================================
// ConsoleApplication13.cpp
// 场景图模式 (Design B) - 2D GameObject + Component 架构演示
//
// 本文件是整个引擎的使用示例，展示了：
//   1. SceneManager 场景管理器（注册、加载、切换、卸载场景）
//   2. 创建场景和游戏对象
//   3. 给对象添加各种组件（Transform, Render, Physics, Collider, Script）
//   4. 设置碰撞体和回调
//   5. 运行碰撞检测并观察 Enter/Stay/Exit 事件
//   6. 父子关系、序列化、销毁对象等
//   7. DontDestroyOnLoad 持久对象
//   8. 场景切换演示（Single / Additive 模式）
// ============================================================

#include "SceneManager.h"
#include "Scene.h"
#include "GameObject.h"
#include "CollisionSystem.h"
#include "GameTags.h"
#include "TransformComponent.h"
#include "RenderComponent.h"
#include "PhysicsComponent.h"
#include "ScriptComponent.h"
#include "ColliderComponent.h"
#include "ChildObjectComponent.h"

// 属性反射系统
#include "PropertyMeta.h"

// 框架测试套件
#include "FrameworkTests.h"

// 性能分析器（Perfetto Trace Event Format 输出）
#include "PerfettoTracer.h"

#include <thread>  // std::this_thread::sleep_for

// ============================================================
//  CPU 忙等辅助函数
//  Windows Debug 模式下 sleep_for 精度极差（默认 ~15.6ms 定时器分辨率），
//  导致 sleep_for(1ms) 实际可能耗时 15ms+。
//  使用 CPU 忙等可以精确控制耗时，确保 Perfetto 火焰图中的数值与预期一致。
// ============================================================
static void BusyWaitMs(int ms)
{
    auto start = std::chrono::high_resolution_clock::now();
    auto target = std::chrono::microseconds(ms * 1000);
    while (std::chrono::high_resolution_clock::now() - start < target)
    {
        // 忙等，不让出 CPU
    }
}

// ============================================================
//  嵌套调用性能分析测试
//  测试 A → B → C 三层调用链，验证 Perfetto 火焰图是否正确显示嵌套层级
//  预期在 Perfetto UI 中看到：
//    |<----------- FuncA (~6ms) ----------->|
//    |  自身~1ms  |<---- FuncB (~5ms) ---->||
//    |            | 自身~2ms |<- FuncC ~3ms>||
// ============================================================
void FuncC()
{
    PROFILE_FUNCTION();
    // 模拟 ~3ms 的计算工作
    BusyWaitMs(3);
}

void FuncB()
{
    PROFILE_FUNCTION();
    // FuncB 自身的工作 ~2ms
    BusyWaitMs(2);
    // 调用 FuncC
    FuncC();
}

void FuncA()
{
    PROFILE_FUNCTION();
    // FuncA 自身的工作 ~1ms
    BusyWaitMs(1);
    // 调用 FuncB
    FuncB();
}

// SceneBuilder 子类（场景构建逻辑内聚在各自的 Builder 中）
#include "Builders/MainMenuBuilder.h"
#include "Builders/GameLevelBuilder.h"
#include "Builders/UIOverlayBuilder.h"
#include "Builders/LoadingBuilder.h"

#include <iostream>


int main()
{
	// ============================================================
	//  Profiling 初始化
	//  开启 PerfettoTracer 会话，记录整个程序运行过程中的函数耗时
	//  参考 UE: FTraceAuxiliary::Initialize()
	//  参考 Unity: Profiler.BeginSession()
	// ============================================================
	PerfettoTracer::Get().BeginSession("engine_profile");

	// 用显式作用域包裹 main 整体，确保 ProfileScope 在 EndSession() 之前析构
	// 原因：PROFILE_FUNCTION() 创建的 RAII 对象在 return 时析构，
	//       但此时 EndSession() 已把 m_sessionActive 设为 false，事件会被丢弃
	{
		PROFILE_FUNCTION();

		// ============================================================
		//  嵌套调用性能分析测试：A → B → C
		//  验证 Perfetto 火焰图能正确显示每层函数调用的耗时
		// ============================================================
		std::cout << "========== 嵌套调用性能分析测试 (A→B→C) ==========\n";
		std::cout << "  调用 FuncA() → FuncB() → FuncC()\n";
		std::cout << "  预期: FuncA ~6ms (含 FuncB), FuncB ~5ms (含 FuncC), FuncC ~3ms\n";
		FuncA();
		std::cout << "  嵌套调用测试完成，请在 Perfetto UI 中查看火焰图嵌套关系\n\n";

		std::cout << "=== 场景图模式 (Design B) - SceneManager 场景管理演示 ===\n\n";

		// ============================================================
		//  Step 0: 初始化 GameplayTag 系统（原生标签自动注册）
		//  参考 UE 的 FNativeGameplayTag 机制：
		//  所有通过 DEFINE_GAMEPLAY_TAG 宏定义的标签在 main() 前已自动链入链表，
		//  InitializeNativeTags() 统一完成真正的注册。
		//  之后就可以直接使用 TAG_Character_Player 等变量，
		//  不再需要字符串。
		// ============================================================
		// ============================================================
		//  Step 0: 初始化 GameplayTag 系统（原生标签自动注册）
		// ============================================================
		{
			PROFILE_SCOPE_CAT("GameplayTag::Initialize", "Engine.Init");
			auto& tagMgr = GameplayTagManager::Get();
			tagMgr.InitializeNativeTags();
			std::cout << "GameplayTag 系统已初始化，共注册 " << tagMgr.GetRegisteredTagCount() << " 个标签\n\n";
		}

		// ============================================================
		//  Step 1: 获取 SceneManager 单例并注册场景工厂
		//  参考 Unity 的 Build Settings 场景列表：
		//  在程序启动时声明所有可加载的场景及其构建逻辑
		// ============================================================
		auto& sceneMgr = SceneManager::Get();

		// 注册场景加载/卸载事件监听（参考 Unity SceneManager.sceneLoaded）
		sceneMgr.SetOnSceneLoaded([](const std::string& name, Scene& scene) {
			std::cout << "  >> 事件: 场景 '" << name << "' 已加载"
				<< "（对象数=" << scene.GetGameObjectCount() << "）\n";
			});
		sceneMgr.SetOnSceneUnloaded([](const std::string& name, Scene& /*scene*/) {
			std::cout << "  >> 事件: 场景 '" << name << "' 即将卸载\n";
			});

		// 注册 SceneBuilder（推荐方式）
		// 每个 Builder 子类内聚了同步构建 + 分步构建的逻辑
		// 一次注册同时提供：同步工厂（OnBuild）+ 分步工厂（OnBuildSteps）
		// Loading 场景通过 IsLoadingScene() 自动识别并设置
		sceneMgr.RegisterSceneBuilder(std::make_unique<MainMenuBuilder>());
		sceneMgr.RegisterSceneBuilder(std::make_unique<GameLevelBuilder>());
		sceneMgr.RegisterSceneBuilder(std::make_unique<UIOverlayBuilder>());
		sceneMgr.RegisterSceneBuilder(std::make_unique<LoadingBuilder>());
		sceneMgr.SetFrameLoadBudget(2); // 每帧最多执行2个加载步骤

		// ============================================================
		//  Step 2: 加载主菜单场景（Single 模式）
		//  参考 Unity: SceneManager.LoadScene("MainMenu", LoadSceneMode.Single)
		// ============================================================
		std::cout << "\n========== 加载主菜单场景 ==========\n";
		Scene* mainMenu = nullptr;
		{
			PROFILE_SCOPE_CAT("LoadScene::MainMenu", "SceneManager");
			mainMenu = sceneMgr.LoadScene("MainMenu", LoadSceneMode::Single);
		}

		// 打印场景信息
		if (mainMenu)
			mainMenu->PrintSceneInfo();

		// 模拟主菜单运行几帧
		std::cout << "\n--- 模拟主菜单运行 2 帧 ---\n";
		sceneMgr.Update(0.016f);
		sceneMgr.FixedUpdate(0.016f);
		sceneMgr.Update(0.016f);
		sceneMgr.FixedUpdate(0.016f);

		// 查看 SceneManager 状态
		sceneMgr.PrintStatus();

		// ============================================================
		//  Step 3: 场景切换 —— 从主菜单切换到游戏关卡
		//  Single 模式：卸载主菜单 → 加载游戏关卡
		//  参考 Unity: SceneManager.LoadScene("GameLevel", LoadSceneMode.Single)
		// ============================================================
		std::cout << "\n========== 切换到游戏关卡（Single 模式）==========\n";
		Scene* gameLevel = nullptr;
		{
			PROFILE_SCOPE_CAT("LoadScene::GameLevel", "SceneManager");
			gameLevel = sceneMgr.LoadScene("GameLevel", LoadSceneMode::Single);
		}

		if (gameLevel)
		{
			gameLevel->PrintSceneInfo();

			// 模拟碰撞检测
			std::cout << "\n--- 模拟游戏关卡 3 帧（含碰撞检测）---\n";

			std::cout << "\n  [第1帧] FixedUpdate:\n";
			{
				PROFILE_SCOPE_CAT("GameLevel::FixedUpdate_Frame1", "GameLoop");
				sceneMgr.FixedUpdate(0.016f);
			}

			std::cout << "\n  [第2帧] FixedUpdate:\n";
			{
				PROFILE_SCOPE_CAT("GameLevel::FixedUpdate_Frame2", "GameLoop");
				sceneMgr.FixedUpdate(0.016f);
			}

			// 移动 BoxB 离开碰撞范围
			GameObject* boxB = gameLevel->FindGameObjectByName("BoxB");
			if (boxB)
			{
				if (auto* tf = boxB->GetComponent<TransformComponent>())
					tf->SetPosition(10.0f, 0.0f);
			}

			std::cout << "\n  [第3帧] FixedUpdate（BoxB 移开）:\n";
			{
				PROFILE_SCOPE_CAT("GameLevel::FixedUpdate_Frame3", "GameLoop");
				sceneMgr.FixedUpdate(0.016f);
			}
		}

		sceneMgr.PrintStatus();

		// ============================================================
		//  Step 4: Additive 加载 UI 叠加层
		//  参考 Unity: SceneManager.LoadScene("UIOverlay", LoadSceneMode.Additive)
		//  Additive 模式不卸载现有场景，两个场景同时运行
		// ============================================================
		std::cout << "\n========== 叠加加载 UI 场景（Additive 模式）==========\n";
		sceneMgr.LoadScene("UIOverlay", LoadSceneMode::Additive);

		sceneMgr.PrintStatus();

		// 两个场景同时更新
		std::cout << "\n--- 两个场景同时运行 1 帧 ---\n";
		sceneMgr.Update(0.016f);
		sceneMgr.FixedUpdate(0.016f);

		// ============================================================
		//  Step 5: 卸载 UI 叠加层
		//  参考 Unity: SceneManager.UnloadSceneAsync("UIOverlay")
		// ============================================================
		std::cout << "\n========== 卸载 UI 叠加层 ==========\n";
		sceneMgr.UnloadScene("UIOverlay");

		sceneMgr.PrintStatus();

		// ============================================================
		//  Step 6: DontDestroyOnLoad 演示
		//  在游戏关卡中创建一个"全局管理器"对象，标记为持久
		//  然后切换回主菜单，验证该对象仍然存活
		// ============================================================
		std::cout << "\n========== DontDestroyOnLoad 演示 ==========\n";
		if (gameLevel)
		{
			// 创建全局背景音乐管理器
			GameObjectID bgmID = gameLevel->CreateGameObject("BGM_Manager");
			GameObject* bgm = gameLevel->GetGameObject(bgmID);
			if (bgm)
			{
				bgm->AddComponent<ScriptComponent>()->SetScriptName("BGMController");
				std::cout << "  创建 BGM_Manager (ID=" << bgmID << ") 在 GameLevel 中\n";

				// 标记为持久对象
				sceneMgr.DontDestroyOnLoad(bgmID);
			}
		}

		// 切换回主菜单（Single 模式会卸载 GameLevel，但持久对象保留）
		std::cout << "\n--- 切换回主菜单（Single 模式）---\n";
		sceneMgr.LoadScene("MainMenu", LoadSceneMode::Single);

		sceneMgr.PrintStatus();

		// ============================================================
		//  Step 7: GameplayTag 演示
		// ============================================================
		std::cout << "\n========== GameplayTag 演示 ==========\n";

		// 直接使用 TAG_xxx 变量（C++ 类型安全，无字符串）
		std::cout << "  TAG_Character_Enemy_Boss.MatchesTag(TAG_Character_Enemy) = "
			<< (TAG_Character_Enemy_Boss.MatchesTag(TAG_Character_Enemy) ? "true" : "false") << "\n";
		std::cout << "  TAG_Character_Enemy_Boss.MatchesTag(TAG_Character) = "
			<< (TAG_Character_Enemy_Boss.MatchesTag(TAG_Character) ? "true" : "false") << "\n";
		std::cout << "  TAG_Character_Enemy.MatchesTag(TAG_Character_Enemy_Boss) = "
			<< (TAG_Character_Enemy.MatchesTag(TAG_Character_Enemy_Boss) ? "true" : "false") << "\n";
		std::cout << "  TAG_Character_Player.MatchesTag(TAG_Character_Enemy) = "
			<< (TAG_Character_Player.MatchesTag(TAG_Character_Enemy) ? "true" : "false") << "\n";

		// ============================================================
		//  Step 8: 序列化当前活动场景
		// ============================================================
		if (Scene* active = sceneMgr.GetActiveScene())
		{
			std::cout << "\n--- 序列化活动场景 ---\n";
			std::cout << active->Serialize() << "\n";
		}

		// 清理
		std::cout << "\n========== 重置 SceneManager ==========\n";
		sceneMgr.Reset();

		// ============================================================
		//  Step 9: 异步场景加载演示（带 Loading 过渡画面）
		//  参考 Unity: SceneManager.LoadSceneAsync("GameLevel", LoadSceneMode.Single)
		//  参考 UE:    UGameplayStatics::OpenLevelBySoftObjectPtr
		//
		//  流程：
		//    1. 重新注册工厂后加载主菜单
		//    2. 从主菜单发起异步加载 GameLevel
		//    3. SceneManager 自动叠加 Loading 场景
		//    4. 模拟游戏主循环：每帧 Update() 推进异步加载
		//    5. 加载完成后自动卸载 Loading，激活 GameLevel
		// ============================================================
		std::cout << "\n========== 异步场景加载演示 ==========\n";

		// 重新注册工厂（Reset 不清除工厂）
		// 先加载主菜单作为起始场景
		sceneMgr.LoadScene("MainMenu", LoadSceneMode::Single);
		std::cout << "\n--- 主菜单运行中，玩家点击开始游戏 ---\n";

		// 发起异步加载 GameLevel（Single 模式）
		auto* asyncOp = sceneMgr.LoadSceneAsync("GameLevel", LoadSceneMode::Single);
		if (asyncOp)
		{
			// 可选：设置完成回调
			asyncOp->SetOnCompleted([]() {
				std::cout << "  >> [回调] 异步加载已完成！游戏开始！\n";
				});

			// 模拟游戏主循环（每帧调用 Update，异步加载在 Update 内部推进）
			// 注意：asyncOp 在加载完成后会被 SceneManager 销毁，
			//       因此使用 SceneManager::IsAsyncLoading() 判断是否完成
			int frame = 0;
			while (sceneMgr.IsAsyncLoading())
			{
				++frame;
				float progress = sceneMgr.GetAsyncLoadProgress();
				std::cout << "\n  [帧 " << frame << "] 加载进度: "
					<< static_cast<int>(progress * 100) << "%\n";

				// 驱动游戏主循环
				sceneMgr.Update(0.016f);
				sceneMgr.FixedUpdate(0.016f);
				sceneMgr.Render();

				// 安全阀：防止无限循环
				if (frame > 100)
				{
					std::cerr << "  [错误] 异步加载超时！\n";
					break;
				}
			}

			std::cout << "\n--- 异步加载完成，最终状态 ---\n";
			sceneMgr.PrintStatus();
		}

		// ============================================================
		//  Step 10: ChildObjectComponent 子对象容器演示
		//  参考 UE UChildActorComponent 的设计：
		//  一个组件可以管理多个子 GameObject 的创建/销毁/查询
		//  子类可重写 OnChildCreated/OnChildDestroyed 实现自定义逻辑
		// ============================================================
		std::cout << "\n========== ChildObjectComponent 子对象容器演示 ==========\n";

		// 创建一个独立场景用于演示
		sceneMgr.RegisterSceneFactory("TestScene", [](Scene& scene)
			{
				// 创建一个"敌人生成器"对象
				auto spawnerID = scene.CreateGameObject("EnemySpawner");
				auto* spawner = scene.GetGameObject(spawnerID);

				// 给生成器添加 Transform 和 ChildObjectComponent
				spawner->AddComponent<TransformComponent>()->SetPosition(0, 0);

				auto* container = spawner->AddComponent<ChildObjectComponent>();
				container->SetMaxChildren(5);  // 最多管理5个子对象

				// 使用 CreateChild 创建子对象（自动建立父子关系）
				std::cout << "  创建3个敌人子对象...\n";
				for (int i = 0; i < 3; ++i)
				{
					container->CreateChild("Enemy_" + std::to_string(i), [i](GameObject* child)
						{
							// 初始化回调：给每个敌人添加组件
							child->AddComponent<TransformComponent>()->SetPosition(
								static_cast<float>(i * 2), 0.0f);
							child->AddComponent<RenderComponent>()->SetVisible(true);
						});
				}

				std::cout << "  容器子对象数量: " << container->GetChildCount() << "\n";
				std::cout << "  容器调试信息: " << container->GetDebugInfo() << "\n";

				// 按名称查找
				if (auto* enemy1 = container->FindChildByName("Enemy_1"))
					std::cout << "  按名称查找 'Enemy_1': ID=" << enemy1->GetID() << "\n";

				// 按索引查找
				if (auto* first = container->GetChildAt(0))
					std::cout << "  按索引查找 [0]: '" << first->GetName() << "'\n";

				// 遍历所有子对象
				std::cout << "  遍历所有子对象:\n";
				container->ForEachChild([](GameObject* child)
					{
						auto* tf = child->GetComponent<TransformComponent>();
						std::cout << "    - '" << child->GetName() << "' 位置=("
							<< (tf ? tf->GetPosition().x : 0) << ", "
							<< (tf ? tf->GetPosition().y : 0) << ")\n";
					});

				// 销毁指定的子对象
				auto ids = container->GetChildIDs();
				if (!ids.empty())
				{
					std::cout << "  销毁第一个子对象 (ID=" << ids[0] << ")...\n";
					container->DestroyChild(ids[0]);
					std::cout << "  销毁后子对象数量: " << container->GetChildCount() << "\n";
				}

				// 序列化
				std::cout << "  容器序列化: " << container->Serialize() << "\n";
			});

		Scene* testScene = sceneMgr.LoadScene("TestScene", LoadSceneMode::Single);
		if (testScene)
		{
			testScene->PrintSceneInfo();
			// 运行1帧
			sceneMgr.Update(0.016f);
			sceneMgr.FixedUpdate(0.016f);
		}

		// 卸载场景时，ChildObjectComponent 的 OnDetach 会自动清理子对象
		std::cout << "\n  卸载场景（ChildObjectComponent::OnDetach 自动清理子对象）...\n";

		// ============================================================
		//  Step 11: 属性反射系统 (PropertyMeta) 演示
		//  参考 C# 的 [SerializeField] / [NonSerialized] 属性标签
		//  参考 UE 的 UPROPERTY(SaveGame) / UPROPERTY(Transient)
		//
		//  核心演示：
		//    - MPROPERTY(Serializable) 标记的属性会被自动序列化
		//    - MPROPERTY(Transient)     标记的属性不会被序列化
		//    - 没有 MPROPERTY 标记的属性完全不被反射系统感知
		// ============================================================
		std::cout << "\n========== 属性反射系统 (MPROPERTY) 演示 ==========\n";
		std::cout << "模拟 C# [SerializeField]/[NonSerialized] 和 UE UPROPERTY() 标签\n\n";

		// 显示全局注册表状态
		auto& registry = PropertyRegistry::Get();
		std::cout << "  PropertyRegistry 已注册类型数: "
			<< registry.GetRegisteredTypeCount() << "\n";

		// 创建一个 TransformComponent 并设置属性值
		TransformComponent demoTransform;
		demoTransform.SetPosition(100.0f, 200.0f);
		demoTransform.SetRotation(45.0f);
		demoTransform.SetScale(2.0f, 3.0f);

		// 获取 TransformComponent 的属性元数据列表
		const auto* props = registry.GetProperties(
			std::type_index(typeid(TransformComponent)));

		if (props)
		{
			// 1. 打印属性反射报告（显示所有注册的属性及其标志）
			std::cout << "\n  --- 属性反射报告 ---\n";
			std::cout << AutoSerializer::GeneratePropertyReport(&demoTransform, *props);

			// 2. 自动序列化（仅输出 Serializable 标记的属性）
			std::cout << "  --- 自动序列化（仅 Serializable 属性）---\n";
			std::string autoJson = AutoSerializer::SerializeToJson(
				&demoTransform, *props, true);
			std::cout << "  " << autoJson << "\n";

			// 3. 序列化所有属性（包括 Transient）作为对比
			std::cout << "\n  --- 序列化所有属性（含 Transient）---\n";
			std::string allJson = AutoSerializer::SerializeToJson(
				&demoTransform, *props, false);
			std::cout << "  " << allJson << "\n";

			// 4. 对比手动序列化 vs 自动序列化
			std::cout << "\n  --- 手动 Serialize() vs AutoSerialize 对比 ---\n";
			std::cout << "  手动: " << demoTransform.Serialize() << "\n";
			std::cout << "  自动: " << demoTransform.AutoSerializeProperties() << "\n";

			// 5. 验证 Transient 属性确实不在自动序列化结果中
			bool velocityInAuto = autoJson.find("m_velocity") != std::string::npos;
			bool velocityInAll  = allJson.find("m_velocity") != std::string::npos;
			std::cout << "\n  --- Transient 属性验证 ---\n";
			std::cout << "  m_velocity 在自动序列化中: " << (velocityInAuto ? "出现 [BUG!]" : "未出现 [OK]") << "\n";
			std::cout << "  m_velocity 在全量序列化中: " << (velocityInAll ? "出现 [OK]" : "未出现 [BUG!]") << "\n";
		}
		else
		{
			std::cout << "  [错误] TransformComponent 未在 PropertyRegistry 中注册!\n";
		}

		std::cout << "\n";

		// ============================================================
		//  Step 12: 框架全面测试套件
		//  检查框架各子系统的正确性、边界条件、临界情况
		// ============================================================
		std::cout << "\n========== 最终重置 SceneManager（测试前清理）==========\n";
		sceneMgr.Reset();

		// 运行所有框架测试
		RunAllFrameworkTests();

		// 最终清理
		std::cout << "\n========== 最终重置 SceneManager ==========\n";
		sceneMgr.Reset();

	} // ← PROFILE_FUNCTION() 的 ProfileScope 在此析构，写入 main 的整体耗时

	// ============================================================
	//  Step 11: Profiling 结果导出
	//  结束会话并生成 engine_profile.json
	//  可在 https://ui.perfetto.dev/ 中打开查看火焰图
	// ============================================================
	std::cout << "\n========== 导出性能分析数据 ==========\n";
	PerfettoTracer::Get().EndSession();
	std::cout << "EndSession 完成" << std::endl;

	std::cout << "\n=== 程序结束 ===\n";
	return 0;
}


// ============================================================
//  场景构建逻辑已内聚到各 SceneBuilder 子类中：
//    - MainMenuBuilder   (Builders/MainMenuBuilder.h)
//    - GameLevelBuilder   (Builders/GameLevelBuilder.h)
//    - UIOverlayBuilder   (Builders/UIOverlayBuilder.h)
//    - LoadingBuilder     (Builders/LoadingBuilder.h)
//
//  不再需要散落的工厂函数（BuildMainMenuScene、BuildGameLevelSteps 等）
//  参考 UE AGameModeBase 的子类化方式：
//    每种关卡类型对应一个 GameMode 子类
// ============================================================