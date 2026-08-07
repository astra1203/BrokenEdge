// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Menu.generated.h"

/**
 * 多人游戏主菜单控件（UMG）
 * @brief 提供“创建主机”与“加入会话”两个按钮，并通过多人会话子系统
 *        完成会话的创建、查找、加入等流程，同时负责输入模式与鼠标光标的切换。
 */
UCLASS()
class MULTIPLAYERSESSIONS_API UMenu : public UUserWidget
{
	GENERATED_BODY()

	// 菜单初始化入口，由关卡蓝图调用以设置会话参数并绑定按钮事件。
	UFUNCTION(BlueprintCallable)
	void MenuSetup(int32 NumOfPublicConnections = 8, FString TypeOfMatch = FString(TEXT("FreeForAll")),
	               FString LobbyPath = FString(TEXT("/Game/Maps/Lobby")));


protected:
	// 控件初始化时绑定按钮点击委托。
	virtual bool Initialize() override;
	// 控件销毁时清理输入模式并解绑。
	virtual void NativeDestruct() override;

	UFUNCTION()
	void OnCreateSession(bool bWasSuccessful);

private:
	// 绑定到蓝图中的“创建主机”按钮。
	UPROPERTY(meta = (BindWidget))
	class UButton* HostButton;

	// 绑定到蓝图中的“加入会话”按钮。
	UPROPERTY(meta = (BindWidget))
	UButton* JoinButton;

	UFUNCTION()
	void HostButtonClicked();

	UFUNCTION()
	void JoinButtonClicked();

	// 移除菜单控件并恢复游戏输入模式与鼠标光标状态。
	void MenuTearDown();

	//用来处理所有在线会话功能的子系统
	class UMultiplayerSessionsSubsystem* MultiplayerSessionsSubsystem;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	int32 NumPublicConnections = 8;

	UPROPERTY(BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
	FString MatchType = TEXT("FreeForAll");

	FString PathToLobby = TEXT("");
};
