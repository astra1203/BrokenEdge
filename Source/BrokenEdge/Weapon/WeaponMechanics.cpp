#include "WeaponMechanics.h"
#include "BrokenEdge/Extraction/Inventory/ExtractionItems.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Curves/CurveFloat.h"
#include "Misc/Crc.h"

namespace
{
	using JObject = TSharedPtr<FJsonObject>;
	float Number(const JObject& O, const TCHAR* Key) { return O->GetNumberField(Key); }
	FVector2D XY(const JObject& O) { return FVector2D(Number(O, TEXT("X")), Number(O, TEXT("Y"))); }

	TArray<float> Numbers(const JObject& O, const TCHAR* Key)
	{
		TArray<float> Result;
		for (const auto& V : O->GetArrayField(Key)) Result.Add(V->AsNumber());
		return Result;
	}

	FWeaponRecoilSequence Sequence(const JObject& O)
	{
		FWeaponRecoilSequence R;
		auto Parse = [](const FString& Value)
		{
			TArray<FString> Tokens;
			Value.ParseIntoArray(Tokens, TEXT(","), true);
			TArray<float> Values;
			for (const auto& Token : Tokens) Values.Add(FCString::Atof(*Token.TrimStartAndEnd()));
			return Values;
		};
		R.Pitch = Parse(O->GetStringField(TEXT("VerticalRecoils")));
		R.Yaw = Parse(O->GetStringField(TEXT("HorizontalRecoils")));
		R.PitchScale = Number(O, TEXT("VerticalScale"));
		R.YawScale = Number(O, TEXT("HorizontalScale"));
		auto Range = [](const JObject& V)
		{
			return FVector2D(Number(V, TEXT("MinValue")), Number(V, TEXT("MaxValue")));
		};
		R.PitchRandom = Range(O->GetObjectField(TEXT("VerticalRandomRecoil")));
		R.YawRandom = Range(O->GetObjectField(TEXT("HorizontalRandomRecoil")));
		return R;
	}
}

const FResolvedWeaponStats* WeaponMechanics::Find(FName Definition)
{
	static TMap<FName, FResolvedWeaponStats> Profiles;
	static bool Loaded = false;
	if (!Loaded)
	{
		Loaded = true;
		FString Text;
		JObject Root;
		if (!FFileHelper::LoadFileToString(
				Text, *(FPaths::ProjectContentDir() / TEXT("Extraction/Data/WeaponMechanics.json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("Weapon mechanics snapshot missing or invalid; legacy weapons retained"));
			return nullptr;
		}
		for (const auto& Entry : Root->GetObjectField(TEXT("profiles"))->Values)
		{
			const auto P = Entry.Value->AsObject();
			const auto B = P->GetObjectField(TEXT("base")), A = P->GetObjectField(TEXT("aim"));
			const auto Bullet = P->GetObjectField(TEXT("bullet")), Breath = P->GetObjectField(TEXT("breath"));
			FResolvedWeaponStats S;
			S.Definition = FName(Entry.Key);
			S.Revision = HashCombine(Root->GetIntegerField(TEXT("revision")), 2u);
			S.MagazineCapacity = Number(B, TEXT("WeaponClipAmmoCount"));
			S.Interval = Number(B, TEXT("FireInterval"));
			S.Reload = Number(B, TEXT("ChangeClipTime"));
			S.EmptyReload = Number(B, TEXT("ChangeClipTimeWhenEmpty"));
			S.Equip = Number(B, TEXT("EquipTime"));
			S.Unequip = Number(B, TEXT("UnEquipTime"));
			S.SprintToFire = Number(B, TEXT("SprintToFireSpeed"));
			S.ReturnSprintInterval = FMath::Max(0.f, Number(B, TEXT("ReturnSprintInterval")));
			S.AimIn = Number(A, TEXT("AimingOnAnimTime"));
			S.AimOut = Number(A, TEXT("AimingOffAnimTime"));
			S.Speed = Number(Bullet, TEXT("InitSpeedFor1P"));
			S.Gravity = Number(Bullet, TEXT("GravityParam")) * 100;
			S.GravityFreeDistance = Number(Bullet, TEXT("ZeroGravityDistance"));
			S.MaxDistance = Number(Bullet, TEXT("MaxDistance"));
			S.Radius = Number(Bullet, TEXT("Radius"));
			S.RangeStart = Number(Bullet, TEXT("ValidDistance"));
			S.FalloffDistances = Numbers(Bullet, TEXT("AttenDistances"));
			S.FalloffRates = Numbers(Bullet, TEXT("AttenRates"));
			const auto Stance = P->GetObjectField(TEXT("stance"));
			S.MoveSpread = Number(Stance, TEXT("SpreadScaleByWalkOrRun"));
			S.CrouchSpread = Number(Stance, TEXT("SpreadScaleByCrouch"));
			S.JumpSpread = Number(Stance, TEXT("SpreadScaleByJump"));
			S.CrouchRecoil = FVector2D(1 + Number(Stance, TEXT("RecoilVerticalFactorByCrouch")),
			                           1 + Number(Stance, TEXT("RecoilHorizontalFactorByCrouch")));
			S.StandBreath = Number(Breath, TEXT("StandBreathFactor"));
			S.CrouchBreath = Number(Breath, TEXT("CrouchBreathFactor"));
			const auto BC = Breath->GetObjectField(TEXT("WeaponBreathConfig"));
			S.BreathDrain = Number(BC, TEXT("AttenSpeed"));
			S.BreathShot = Number(BC, TEXT("AttenValueOnFire"));
			S.BreathRecovery = Number(BC, TEXT("RecoverSpeed"));
			S.BreathCooldown = Number(BC, TEXT("HoldBreathCD"));
			for (int32 Mode = 0; Mode != 2; ++Mode)
			{
				const TCHAR* Key = Mode ? TEXT("ads") : TEXT("hip");
				const auto R = P->GetObjectField(TEXT("recoil"))->GetObjectField(Key);
				S.Single[Mode] = Sequence(R->GetObjectField(TEXT("SingleOrBurstShootRecoil")));
				S.Continuous[Mode] = Sequence(R->GetObjectField(TEXT("ContinueShootRecoil")));
				S.Loop[Mode] = Sequence(R->GetObjectField(TEXT("ContinueShootRecoilLoop")));
				const auto D = P->GetObjectField(TEXT("spread"))->GetObjectField(Key);
				auto& Spread = S.Spread[Mode];
				const auto Area = Numbers(D, TEXT("AreaSize"));
				Spread.Base = FVector2D(Area[0], Area[1]);
				Spread.Maximum = XY(D->GetObjectField(TEXT("SpreadMax")));
				Spread.Recovery = XY(D->GetObjectField(TEXT("FireSpreadRecoverSpeed")));
				for (const auto& V : D->GetArrayField(TEXT("FireSpreadItems")))
				{
					const auto F = V->AsObject();
					// Field schema is audited by the importer; unknown items stay inactive.
					const TSharedPtr<FJsonObject>* Amount = nullptr;
					if (F->TryGetObjectField(TEXT("Spread"), Amount)) Spread.PerShot += XY(*Amount);
				}
			}
			S.Sources.Add(TEXT("PC source base rows; preset/score composition pending verification"));
			S.Sources.Add(TEXT(
				"Compatibility: recoil raw * 0.1 degrees; destination-step falloff; existing ammo/armor damage"));
			Profiles.Add(S.Definition, MoveTemp(S));
		}
	}
	return Profiles.Find(Definition);
}

FResolvedWeaponStats WeaponMechanics::ResolveWeaponStats(const FExtractionItem& Item)
{
	const auto* Base = Find(Item.Definition);
	if (!Base) return {};
	FResolvedWeaponStats Result = *Base;
	TArray<FName> Parts;
	for (const auto& Part : Item.Attachments) Parts.Add(Part.Definition);
	Parts.Sort(FNameLexicalLess());
	for (const FName Part : Parts)
		if (const auto* D = UExtractionItemDefinition::Get(Part))
		{
			Result.SpreadScale *= D->ScatterScale;
			Result.Sources.Add(Part.ToString() + TEXT(": legacy spread multiplier"));
		}
	struct FOperation
	{
		FWeaponAttributeModifier Modifier;
		FName Source;
	};
	TArray<FOperation> Operations;
	for (const auto& Part : Item.Attachments)
		if (const auto* D = UExtractionItemDefinition::Get(Part.Definition))
		{
			for (const auto& M : D->WeaponModifiers) Operations.Add({M, Part.Definition});
			for (const auto& Tune : D->Tunes)
			{
				if (!Tune.Curve) continue;
				const auto* Value = Part.TuneValues.FindByPredicate([&](const auto& V) { return V.Key == Tune.Id; });
				FWeaponAttributeModifier M;
				M.Attribute = Tune.Attribute;
				M.Operation = Tune.Operation;
				M.Priority = 100;
				M.Value = Tune.Curve->GetFloatValue(Value ? Value->Value : Tune.Default);
				Operations.Add({M, Part.Definition});
			}
		}
	Operations.Sort([](const FOperation& A, const FOperation& B)
	{
		if (A.Modifier.Priority != B.Modifier.Priority) return A.Modifier.Priority < B.Modifier.Priority;
		if (A.Source != B.Source) return A.Source.LexicalLess(B.Source);
		if (A.Modifier.Attribute != B.Modifier.Attribute) return A.Modifier.Attribute.LexicalLess(B.Modifier.Attribute);
		if (A.Modifier.Operation != B.Modifier.Operation) return A.Modifier.Operation < B.Modifier.Operation;
		return A.Modifier.Value < B.Modifier.Value;
	});
	TMap<FName, float*> Attributes = {
		{TEXT("MagazineCapacity"), &Result.MagazineCapacity},
		{TEXT("Interval"), &Result.Interval}, {TEXT("AimIn"), &Result.AimIn}, {TEXT("AimOut"), &Result.AimOut},
		{TEXT("SprintToFire"), &Result.SprintToFire}, {TEXT("Reload"), &Result.Reload},
		{TEXT("EmptyReload"), &Result.EmptyReload},
		{TEXT("Speed"), &Result.Speed}, {TEXT("RangeStart"), &Result.RangeStart},
		{TEXT("RecoilScale"), &Result.RecoilScale},
		{TEXT("SpreadScale"), &Result.SpreadScale}, {TEXT("StandBreath"), &Result.StandBreath},
		{TEXT("CrouchBreath"), &Result.CrouchBreath}
	};
	for (const auto& Op : Operations)
	{
		float** Target = Attributes.Find(Op.Modifier.Attribute);
		if (!Target || !FMath::IsFinite(Op.Modifier.Value))
		{
			Result.Revision = 0;
			return Result;
		}
		float& Value = **Target;
		switch (Op.Modifier.Operation)
		{
		case EWeaponModifierOperation::Add: Value += Op.Modifier.Value;
			break;
		case EWeaponModifierOperation::Multiply: Value *= Op.Modifier.Value;
			break;
		case EWeaponModifierOperation::Replace: Value = Op.Modifier.Value;
			break;
		}
		Result.Sources.Add(FString::Printf(TEXT("%s:%s=%g"), *Op.Source.ToString(), *Op.Modifier.Attribute.ToString(),
		                                   Value));
	}
	for (const auto& Attribute : Attributes)
		if (!FMath::IsFinite(*Attribute.Value) || *Attribute.Value <= 0)
		{
			Result.Revision = 0;
			return Result;
		}
	if (Result.MagazineCapacity > 200 || !FMath::IsNearlyEqual(Result.MagazineCapacity,
	                                                           FMath::RoundToFloat(Result.MagazineCapacity)))
	{
		Result.Revision = 0;
		return Result;
	}
	if (!Parts.IsEmpty())
	{
		TArray<FName> Keys;
		Attributes.GetKeys(Keys);
		Keys.Sort(FNameLexicalLess());
		for (FName Key : Keys)
			Result.Revision = HashCombine(Result.Revision,
			                              FCrc::StrCrc32(*FString::Printf(
				                              TEXT("%s:%g"), *Key.ToString(), *Attributes[Key])));
		for (FName Part : Parts) Result.Revision = HashCombine(Result.Revision, FCrc::StrCrc32(*Part.ToString()));
	}

	return Result;
}

float FResolvedWeaponStats::Falloff(float Distance) const
{
	// Explicit temporary policy: destination value applies after entering each segment.
	if (Distance <= RangeStart) return 1;
	for (int32 I = 0; I < FMath::Min(FalloffDistances.Num(), FalloffRates.Num()); ++I)
		if (Distance <= FalloffDistances[I]) return FalloffRates[I];
	return FalloffRates.IsEmpty() ? 1 : FalloffRates.Last();
}

double WeaponMechanics::CompensatedDebt(double Debt, double Input)
{
	return Debt * Input < 0 ? FMath::Sign(Debt) * FMath::Max(0., FMath::Abs(Debt) - FMath::Abs(Input)) : Debt;
}

FVector2D FResolvedWeaponStats::Recoil(uint32 Index, bool bContinuous, float ADS, uint32 Seed) const
{
	FRandomStream Random(Seed ^ 0x51ED270B);
	const float PitchNoise = Random.FRand(), YawNoise = Random.FRand();
	auto Evaluate = [&](int32 Mode)
	{
		const auto* S = bContinuous ? &Continuous[Mode] : &Single[Mode];
		uint32 Local = Index;
		if (bContinuous && Index >= uint32(S->Pitch.Num()))
		{
			Local -= S->Pitch.Num();
			S = &Loop[Mode];
		}
		const float P = S->Pitch.IsEmpty() ? 0 : S->Pitch[Local % S->Pitch.Num()];
		const float Y = S->Yaw.IsEmpty() ? 0 : S->Yaw[Local % S->Yaw.Num()];
		return FVector2D((P + FMath::Lerp(S->PitchRandom.X, S->PitchRandom.Y, PitchNoise)) * S->PitchScale,
		                 (Y + FMath::Lerp(S->YawRandom.X, S->YawRandom.Y, YawNoise)) * S->YawScale);
	};
	return FMath::Lerp(Evaluate(0), Evaluate(1), ADS) * SourceRecoilToDegrees * RecoilScale;
}

FVector WeaponMechanics::SpreadDirection(const FVector& Direction, FVector2D Degrees, uint32 Seed)
{
	FRandomStream Random(Seed ^ 0x68BC21EB);
	const float Angle = Random.FRand() * 2 * PI, Radius = FMath::Sqrt(Random.FRand());
	const FRotationMatrix Basis(Direction.Rotation());
	return (Direction.GetSafeNormal() + Basis.GetUnitAxis(EAxis::Y) * FMath::Tan(
				FMath::DegreesToRadians(Degrees.X * Radius * FMath::Cos(Angle)))
			+ Basis.GetUnitAxis(EAxis::Z) * FMath::Tan(FMath::DegreesToRadians(Degrees.Y * Radius * FMath::Sin(Angle))))
		.
		GetSafeNormal();
}
