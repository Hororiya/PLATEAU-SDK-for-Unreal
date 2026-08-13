// Copyright 2023 Ministry of Land, Infrastructure and Transport
// Copyright 2026 6F978E

#pragma once

#include "CoreMinimal.h"

struct FPLATEAUInterchangeGmlJob;

class FPLATEAUInterchangeMeshImporter
{
public:
    static void Register();
    static void Unregister();

private:
    static void BeginJob(FPLATEAUInterchangeGmlJob Job);
    static void ImportOnGameThread(FPLATEAUInterchangeGmlJob Job);
    static void AcquireSlot();
    static void ReleaseSlot();
};
