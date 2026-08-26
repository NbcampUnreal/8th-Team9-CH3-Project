// Fill out your copyright notice in the Description page of Project Settings.


#include "BattleLogic/Attachment/RangedAttachmentComponent.h"
#include "BattleLogic/Attachment/AttachmentDataAsset.h"
#include "BattleLogic/Weapon/RangedWeaponBase.h"
#include "BattleLogic/Weapon/DataAssets/RangedWeaponDataAsset.h"

URangedAttachmentComponent::URangedAttachmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	OwnerWeapon = nullptr;
	WeaponData = nullptr;
	WeaponMesh = nullptr;

}


void URangedAttachmentComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()) return;
	OwnerWeapon = Cast<ARangedWeaponBase>(GetOwner());

	if (!OwnerWeapon || !OwnerWeapon->ItemData) return;
	WeaponData = Cast<URangedWeaponDataAsset>(OwnerWeapon->ItemData);
	AttachmentSlots = OwnerWeapon->AttachmentSlots;

	if (!OwnerWeapon->SkeletalMesh) return;
	WeaponMesh = OwnerWeapon->SkeletalMesh;

	for (uint8 SlotIndex = 0; SlotIndex < static_cast<uint8>(EAttachmentSlot::None); ++SlotIndex)
	{
		EAttachmentSlot CurrentSlot = static_cast<EAttachmentSlot>(SlotIndex);

		UStaticMeshComponent* DynamicSlotComp = NewObject<UStaticMeshComponent>(OwnerWeapon);
		if (DynamicSlotComp)
		{
			DynamicSlotComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			DynamicSlotComp->SetStaticMesh(nullptr); // 초기 상태는 외형 없음
			DynamicSlotComp->SetVisibility(false);   // 기본 숨김 처리

			DynamicSlotComp->RegisterComponent();
			AttachmentMeshComponents.Add(CurrentSlot, DynamicSlotComp);
		}
	}
}

void URangedAttachmentComponent::RefreshWeaponStats()
{
	if (!OwnerWeapon || !WeaponData) return;

	// 무기의 기본 스탯을 에셋에서 가져옴
	float BaseSpreadPerShot = WeaponData->SpreadPerShot;
	float BaseRecoverySpreadSpeed = WeaponData->RecoverySpreadSpeed;
	float BaseMaxSpreadAngle = WeaponData->MaxSpreadAngle;
	int32 BaseMaxAmmo = WeaponData->MaxAmmo;
	float BaseRecoilAmount = WeaponData->RecoilAmount;

	// 현재 장착된 부착물로 스탯을 갱신
	for (const TPair<EAttachmentSlot, UAttachmentDataAsset*>& AttachmentPair : CurrentAttachments)
	{
		UAttachmentDataAsset* AttachmentData = AttachmentPair.Value;
		if (AttachmentData)
		{
			BaseSpreadPerShot *= AttachmentData->SpreadPerShotModifier;
			BaseRecoverySpreadSpeed *= AttachmentData->RecoverySpreadMultiplier;
			BaseMaxSpreadAngle *= AttachmentData->MaxSpreadMultiplier;
			BaseMaxAmmo += AttachmentData->MaxAmmoBonus;
			BaseRecoilAmount *= AttachmentData->VerticalRecoilModifier;
		}
	}

	// 갱신된 스탯을 무기에 적용
	OwnerWeapon->SpreadPerShot = BaseSpreadPerShot;
	OwnerWeapon->RecoverySpreadSpeed = BaseRecoverySpreadSpeed;
	OwnerWeapon->MaxSpreadAngle = BaseMaxSpreadAngle;
	OwnerWeapon->MaxAmmo = BaseMaxAmmo;
	OwnerWeapon->RecoilAmount = BaseRecoilAmount;
}

bool URangedAttachmentComponent::IsCanEquipAttachment(UAttachmentDataAsset* AttachmentData) const
{
	if (!OwnerWeapon || !WeaponData || !AttachmentData) return false;

	// 무기 타입이 일치하는지 확인
	if (!AttachmentData->WeaponTypes.Contains(WeaponData->WeaponType))
	{
		UE_LOG(LogTemp, Warning, TEXT("Attachment type does not match weapon type!"));
		return false;
	}

	// 해당 부착물 타입이 이 무기에서 지원되는지 확인
	if(!AttachmentSlots.Contains(AttachmentData->AttachmentSlot))
	{
		UE_LOG(LogTemp, Warning, TEXT("Weapon does not have the required socket for this attachment!"));
		return false;
	}

	return true; // 모든 조건을 만족하면 장착 가능
}

bool URangedAttachmentComponent::EquipAttachment(UAttachmentDataAsset* AttachmentData)
{
	if (!IsCanEquipAttachment(AttachmentData)) return false;

	// 부착물이 이미 있는 경우는 고려하지 않습니다.
	// 부착물을 장착하기 전, 무조건 슬롯을 비워두도록 구현했습니다.

	EAttachmentSlot CurrentSlot = AttachmentData->AttachmentSlot;
	CurrentAttachments.Add(CurrentSlot, AttachmentData);	// 새로운 부착물 장착
	UpdateAttachmentMesh(CurrentSlot);						// 부착물 메쉬 업데이트
	RefreshWeaponStats();									// 장착 후 무기 스탯 갱신

	OnAttachmentChanged.Broadcast(this);					// 부착물 변경 후 델리게이트 브로드캐스트
	return true;
}

UAttachmentDataAsset* URangedAttachmentComponent::UnequipAttachment(EAttachmentSlot AttachmentSlot)
{
	if (!OwnerWeapon || !WeaponData) return nullptr;

	if (!CurrentAttachments.Contains(AttachmentSlot))
	{
		UE_LOG(LogTemp, Warning, TEXT("No attachment to unequip in this slot!"));
		return nullptr;
	}

	UAttachmentDataAsset* RemovedAttachment = CurrentAttachments[AttachmentSlot];
	CurrentAttachments.Remove(AttachmentSlot);	// 부착물 해제
	UpdateAttachmentMesh(AttachmentSlot);		// 부착물 메쉬 업데이트
	RefreshWeaponStats();						// 해제 후 무기 스탯 갱신

	OnAttachmentChanged.Broadcast(this);		// 부착물 변경 후 델리게이트 브로드캐스트
	return RemovedAttachment;
}

void URangedAttachmentComponent::UpdateAttachmentMesh(EAttachmentSlot AttachmentSlot)
{
	if (!OwnerWeapon || !WeaponMesh) return;

    // 현재 장착된 부착물 메쉬 추출
	UStaticMeshComponent* TargetMeshComp = AttachmentMeshComponents.FindRef(AttachmentSlot);
	if (!TargetMeshComp) return;

	TargetMeshComp->SetStaticMesh(nullptr);
	TargetMeshComp->SetVisibility(false);

	// 현재 장착된 부착물 데이터 추출
	UAttachmentDataAsset* AttachmentData = CurrentAttachments.FindRef(AttachmentSlot);
	if (!AttachmentData || !AttachmentData->ItemMesh) return;
	
	// 부착물이 장착될 소켓이 무기에 존재하는지 확인
	if (!WeaponMesh->DoesSocketExist(AttachmentData->SocketName)) return;

	TargetMeshComp->SetStaticMesh(AttachmentData->ItemMesh);
	TargetMeshComp->AttachToComponent(
		WeaponMesh,
		FAttachmentTransformRules::KeepRelativeTransform,
		AttachmentData->SocketName
	);
	TargetMeshComp->SetVisibility(true);
}

TMap<EAttachmentSlot, UAttachmentDataAsset*>& URangedAttachmentComponent::GetCurrentAttachments() const
{
	return const_cast<TMap<EAttachmentSlot, UAttachmentDataAsset*>&>(CurrentAttachments);
}

bool URangedAttachmentComponent::IsSupportAttachment(EAttachmentSlot AttachmentSlot) const
{
	if (!WeaponData) return false;
	return AttachmentSlots.Contains(AttachmentSlot);
}