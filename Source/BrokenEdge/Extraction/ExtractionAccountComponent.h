#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionPersistence.h"
#include "ExtractionAccountComponent.generated.h"
// 控制器上的账号桥接组件：客户端保存凭据，服务器绑定档案并复制本人可见的仓库快照。
// RPC 只传操作请求与身份信息，仓库结果始终由服务器 Persistence 计算。

UCLASS()
class BROKENEDGE_API UExtractionAccountComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	// 默认启用组件复制，每 0.25 秒在服务器刷新绑定与锁定状态。
	UExtractionAccountComponent();
	// 服务器按需加载仓库；有档案则刷新，没有档案则只发起一次客户端凭据绑定。
	virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	// Profile、Bound、Locked 和 Message 只发给拥有该控制器的客户端，其他玩家不应看到仓库快照。
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated) FExtractionProfileView Profile;
	UPROPERTY(Replicated) bool Bound = false;
	UPROPERTY(Replicated) bool Locked = false;
	UPROPERTY(Replicated) FString Message;
	// 从当前 GameInstance 获取仓库子系统；本组件的服务器操作通过它访问长期状态。
	UExtractionPersistence* Store() const;
	// 服务器复制档案最新快照，并根据准备、旅行、模式切换和存储状态更新锁定标志。
	void Refresh();
	// 为本次仓库操作生成唯一 ID 并携带当前快照版本，随后调用服务器 RPC。
	void RequestMove(FExtractionStorageRequest Request);
	// 为交易或任务操作生成唯一 ID，携带版本供服务器检测过期请求。
	void RequestEconomy(FExtractionEconomyRequest Request);
	// 服务器重新计算锁定状态，仅在已绑定且未锁定时执行交易并回传持久化结果。
	UFUNCTION(Server, Reliable) void ServerEconomy(FExtractionEconomyRequest Request);
	// 服务器校验大厅编辑权限，再请求仓库整理；不直接相信客户端界面是否允许点击。
	UFUNCTION(Server, Reliable) void ServerMove(FExtractionStorageRequest Request);
	// 服务器校验请求时机、凭据和在线身份冲突；新凭据需要客户端确认保存后才完成绑定。
	UFUNCTION(Server, Reliable) void ServerBind(FGuid Id, const FString& Credential);
	// 所属客户端保存新凭据，并将保存结果回传；保存失败时不能放行出战。
	UFUNCTION(Client, Reliable) void ClientCredentials(FGuid StoreId, FGuid ProfileId, const FString& Credential);
	// 所属客户端读取本地凭据再请求服务器绑定；文件缺失请求新建，文件损坏则提示错误。
	UFUNCTION(Client, Reliable) void ClientBind(FGuid StoreId);
	// 处理凭据落盘确认，再次检查在线冲突，防止确认往返期间另一连接占用同一档案。
	UFUNCTION(Server, Reliable) void ServerCredentialSaved(bool Success);
private:
	bool Requested = false;
	// 按服务器仓库 ID 和本地档案标签隔离凭据；PIE 追加实例号，避免双客户端共用身份。
	FString CredentialPath(FGuid StoreId) const;
};
