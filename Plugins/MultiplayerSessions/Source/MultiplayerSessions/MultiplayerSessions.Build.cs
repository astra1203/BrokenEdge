// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class MultiplayerSessions : ModuleRules
{
	public MultiplayerSessions(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Public 头文件直接暴露 UGameInstanceSubsystem 和 OnlineSession 原生类型，
		// 因此依赖本插件的其他模块也必须能够看到这些模块的公开头文件。
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"OnlineSubsystem",
				// UMenu 公开继承自 UUserWidget 并使用 UButton（UMG），
				// 因此 UMG/Slate/SlateCore 必须作为公开依赖，否则链接时无法解析 UUserWidget 等符号。
				"UMG",
				"Slate",
				"SlateCore"
			});

		// OnlineSubsystemUtils 只在 Private 实现中用于按当前 World 获取正确的 OSS 实例，
		// 不属于插件对外 API，所以保持为私有依赖。
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"OnlineSubsystemUtils"
			});
	}
}
