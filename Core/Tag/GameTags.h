#pragma once
// ============================================================
//  GameTags.h
//  集中声明所有游戏标签（对标 UE 的 NativeGameplayTags.h）
//
//  设计理念：
//  - 所有标签在这里 extern 声明，任何 #include "GameTags.h" 的文件
//    都可以直接使用这些标签变量
//  - 标签的实际定义和注册在 GameTags.cpp 中完成
//  - 使用时是 C++ 变量名（编译期类型安全），不再传字符串
//
//  使用方式：
//    #include "GameTags.h"
//    if (myTag.MatchesTag(TAG_Character_Player)) { ... }
//    obj->AddTag(TAG_Attribute_Damageable);
//    container.HasTag(TAG_Character_Enemy);  // 层级匹配
// ============================================================
#include "GameplayTag.h"

// ============================================================
//  角色标签 (Character.*)
// ============================================================
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character);                // Character
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Player);         // Character.Player
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Enemy);          // Character.Enemy
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Enemy_Boss);     // Character.Enemy.Boss
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_Enemy_Minion);   // Character.Enemy.Minion
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Character_NPC);            // Character.NPC

// ============================================================
//  环境标签 (Environment.*)
// ============================================================
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Environment_Terrain);      // Environment.Terrain
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Environment_Obstacle);     // Environment.Obstacle
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Environment_Destructible); // Environment.Destructible

// ============================================================
//  可交互标签 (Interactable.*)
// ============================================================
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Interactable_Pickup);      // Interactable.Pickup
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Interactable_Trigger);     // Interactable.Trigger
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Interactable_Door);        // Interactable.Door

// ============================================================
//  属性标签 (Attribute.*)
// ============================================================
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Attribute_Damageable);     // Attribute.Damageable
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Attribute_Invincible);     // Attribute.Invincible
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Attribute_Static);         // Attribute.Static

// ============================================================
//  投射物标签 (Projectile.*)
// ============================================================
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Projectile_Bullet);        // Projectile.Bullet
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Projectile_Missile);       // Projectile.Missile

// ============================================================
//  UI 标签 (UI.*)
// ============================================================
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_HUD);                   // UI.HUD
DECLARE_GAMEPLAY_TAG_EXTERN(TAG_UI_Popup);                 // UI.Popup
