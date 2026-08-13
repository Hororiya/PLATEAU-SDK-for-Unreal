// Copyright 2023 Ministry of Land, Infrastructure and Transport
// Copyright 2026 6F978E

#include "Import/PLATEAUImportMaterialLedger.h"

#include "PLATEAUTextureLoader.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/Package.h"

#include "plateau/polygon_mesh/model.h"
#include "plateau/polygon_mesh/mesh.h"
#include "plateau/polygon_mesh/sub_mesh.h"
#include "citygml/material.h"

DEFINE_LOG_CATEGORY_STATIC(LogPLATEAUImport, Log, All);

void FPLATEAUImportMaterialLedger::CollectKeysFromModel(
    const plateau::polygonMesh::Model& Model,
    TArray<FPLATEAUMaterialKey>& OutKeys)
{
    OutKeys.Reset();
    for (plateau::polygonMesh::Mesh* Mesh : Model.getAllMeshes())
    {
        if (Mesh == nullptr)
        {
            continue;
        }
        for (const plateau::polygonMesh::SubMesh& Sub : Mesh->getSubMeshes())
        {
            FPLATEAUMaterialKey Key;
            Key.TexturePath = UTF8_TO_TCHAR(Sub.getTexturePath().c_str());
            Key.SlotName = Key.TexturePath.IsEmpty()
                ? FString(TEXT("DefaultMaterial"))
                : FPaths::GetBaseFilename(Key.TexturePath);

            if (const std::shared_ptr<const citygml::Material> GmlMat = Sub.getMaterial())
            {
                Key.bHasGmlMaterial = true;
                const auto Dif = GmlMat->getDiffuse();
                const auto Emi = GmlMat->getEmissive();
                Key.BaseColor = FLinearColor(Dif.x, Dif.y, Dif.z);
                Key.Emissive = FLinearColor(Emi.x, Emi.y, Emi.z);
                Key.Shininess = GmlMat->getShininess();
                Key.Transparency = GmlMat->getTransparency();
                Key.Ambient = GmlMat->getAmbientIntensity();
            }

            OutKeys.Add(Key);
        }
    }
}

void FPLATEAUImportMaterialLedger::RecordMesh(UStaticMesh* Mesh, const TArray<FPLATEAUMaterialKey>& KeysInSlotOrder)
{
    if (Mesh == nullptr)
    {
        return;
    }
    FScopeLock Lock(&Mutex);
    FPendingMesh Pending;
    Pending.Mesh = Mesh;
    Pending.Keys = KeysInSlotOrder;
    PendingMeshes.Add(MoveTemp(Pending));
}

void FPLATEAUImportMaterialLedger::RecordComponent(UStaticMeshComponent* Component)
{
    if (Component == nullptr)
    {
        return;
    }
    FScopeLock Lock(&Mutex);
    PendingComponents.Add(Component);
}

int32 FPLATEAUImportMaterialLedger::NumUniqueKeys() const
{
    TSet<FPLATEAUMaterialKey> Unique;
    FScopeLock Lock(&Mutex);
    for (const FPendingMesh& Pending : PendingMeshes)
    {
        Unique.Append(Pending.Keys);
    }
    return Unique.Num();
}

int32 FPLATEAUImportMaterialLedger::NumPendingMeshes() const
{
    FScopeLock Lock(&Mutex);
    return PendingMeshes.Num();
}

FString FPLATEAUImportMaterialLedger::MakeAssetName(const FPLATEAUMaterialKey& Key)
{
    const FString Hash = FString::Printf(TEXT("%08x"), GetTypeHash(Key));
    const FString SafeSlot = FPaths::MakeValidFileName(Key.SlotName).Replace(TEXT("."), TEXT("_"));
    return FString::Printf(TEXT("M_PLATEAU_%s_%s"), *SafeSlot, *Hash);
}

UMaterialInterface* FPLATEAUImportMaterialLedger::GetOrCreateMaterial(const FPLATEAUMaterialKey& Key)
{
    if (TObjectPtr<UMaterialInterface>* Found = UniqueMaterials.Find(Key))
    {
        return Found->Get();
    }

    const TCHAR* ParentPath = Key.Transparency > 0.f
        ? TEXT("/PLATEAU-SDK-for-Unreal/Materials/PLATEAUX3DMaterial_Transparent")
        : (Key.bHasGmlMaterial
            ? TEXT("/PLATEAU-SDK-for-Unreal/Materials/PLATEAUX3DMaterial")
            : TEXT("/PLATEAU-SDK-for-Unreal/Materials/DefaultMaterial"));

    UMaterial* Parent = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, ParentPath));
    if (Parent == nullptr)
    {
        UE_LOG(LogPLATEAUImport, Warning, TEXT("Missing parent material %s"), ParentPath);
        return nullptr;
    }

    const FString AssetName = MakeAssetName(Key);
    const FString PackagePath = TEXT("/Game/PLATEAU/Materials/") + AssetName;
    UPackage* Package = CreatePackage(*PackagePath);
    Package->FullyLoad();

    UMaterialInstanceDynamic* Dyn = UMaterialInstanceDynamic::Create(Parent, Package, *AssetName);
    Dyn->SetFlags(RF_Public | RF_Standalone);

    if (Key.bHasGmlMaterial)
    {
        Dyn->SetVectorParameterValue(TEXT("BaseColor"), Key.BaseColor);
        Dyn->SetVectorParameterValue(TEXT("EmissiveColor"), Key.Emissive);
        Dyn->SetScalarParameterValue(TEXT("Ambient"), Key.Ambient);
        Dyn->SetScalarParameterValue(TEXT("Shininess"), Key.Shininess);
        Dyn->SetScalarParameterValue(TEXT("Transparency"), Key.Transparency);
    }

    if (!Key.TexturePath.IsEmpty())
    {
        if (UTexture2D* Texture = FPLATEAUTextureLoader::Load(Key.TexturePath, false))
        {
            Dyn->SetTextureParameterValue(TEXT("Texture"), Texture);
        }
    }

    UniqueMaterials.Add(Key, Dyn);
    return Dyn;
}

void FPLATEAUImportMaterialLedger::CreateAndApply()
{
    TArray<FPendingMesh> MeshesCopy;
    TArray<TWeakObjectPtr<UStaticMeshComponent>> ComponentsCopy;
    {
        FScopeLock Lock(&Mutex);
        MeshesCopy = PendingMeshes;
        ComponentsCopy = PendingComponents;
    }

    TSet<FPLATEAUMaterialKey> AllKeys;
    for (const FPendingMesh& Pending : MeshesCopy)
    {
        AllKeys.Append(Pending.Keys);
    }

    for (const FPLATEAUMaterialKey& Key : AllKeys)
    {
        GetOrCreateMaterial(Key);
    }

    auto FindMaterial = [this](const FPLATEAUMaterialKey& Key) -> UMaterialInterface*
    {
        if (TObjectPtr<UMaterialInterface>* Found = UniqueMaterials.Find(Key))
        {
            return Found->Get();
        }
        return nullptr;
    };

    int32 AssignedSlots = 0;
    for (const FPendingMesh& Pending : MeshesCopy)
    {
        UStaticMesh* Mesh = Pending.Mesh.Get();
        if (Mesh == nullptr)
        {
            continue;
        }

        const TArray<FStaticMaterial>& StaticMats = Mesh->GetStaticMaterials();
        for (int32 Index = 0; Index < StaticMats.Num(); ++Index)
        {
            UMaterialInterface* Mat = nullptr;
            const FString Slot = StaticMats[Index].MaterialSlotName.ToString();
            for (const FPLATEAUMaterialKey& Key : Pending.Keys)
            {
                if (Key.SlotName == Slot)
                {
                    Mat = FindMaterial(Key);
                    break;
                }
            }
            if (Mat == nullptr && Pending.Keys.IsValidIndex(Index))
            {
                Mat = FindMaterial(Pending.Keys[Index]);
            }
            if (Mat == nullptr && Pending.Keys.Num() > 0)
            {
                Mat = FindMaterial(Pending.Keys[0]);
            }
            if (Mat != nullptr)
            {
                Mesh->SetMaterial(Index, Mat);
                ++AssignedSlots;
            }
        }
    }

    for (const TWeakObjectPtr<UStaticMeshComponent>& WeakComp : ComponentsCopy)
    {
        UStaticMeshComponent* Comp = WeakComp.Get();
        if (Comp == nullptr || Comp->GetStaticMesh() == nullptr)
        {
            continue;
        }
        UStaticMesh* Mesh = Comp->GetStaticMesh();
        for (int32 Index = 0; Index < Mesh->GetStaticMaterials().Num(); ++Index)
        {
            Comp->SetMaterial(Index, Mesh->GetMaterial(Index));
        }
    }

    UE_LOG(LogPLATEAUImport, Display,
        TEXT("Deferred materials: %d unique keys, %d meshes, %d slots assigned"),
        UniqueMaterials.Num(), MeshesCopy.Num(), AssignedSlots);
}

void FPLATEAUImportMaterialLedger::Reset()
{
    FScopeLock Lock(&Mutex);
    PendingMeshes.Reset();
    PendingComponents.Reset();
    UniqueMaterials.Reset();
}
