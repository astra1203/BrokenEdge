// Copyright Epic Games, Inc. All Rights Reserved.

// #include "Modules/ModuleManager.h"
// #include "MultiplayerSessions.h"
#include "MultiplayerSessions/public/MultiplayerSessions.h"

DEFINE_LOG_CATEGORY(LogMultiplayerSessions);

IMPLEMENT_MODULE(FMultiplayerSessionsModule, MultiplayerSessions)


void FMultiplayerSessionsModule::StartupModule()
{
	// 会话对象由 UE 的 Subsystem 框架按 GameInstance/World 生命周期自动创建，无需在模块启动时 new。
}

void FMultiplayerSessionsModule::ShutdownModule()
{
	// 各 Subsystem 会在模块卸载前执行 Deinitialize 并清理 OSS 委托，因此这里无需重复释放。
}

// 将本类注册为名为 MultiplayerSessions 的 Runtime 模块入口。
// IMPLEMENT_MODULE(FMultiplayerSessionsModule, MultiplayerSessions)
