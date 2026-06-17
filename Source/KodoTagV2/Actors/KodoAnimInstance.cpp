// Kodo Tag: Survivor — UE Migration, Phase 5.

#include "Actors/KodoAnimInstance.h"
#include "Actors/KodoTagCharacterBase.h"

void UKodoAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (const AKodoTagCharacterBase* Character = Cast<AKodoTagCharacterBase>(TryGetPawnOwner()))
	{
		Speed = Character->GetAnimSpeed();
		bIsMoving = Speed > 1.f;
		bIsAttacking = Character->IsAnimAttacking();
		bIsDead = Character->IsAnimDead();
	}
}
