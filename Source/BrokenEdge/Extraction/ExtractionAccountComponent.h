#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExtractionPersistence.h"
#include "ExtractionAccountComponent.generated.h"

UCLASS()
class BROKENEDGE_API UExtractionAccountComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UExtractionAccountComponent();
	virtual void TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Tick) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(Replicated) FExtractionProfileView Profile;
	UPROPERTY(Replicated) bool Bound = false;
	UPROPERTY(Replicated) bool Locked = false;
	UPROPERTY(Replicated) FString Message;
	UExtractionPersistence* Store() const;
	void Refresh();
	void RequestMove(FExtractionStorageRequest Request);
	UFUNCTION(Server, Reliable) void ServerMove(FExtractionStorageRequest Request);
	UFUNCTION(Server, Reliable) void ServerBind(FGuid Id, const FString& Credential);
	UFUNCTION(Client, Reliable) void ClientCredentials(FGuid StoreId, FGuid ProfileId, const FString& Credential);
	UFUNCTION(Client, Reliable) void ClientBind(FGuid StoreId);
	UFUNCTION(Server, Reliable) void ServerCredentialSaved(bool Success);
private:
	bool Requested = false;
	FString CredentialPath(FGuid StoreId) const;
};
