// ============================================================
//  GameTags.cpp
//  集中定义和注册所有游戏标签（对标 UE 的 NativeGameplayTags.cpp）
//
//  每个 DEFINE_GAMEPLAY_TAG 宏展开后创建一个全局 NativeGameplayTag 变量，
//  其构造函数在 main() 之前自动将标签注册到 GameplayTagManager 的链表中。
//  随后 InitializeNativeTags() 统一完成真正的注册。
//
//  添加新标签：
//  1. 在 GameTags.h 中添加 DECLARE_GAMEPLAY_TAG_EXTERN(TAG_xxx);
//  2. 在本文件中添加 DEFINE_GAMEPLAY_TAG(TAG_xxx, "Tag.String");
//  3. 任何 #include "GameTags.h" 的文件都可以直接使用 TAG_xxx
// ============================================================
#include "GameTags.h"

// ============================================================
//  角色标签 (Character.*)
// ============================================================
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character,               "Character",               "所有角色的根标签");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Player,        "Character.Player",        "玩家角色");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Enemy,         "Character.Enemy",         "敌人角色根标签");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Enemy_Boss,    "Character.Enemy.Boss",    "Boss 敌人");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_Enemy_Minion,  "Character.Enemy.Minion",  "小兵敌人");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Character_NPC,           "Character.NPC",           "非玩家角色");

// ============================================================
//  环境标签 (Environment.*)
// ============================================================
DEFINE_GAMEPLAY_TAG(TAG_Environment_Terrain,      "Environment.Terrain");
DEFINE_GAMEPLAY_TAG(TAG_Environment_Obstacle,     "Environment.Obstacle");
DEFINE_GAMEPLAY_TAG(TAG_Environment_Destructible, "Environment.Destructible");

// ============================================================
//  可交互标签 (Interactable.*)
// ============================================================
DEFINE_GAMEPLAY_TAG(TAG_Interactable_Pickup,  "Interactable.Pickup");
DEFINE_GAMEPLAY_TAG(TAG_Interactable_Trigger, "Interactable.Trigger");
DEFINE_GAMEPLAY_TAG(TAG_Interactable_Door,    "Interactable.Door");

// ============================================================
//  属性标签 (Attribute.*)
// ============================================================
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Attribute_Damageable, "Attribute.Damageable", "可受伤害");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Attribute_Invincible, "Attribute.Invincible", "无敌状态");
DEFINE_GAMEPLAY_TAG_COMMENT(TAG_Attribute_Static,     "Attribute.Static",     "静态不可移动");

// ============================================================
//  投射物标签 (Projectile.*)
// ============================================================
DEFINE_GAMEPLAY_TAG(TAG_Projectile_Bullet,  "Projectile.Bullet");
DEFINE_GAMEPLAY_TAG(TAG_Projectile_Missile, "Projectile.Missile");

// ============================================================
//  UI 标签 (UI.*)
// ============================================================
DEFINE_GAMEPLAY_TAG(TAG_UI_HUD,   "UI.HUD");
DEFINE_GAMEPLAY_TAG(TAG_UI_Popup, "UI.Popup");
