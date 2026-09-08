// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BrokenEdgePlayerController.generated.h"

/**
 *
 */
UCLASS()
class BROKENEDGE_API ABrokenEdgePlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void ServerSetLobbyReady(bool bReady);
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Lobby")
	void ServerSetLobbyMatchType(const FString& MatchType);
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel) override;
	/**
	 * @brief 游戏开始时初始化
	 */
	virtual void BeginPlay() override;

	/**
 * @brief 获取需要复制的属性列表
 * @param OutLifetimeProps 输出属性列表
 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
    * @brief 设置HUD生命值显示
    * @param Health 当前生命值
    * @param MaxHealth 最大生命值
    */
	void SetHUDHealth(float Health, float MaxHealth);
	/**
	 * @brief 设置 HUD 护盾值
	 * @param Shield 当前护盾值
	 * @param MaxShield 最大护盾值
	 */
	void SetHUDShield(float Shield, float MaxShield);
	
	void SetHUDWeaponAmmo(int32 Ammo);
	
	void SetHUDCarriedAmmo(int32 Ammo);

	/**
 * @brief 设置HUD分数显示
 * @param Score 分数值
 */
	void SetHUDScore(float Score);
	
	/**
 * @brief 设置HUD淘汰数显示
 * @param Defeats 淘汰数
 */
	void SetHUDDefeats(int32 Defeats);
	
	
	void SetHUDBlueTeamScore(int32 BlueScore);
	void SetHUDRedTeamScore(int32 RedScore);

	/** 初始化队伍分数 */
	void InitTeamScores();

	/** 隐藏队伍分数 */
	void HideTeamScores();

	/** 单程时间 */
	float SingleTripTime = 0.f;

	/**
 * @brief 获取服务器时间
 * @return 服务器时间
 */
	virtual float GetServerTime();
	
	/**
* @brief 客户端淘汰公告
* @param Attacker 攻击者
* @param Victim 受害者
*/
	UFUNCTION(Client, Reliable)
	void ClientDefeatedAnnouncement(APlayerState* Attacker, APlayerState* Victim);



protected:
	/// 玩家控制角色时调用
	virtual void OnPossess(APawn* InPawn) override;
	
	/// 每帧更新
	virtual void Tick(float DeltaTime) override;
	void CheckTimeSync(float DeltaTime);

	/** 时间同步频率 */
	UPROPERTY(EditAnywhere, Category = Time)
	float TimeSyncFrequency = 5.f;

	/** 时间同步运行时间 */
	float TimeSyncRunningTime = 0.f;

	/**
 * @brief 请求服务器时间
 * @param TimeOfClientRequest 客户端请求时间
 */
	UFUNCTION(Server, Reliable)
	void ServerRequestServerTime(float TimeOfClientRequest);

	/**
 * @brief 报告服务器时间到客户端
 * @param TimeOfClientRequest 客户端请求时间
 * @param TimeServerReceivedClientRequest 服务器接收请求时间
 */
	UFUNCTION(Client, Reliable)
	void ClientReportServerTime(float TimeOfClientRequest, float TimeServerReceivedClientRequest);

	/** 客户端和服务器的时间差 */
	float ClientServerDelta = 0.f;

	/// 轮询初始化（等待必要组件加载）
	void PollInit();

	/**
 * @brief 服务器端检查匹配状态
 */
	UFUNCTION(Server, Reliable)
	void ServerCheckMatchState();

	/**
 * @brief 客户端加入中途游戏
 * @param StateOfMatch 匹配状态
 * @param Warmup 热身时间
 * @param Match 比赛时间
 * @param Cooldown 冷却时间
 * @param StartingTime 开始时间
 * @param bIsTeamsMatch 是否团队匹配
 */
	UFUNCTION(Client, Reliable)
	void ClientJoinMidgame(FName StateOfMatch, float Warmup, float Match, float Cooldown, float StartingTime, bool bIsTeamsMatch);


private:
	void UpdateLobbyUI();
	void RemoveLobbyUI();
	UPROPERTY(Transient)
	class ULobbyWidget* LobbyWidget = nullptr;
	TWeakObjectPtr<UWorld> LobbyTravelWorld;
	/** HUD引用 */
	UPROPERTY()
	class ABrokenEdgeHUD* BrokenEdgeHUD;
	
	/// 角色覆盖层 Widget
	UPROPERTY()
	class UCharacterOverlay* CharacterOverlay;
	
	/** 是否初始化生命值 */
	bool bInitializeHealth = false;
	
	/** HUD生命值 */
	float HUDHealth = 0.f;
	
	/** HUD最大生命值 */
	float HUDMaxHealth = 0.f;
	
	/// HUD 护盾值
	float HUDShield = 0.f;
	
	/** 是否初始化护盾值 */
	bool bInitializeShield = false;

	/// HUD 最大护盾值
	float HUDMaxShield = 0.f;
	
	/** 是否初始化武器弹药 */
	bool bInitializeWeaponAmmo = false;

	/** HUD武器弹药 */
	float HUDWeaponAmmo = 0.f;
	
	/** HUD携带弹药 */
	float HUDCarriedAmmo = 0.f;

	/** 是否初始化携带弹药 */
	bool bInitializeCarriedAmmo = false;
	
	/** 是否初始化分数 */
	bool bInitializeScore = false;
	
	/** HUD分数 */
	float HUDScore;
	
	/** 是否初始化淘汰数 */
	bool bInitializeDefeats = false;

	/** HUD淘汰数 */
	int32 HUDDefeats;

	/** 是否显示队伍分数 */
	UPROPERTY(Replicated)
	bool bShowTeamScores = false;

	/** 游戏模式 */
	// UPROPERTY()
	// class ABlasterGameMode* BlasterGameMode;

	/** 关卡开始时间 */
	float LevelStartingTime = 0.f;

	/** 比赛时间 */
	float MatchTime = 0.f;

	/** 热身时间 */
	float WarmupTime = 0.f;

	/** 冷却时间 */
	float CooldownTime = 0.f;

	/** 匹配状态 */	//冷却中、等待开始、进行中
	UPROPERTY(ReplicatedUsing = OnRep_MatchState)
	FName MatchState;

	/**
 * @brief 匹配状态复制回调
 */
	UFUNCTION()
	void OnRep_MatchState();
};
