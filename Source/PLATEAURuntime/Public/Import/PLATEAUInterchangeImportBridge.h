// Copyright 2023 Ministry of Land, Infrastructure and Transport
// Copyright 2026 6F978E

#pragma once

#include "CoreMinimal.h"
#include "Import/PLATEAUImportMaterialLedger.h"

class APLATEAUInstancedCityModel;
class USceneComponent;

struct FPLATEAUInterchangeGmlJob
{
    TWeakObjectPtr<APLATEAUInstancedCityModel> ModelActor;
    TWeakObjectPtr<USceneComponent> GmlRoot;
    FString GmlBaseName;
    FString DatasetName;
    FString GlbPath;
    TArray<FPLATEAUMaterialKey> MaterialKeys;
    TSharedPtr<FPLATEAUImportMaterialLedger, ESPMode::ThreadSafe> Ledger;
    TFunction<void(bool /*bSuccess*/)> OnComplete;
};

DECLARE_DELEGATE_OneParam(FPLATEAUBeginInterchangeGmlImport, FPLATEAUInterchangeGmlJob);

PLATEAURUNTIME_API FPLATEAUBeginInterchangeGmlImport& GetPLATEAUInterchangeGmlImporter();
PLATEAURUNTIME_API bool ShouldUsePLATEAUInterchangeImport();
PLATEAURUNTIME_API bool ShouldDeferPLATEAUMaterials();
PLATEAURUNTIME_API int32 GetPLATEAUInterchangeMaxInFlight();
