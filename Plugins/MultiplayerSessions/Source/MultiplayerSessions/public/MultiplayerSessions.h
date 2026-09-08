// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/** 插件统一日志分类，便于在 Dedicated Server 日志中筛选会话生命周期。 */
DECLARE_LOG_CATEGORY_EXTERN(LogMultiplayerSessions, Log, All);

/** 专服会话使用的配置节和广告键。模块只负责加载，建房由专服大厅的 BeginPlay 发起。 */
namespace MultiplayerSessionKeys
{
	inline const FName MatchType(TEXT("MatchType"));
	inline const FName ProjectName(TEXT("ProjectName"));
	inline constexpr const TCHAR* DedicatedServerConfig = TEXT("MultiplayerSessions.DedicatedServer");
}

/** Runtime 模块入口。专服会话属于 GameInstance，不在模块加载时创建。 */
class FMultiplayerSessionsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
