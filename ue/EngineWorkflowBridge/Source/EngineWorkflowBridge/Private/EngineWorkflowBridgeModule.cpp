#include "EngineWorkflowBridgeModule.h"
#include "EngineWorkflowBridgeServer.h"

void FEngineWorkflowBridgeModule::StartupModule()
{
    Server = MakeUnique<FEngineWorkflowBridgeServer>();
    Server->Start();
}

void FEngineWorkflowBridgeModule::ShutdownModule()
{
    if (Server)
    {
        Server->Stop();
        Server.Reset();
    }
}

IMPLEMENT_MODULE(FEngineWorkflowBridgeModule, EngineWorkflowBridge)