#pragma once

#include "Modules/ModuleManager.h"

class FEngineWorkflowBridgeServer;

class FEngineWorkflowBridgeModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TUniquePtr<FEngineWorkflowBridgeServer> Server;
};