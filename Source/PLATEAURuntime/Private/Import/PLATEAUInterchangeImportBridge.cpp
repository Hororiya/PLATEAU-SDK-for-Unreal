// Copyright 2023 Ministry of Land, Infrastructure and Transport
// Copyright 2026 6F978E

#include "Import/PLATEAUInterchangeImportBridge.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarUseInterchange(
    TEXT("plateau.Import.UseInterchange"),
    1,
    TEXT("1 = import CityGML meshes through Interchange (glTF pipeline). 0 = legacy per-component loader."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarDeferMaterials(
    TEXT("plateau.Import.DeferMaterials"),
    1,
    TEXT("1 = record material keys during import and create unique materials once at the end."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarMaxInFlight(
    TEXT("plateau.Import.MaxInFlight"),
    2,
    TEXT("Max concurrent Interchange glTF import jobs. Lower = less peak memory."),
    ECVF_Default);

static FPLATEAUBeginInterchangeGmlImport GInterchangeGmlImporter;

FPLATEAUBeginInterchangeGmlImport& GetPLATEAUInterchangeGmlImporter()
{
    return GInterchangeGmlImporter;
}

bool ShouldUsePLATEAUInterchangeImport()
{
    return CVarUseInterchange.GetValueOnAnyThread() != 0 && GInterchangeGmlImporter.IsBound();
}

bool ShouldDeferPLATEAUMaterials()
{
    return CVarDeferMaterials.GetValueOnAnyThread() != 0;
}

int32 GetPLATEAUInterchangeMaxInFlight()
{
    return FMath::Max(1, CVarMaxInFlight.GetValueOnAnyThread());
}
