// Kodo Tag: shared tint helper. The engine basic-shape meshes' default material does not
// reliably expose a settable "Color", so to tint anything we build a dynamic material from
// the known-tintable engine material and set several common parameter names.

#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace KodoTint
{
	/** Assign a dynamic instance of the engine tintable material to slot 0, set to Color. */
	inline void Apply(UPrimitiveComponent* Component, const FLinearColor& Color)
	{
		if (!Component)
		{
			return;
		}
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		if (!Base)
		{
			Base = Component->GetMaterial(0);
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Component);
		if (!MID)
		{
			return;
		}
		MID->SetVectorParameterValue(FName("Color"), Color);
		MID->SetVectorParameterValue(FName("BaseColor"), Color);
		MID->SetVectorParameterValue(FName("Tint"), Color);
		Component->SetMaterial(0, MID);
	}
}
