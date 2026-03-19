
# ============================================================
#  Mini2D Engine — CMakeLists.txt
#
#  跨平台构建配置（Windows + macOS）
#  C++17 标准，纯头文件 + 源文件项目，无第三方依赖
#
#  使用方式：
#    # 生成构建文件
#    cmake -B build -S .
#
#    # 编译（Debug 模式）
#    cmake --build build --config Debug
#
#    # 编译（Release 模式）
#    cmake --build build --config Release
#
#    # 运行
#    ./build/Debug/Mini2DEngine        (Windows)
#    ./build/Mini2DEngine               (macOS)
#
#  Windows 额外支持：
#    # 生成 Visual Studio 解决方案
#    cmake -B build -S . -G "Visual Studio 17 2022" -A x64
#
#  macOS 额外支持：
#    # 生成 Xcode 项目
#    cmake -B build -S . -G Xcode
# ============================================================
cmake_minimum_required(VERSION 3.15)

project(Mini2DEngine
    VERSION 0.1.0
    DESCRIPTION "2D 游戏引擎 — 模仿 UE/Unity 架构设计"
    LANGUAGES CXX
)

# ============================================================
#  C++ 标准设置（与 vcxproj 中 stdcpp17 一致）
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
#  源文件列表
#  显式列出所有 .cpp 文件（不使用 GLOB，避免新增文件后缓存不刷新）
#  参考 UE 的 Build.cs 显式列出源文件的方式
# ============================================================
set(SOURCES
    # 入口文件
    ConsoleApplication13.cpp

    # Core 层
    Core/Object/GameObject.cpp
    Core/Tag/GameTags.cpp
    Core/Reflection/PropertyMeta.cpp

    # Engine 层 — 组件
    Engine/Components/TransformComponent.cpp
    Engine/Components/PhysicsComponent.cpp
    Engine/Components/ColliderComponent.cpp
    Engine/Components/RenderComponent.cpp
    Engine/Components/ScriptComponent.cpp
    Engine/Components/ChildObjectComponent.cpp

    # Engine 层 — 物理
    Engine/Physics/CollisionSystem.cpp

    # Engine 层 — 性能分析
    Engine/Profiling/PerfettoTracer.cpp

    # Engine 层 — 场景
    Engine/Scene/Scene.cpp
)

# ============================================================
#  头文件列表
#  CMake 不需要头文件参与编译，但显式列出可以让 IDE 正确显示
#  参考 UE 在 Build.cs 中通过 PublicIncludePaths 管理头文件
# ============================================================
set(HEADERS
    # Core 层 — 反射
    Core/Reflection/PropertyMeta.h

    # Core 层 — 类型
    Core/Types/CoreTypes.h

    # Core 层 — 对象
    Core/Object/Component.h
    Core/Object/GameObject.h

    # Core 层 — 标签
    Core/Tag/GameplayTag.h
    Core/Tag/GameTags.h

    # Engine 层 — 组件
    Engine/Components/TransformComponent.h
    Engine/Components/PhysicsComponent.h
    Engine/Components/ColliderComponent.h
    Engine/Components/RenderComponent.h
    Engine/Components/ScriptComponent.h
    Engine/Components/ChildObjectComponent.h

    # Engine 层 — 物理
    Engine/Physics/CollisionSystem.h
    Engine/Physics/DynamicAABBTree.h

    # Engine 层 — 场景
    Engine/Scene/Scene.h
    Engine/Scene/SceneManager.h
    Engine/Scene/AsyncSceneOperation.h
    Engine/Scene/SceneBuilder.h

    # Engine 层 — 性能分析
    Engine/Profiling/PerfettoTracer.h

    # Engine 层 — 场景构建器
    Engine/Scene/Builders/MainMenuBuilder.h
    Engine/Scene/Builders/GameLevelBuilder.h
    Engine/Scene/Builders/UIOverlayBuilder.h
    Engine/Scene/Builders/LoadingBuilder.h

    # 测试
    Tests/FrameworkTests.h
)

# ============================================================
#  创建可执行目标
# ============================================================
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# ============================================================
#  头文件搜索路径
#  与 vcxproj 中的 AdditionalIncludeDirectories 完全对齐：
#    Core\Types; Core\Object; Core\Tag;
#    Engine\Components; Engine\Physics;
#    Engine\Scene; Engine\Scene\Builders
# ============================================================
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Types
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Object
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Tag
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Reflection
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Components
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Physics
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Scene
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Scene/Builders
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/Profiling
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests
)

# ============================================================
#  编译器选项（跨平台兼容）
# ============================================================
if(MSVC)
    # Windows (MSVC) 编译选项 — 与 vcxproj 设置对齐
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W3             # 警告等级3（对应 WarningLevel Level3）
        /utf-8          # 源文件和执行字符集都用 UTF-8，解决中文乱码
        /permissive-    # 一致性模式（对应 ConformanceMode true）
        /sdl            # SDL 安全检查（对应 SDLCheck true）
    )

    # Debug 模式预处理器宏
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:_DEBUG _CONSOLE>
        $<$<CONFIG:Release>:NDEBUG _CONSOLE>
        UNICODE
        _UNICODE
    )

    # Release 优化选项
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/O2 /Oi /GL>    # 优化 + 内部函数 + 全程序优化
    )
    target_link_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Release>:/LTCG>          # 链接时代码生成
    )

elseif(APPLE)
    # macOS (Clang) 编译选项
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall               # 开启常见警告
        -Wextra             # 额外警告
        -Wpedantic          # 严格标准一致性
    )

    # Debug/Release 优化级别
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:-g -O0>           # 调试信息 + 无优化
        $<$<CONFIG:Release>:-O2 -DNDEBUG>   # O2 优化
    )

else()
    # Linux / 其他 (GCC/Clang) 编译选项
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:-g -O0>
        $<$<CONFIG:Release>:-O2 -DNDEBUG>
    )
endif()

# ============================================================
#  IDE 分组（Visual Studio / Xcode 中的文件夹结构）
#  使得在 IDE 中看到与磁盘一致的目录结构
#  参考 UE 的 Source Groups 分组方式
# ============================================================
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

# ============================================================
#  输出构建信息
# ============================================================
message(STATUS "")
message(STATUS "============================================")
message(STATUS "  ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:     ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:     ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "============================================")
message(STATUS "")
