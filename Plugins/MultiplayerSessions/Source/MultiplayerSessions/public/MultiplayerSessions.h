// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** 插件统一日志分类，便于在 Dedicated Server 日志中筛选会话生命周期。 */
DECLARE_LOG_CATEGORY_EXTERN(LogMultiplayerSessions, Log, All);

/** Runtime 模块入口。当前没有全局资源需要在启动/关闭时手动管理。 */
class FMultiplayerSessionsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
