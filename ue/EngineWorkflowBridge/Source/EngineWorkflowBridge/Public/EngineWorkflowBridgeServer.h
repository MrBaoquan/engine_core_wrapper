#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "HAL/CriticalSection.h"
#include "HttpRouteHandle.h"
#include "HttpResultCallback.h"
#include "Runtime/Launch/Resources/Version.h"

class IHttpRouter;
struct FHttpServerRequest;

class FEngineWorkflowBridgeServer
{
public:
    FEngineWorkflowBridgeServer();
    ~FEngineWorkflowBridgeServer();

    void Start();
    void Stop();

private:
    struct FSessionInfo
    {
        FString ProtocolVersion = TEXT("1.0");
        FString ProjectId;
        FString EngineType = TEXT("unreal");
        FString ProjectName;
        FString ProjectPath;
        int32 ProcessId = 0;
        FString Endpoint;
        TArray<FString> Capabilities;
        FString Status = TEXT("idle");
        FString LastUpdatedUtc;
    };

    struct FImportAssetItem
    {
        FString SourcePath;
        FString AssetType;
        FString TargetSubdirectory;
        FString DisplayName;
    };

    struct FImportAssetResult
    {
        FString SourcePath;
        FString Status;
        FString EngineAssetPath;
        FString EngineObjectId;
        FString Message;
    };

    bool HandleHealth(const FHttpServerRequest &Request, const FHttpResultCallback &OnComplete);
    bool HandleSession(const FHttpServerRequest &Request, const FHttpResultCallback &OnComplete);
    bool HandleImportAssets(const FHttpServerRequest &Request, const FHttpResultCallback &OnComplete);

    bool TryBindPort();
    bool IsPortAvailable(uint32 CandidatePort) const;
    bool HandleHeartbeat(float DeltaTime);
    void WriteDiscoveryFile() const;
    void DeleteDiscoveryFile() const;
    FSessionInfo BuildSessionInfo() const;
    FString GetDiscoveryDirectory() const;
    FString GetDiscoveryFilePath() const;
    FString MakeProjectId() const;
    FString MakeTimestampUtc() const;
    FString GetLogPath() const;
    void WriteLog(const FString &Message) const;
    FString GetStatus() const;
    void SetStatus(const FString &NewStatus);
    FString NormalizeTargetSubdirectory(const FString &Input) const;
    TSharedRef<class FJsonObject> SessionToJsonObject(const FSessionInfo &Session) const;
    TSharedRef<class FJsonObject> ResultToJsonObject(const FImportAssetResult &Result) const;
    FImportAssetResult ImportAudio(const FImportAssetItem &Item, bool bOverwrite);
    void SendJson(const FHttpResultCallback &OnComplete, int32 StatusCode, const TSharedRef<class FJsonObject> &Payload) const;

private:
    FHttpRouteHandle HealthRouteHandle;
    FHttpRouteHandle SessionRouteHandle;
    FHttpRouteHandle ImportRouteHandle;
#if ENGINE_MAJOR_VERSION >= 5
    FTSTicker::FDelegateHandle HeartbeatTickerHandle;
#else
    FDelegateHandle HeartbeatTickerHandle;
#endif
    TSharedPtr<IHttpRouter> Router;
    mutable FCriticalSection DiscoveryFileLock;
    mutable FCriticalSection StateLock;
    uint32 Port = 0;
    FString ProjectId;
    FString Endpoint;
    FString Status = TEXT("idle");
};