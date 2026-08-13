// Copyright 2023 Ministry of Land, Infrastructure and Transport
// Copyright 2026 6F978E

#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialInterface.h"

namespace plateau::polygonMesh {
    class Model;
}

class UStaticMesh;
class UStaticMeshComponent;

/** Appearance key used to share one material across all imported meshes. */
struct FPLATEAUMaterialKey
{
    FString TexturePath;
    FString SlotName;
    FLinearColor BaseColor = FLinearColor::White;
    FLinearColor Emissive = FLinearColor::Black;
    float Shininess = 0.f;
    float Transparency = 0.f;
    float Ambient = 1.f;
    bool bHasGmlMaterial = false;

    bool operator==(const FPLATEAUMaterialKey& Other) const
    {
        return SlotName == Other.SlotName
            && TexturePath == Other.TexturePath
            && bHasGmlMaterial == Other.bHasGmlMaterial
            && BaseColor.Equals(Other.BaseColor, 0.001f)
            && Transparency == Other.Transparency;
    }

    friend uint32 GetTypeHash(const FPLATEAUMaterialKey& Value)
    {
        return HashCombine(GetTypeHash(Value.SlotName), GetTypeHash(Value.TexturePath));
    }
};

/**
 * Records unique material keys while geometry is imported, then creates each
 * material once and assigns it to every waiting mesh / component.
 */
class PLATEAURUNTIME_API FPLATEAUImportMaterialLedger
{
public:
    static void CollectKeysFromModel(const plateau::polygonMesh::Model& Model, TArray<FPLATEAUMaterialKey>& OutKeys);

    void RecordMesh(UStaticMesh* Mesh, const TArray<FPLATEAUMaterialKey>& KeysInSlotOrder);
    void RecordComponent(UStaticMeshComponent* Component);

    int32 NumUniqueKeys() const;
    int32 NumPendingMeshes() const;

    /** Game-thread: create unique materials, then bind them. */
    void CreateAndApply();

    void Reset();

private:
    struct FPendingMesh
    {
        TWeakObjectPtr<UStaticMesh> Mesh;
        TArray<FPLATEAUMaterialKey> Keys;
    };

    mutable FCriticalSection Mutex;
    TArray<FPendingMesh> PendingMeshes;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> PendingComponents;
    TMap<FPLATEAUMaterialKey, TObjectPtr<UMaterialInterface>> UniqueMaterials;

    UMaterialInterface* GetOrCreateMaterial(const FPLATEAUMaterialKey& Key);
    static FString MakeAssetName(const FPLATEAUMaterialKey& Key);
};
