// Copyright 2023 Ministry of Land, Infrastructure and Transport
// Copyright 2026 6F978E

#include "Import/PLATEAUInterchangeMeshImporter.h"

#include "Import/PLATEAUInterchangeImportBridge.h"
#include "PLATEAUInstancedCityModel.h"
#include "Component/PLATEAUStaticMeshComponent.h"

#include "Async/Async.h"
#include "Engine/StaticMesh.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeGenericTexturePipeline.h"
#include "InterchangeManager.h"
#include "InterchangeMeshDefinitions.h"
#include "InterchangeSourceData.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogPLATEAUInterchange, Log, All);

namespace
{
    FCriticalSection GGateCS;
    FEvent* GGateEvent = nullptr;
    int32 GInFlight = 0;

    void EnsureGate()
    {
        if (GGateEvent == nullptr)
        {
            GGateEvent = FPlatformProcess::GetSynchEventFromPool(false);
        }
    }
}

void FPLATEAUInterchangeMeshImporter::Register()
{
    EnsureGate();
    GetPLATEAUInterchangeGmlImporter().BindStatic(&FPLATEAUInterchangeMeshImporter::BeginJob);
}

void FPLATEAUInterchangeMeshImporter::Unregister()
{
    GetPLATEAUInterchangeGmlImporter().Unbind();
}

void FPLATEAUInterchangeMeshImporter::AcquireSlot()
{
    EnsureGate();
    const int32 MaxInFlight = GetPLATEAUInterchangeMaxInFlight();
    for (;;)
    {
        {
            FScopeLock Lock(&GGateCS);
            if (GInFlight < MaxInFlight)
            {
                ++GInFlight;
                return;
            }
        }
        GGateEvent->Wait();
    }
}

void FPLATEAUInterchangeMeshImporter::ReleaseSlot()
{
    {
        FScopeLock Lock(&GGateCS);
        GInFlight = FMath::Max(0, GInFlight - 1);
    }
    if (GGateEvent)
    {
        GGateEvent->Trigger();
    }
}

void FPLATEAUInterchangeMeshImporter::BeginJob(FPLATEAUInterchangeGmlJob Job)
{
    AcquireSlot();
    if (IsInGameThread())
    {
        ImportOnGameThread(MoveTemp(Job));
        return;
    }

    FPLATEAUInterchangeGmlJob JobCopy = MoveTemp(Job);
    AsyncTask(ENamedThreads::GameThread, [JobCopy]() mutable
    {
        ImportOnGameThread(MoveTemp(JobCopy));
    });
}

void FPLATEAUInterchangeMeshImporter::ImportOnGameThread(FPLATEAUInterchangeGmlJob Job)
{
    auto Finish = [Job](bool bSuccess) mutable
    {
        ReleaseSlot();
        if (Job.OnComplete)
        {
            Job.OnComplete(bSuccess);
        }
    };

    if (!FPaths::FileExists(Job.GlbPath))
    {
        UE_LOG(LogPLATEAUInterchange, Error, TEXT("Missing staged glTF: %s"), *Job.GlbPath);
        Finish(false);
        return;
    }

    UInterchangeManager& Manager = UInterchangeManager::GetInterchangeManager();
    UInterchangeSourceData* SourceData = UInterchangeManager::CreateSourceData(Job.GlbPath);
    if (SourceData == nullptr || !Manager.CanTranslateSourceData(SourceData))
    {
        UE_LOG(LogPLATEAUInterchange, Error, TEXT("Interchange cannot translate %s"), *Job.GlbPath);
        Finish(false);
        return;
    }

    UInterchangeGenericAssetsPipeline* Pipeline = NewObject<UInterchangeGenericAssetsPipeline>(GetTransientPackage());
    Pipeline->AddToRoot();
    Pipeline->bUseSourceNameForAsset = true;
    Pipeline->AssetName = Job.GmlBaseName;
    Pipeline->ImportOffsetUniformScale = 1.f;

    if (Pipeline->MeshPipeline)
    {
        Pipeline->MeshPipeline->bImportStaticMeshes = true;
        Pipeline->MeshPipeline->bImportSkeletalMeshes = false;
        Pipeline->MeshPipeline->CombineStaticMeshesBehavior = EInterchangeCombineStaticMeshesBehavior::DoNotCombine;
        Pipeline->MeshPipeline->bBuildNanite = true;
        Pipeline->MeshPipeline->NaniteTriangleThreshold = 0;
        Pipeline->MeshPipeline->bCollision = false;
        Pipeline->MeshPipeline->Collision = EInterchangeMeshCollision::None;
        Pipeline->MeshPipeline->bGenerateLightmapUVs = false;
        Pipeline->MeshPipeline->bBuildReversedIndexBuffer = false;
    }

    if (Pipeline->AnimationPipeline)
    {
        Pipeline->AnimationPipeline->bImportAnimations = false;
    }

    if (Pipeline->MaterialPipeline)
    {
        // Geometry first. Unique PLATEAU materials are created once at the end of the import.
        Pipeline->MaterialPipeline->bImportMaterials = false;
        if (Pipeline->MaterialPipeline->TexturePipeline)
        {
            Pipeline->MaterialPipeline->TexturePipeline->bImportTextures = false;
        }
    }

    if (Pipeline->CommonMeshesProperties)
    {
        Pipeline->CommonMeshesProperties->ForceAllMeshAsType = EInterchangeForceMeshType::IFMT_StaticMesh;
        Pipeline->CommonMeshesProperties->bConvertStaticsWithAnimatedTransformToSkeletals = false;
        Pipeline->CommonMeshesProperties->bKeepSectionsSeparate = true;
    }

    FImportAssetParameters Params;
    Params.bIsAutomated = true;
    Params.bReplaceExisting = true;
    Params.DestinationName = Job.GmlBaseName;
    Params.OverridePipelines.Add(Pipeline);

    const FString DestPath = TEXT("/Game/PLATEAU/Meshes/") + (Job.DatasetName.IsEmpty() ? FString(TEXT("CityGML")) : Job.DatasetName);

    UE::Interchange::FAssetImportResultRef Result = Manager.ImportAssetAsync(DestPath, SourceData, Params);
    if (!Result->IsValid())
    {
        Pipeline->RemoveFromRoot();
        UE_LOG(LogPLATEAUInterchange, Error, TEXT("Interchange ImportAssetAsync failed for %s"), *Job.GmlBaseName);
        Finish(false);
        return;
    }

    Result->OnDone([Job, Pipeline, Finish](UE::Interchange::FImportResult& ImportResult) mutable
    {
        Pipeline->RemoveFromRoot();

        APLATEAUInstancedCityModel* Actor = Job.ModelActor.Get();
        USceneComponent* Root = Job.GmlRoot.Get();
        if (Actor == nullptr)
        {
            Finish(false);
            return;
        }

        TArray<UStaticMesh*> ImportedMeshes;
        for (UObject* Object : ImportResult.GetImportedObjects())
        {
            if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
            {
                ImportedMeshes.Add(Mesh);
                if (Job.Ledger.IsValid())
                {
                    Job.Ledger->RecordMesh(Mesh, Job.MaterialKeys);
                }
            }
        }

        if (Root == nullptr)
        {
            Root = Actor->GetRootComponent();
        }

        for (UStaticMesh* Mesh : ImportedMeshes)
        {
            UPLATEAUStaticMeshComponent* Comp = NewObject<UPLATEAUStaticMeshComponent>(Actor, NAME_None);
            Comp->SetMobility(EComponentMobility::Static);
            Comp->SetStaticMesh(Mesh);
            Actor->AddInstanceComponent(Comp);
            Comp->RegisterComponent();
            Comp->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
            if (Job.Ledger.IsValid())
            {
                Job.Ledger->RecordComponent(Comp);
            }
        }

        UE_LOG(LogPLATEAUInterchange, Display,
            TEXT("Interchange imported %s (%d static meshes, %d material keys recorded)"),
            *Job.GmlBaseName, ImportedMeshes.Num(), Job.MaterialKeys.Num());

        Finish(ImportedMeshes.Num() > 0);
    });
}
