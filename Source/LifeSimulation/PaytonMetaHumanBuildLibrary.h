#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PaytonMetaHumanBuildLibrary.generated.h"

UCLASS()
class LIFESIMULATION_API UPaytonMetaHumanBuildLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** UE5.8の正式なMetaHuman Character AssemblyでPayton_Custom2をゲーム用に生成する。 */
	UFUNCTION(BlueprintCallable, Category = "LifeSimulation|MetaHuman")
	static bool BuildPaytonCustom2Character();
};
