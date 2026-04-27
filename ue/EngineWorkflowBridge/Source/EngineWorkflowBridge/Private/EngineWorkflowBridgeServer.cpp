#include "EngineWorkflowBridgeServer.h"

#include "AssetToolsModule.h"
#include "Async/Async.h"
#include "AutomatedAssetImportData.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HttpPath.h"
#include "IHttpRouter.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/DateTime.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Object.h"
#include "UObject/StrongObjectPtr.h"

FEngineWorkflowBridgeServer::FEngineWorkflowBridgeServer()
{
}

FEngineWorkflowBridgeServer::~FEngineWorkflowBridgeServer()
{
    Stop();
}

void FEngineWorkflowBridgeServer::Start()
{
    ProjectId = MakeProjectId();
    if (!TryBindPort())
    {
        UE_LOG(LogTemp, Error, TEXT("EngineWorkflowBridge failed to bind a localhost port"));
        return;
    }

    Endpoint = FString::Printf(TEXT("http://127.0.0.1:%u"), Port);
    WriteDiscoveryFile();
}

void FEngineWorkflowBridgeServer::Stop()
{
    if (Router.IsValid())
    {
        if (HealthRouteHandle.IsValid())
        {
            Router->UnbindRoute(HealthRouteHandle);
            HealthRouteHandle.Reset();
        }

        if (SessionRouteHandle.IsValid())
        {
            Router->UnbindRoute(SessionRouteHandle);
            SessionRouteHandle.Reset();
        }

        if (ImportRouteHandle.IsValid())
        {
            Router->UnbindRoute(ImportRouteHandle);
            ImportRouteHandle.Reset();
        }

        FHttpServerModule::Get().StopAllListeners();
        Router.Reset();
    }

    DeleteDiscoveryFile();
}

bool FEngineWorkflowBridgeServer::TryBindPort()
{
    FHttpServerModule& HttpServerModule = FHttpServerModule::Get();
    for (uint32 CandidatePort = 38240; CandidatePort < 38340; ++CandidatePort)
    {
        TSharedPtr<IHttpRouter> CandidateRouter = HttpServerModule.GetHttpRouter(CandidatePort, true);
        if (!CandidateRouter.IsValid())
        {
            continue;
        }

        FHttpRouteHandle CandidateHealthRoute = CandidateRouter->BindRoute(
            FHttpPath(TEXT("/api/v1/health")),
            EHttpServerRequestVerbs::VERB_GET,
            FHttpRequestHandler::CreateRaw(this, &FEngineWorkflowBridgeServer::HandleHealth));

        FHttpRouteHandle CandidateSessionRoute = CandidateRouter->BindRoute(
            FHttpPath(TEXT("/api/v1/session")),
            EHttpServerRequestVerbs::VERB_GET,
            FHttpRequestHandler::CreateRaw(this, &FEngineWorkflowBridgeServer::HandleSession));

        FHttpRouteHandle CandidateImportRoute = CandidateRouter->BindRoute(
            FHttpPath(TEXT("/api/v1/import-assets")),
            EHttpServerRequestVerbs::VERB_POST,
            FHttpRequestHandler::CreateRaw(this, &FEngineWorkflowBridgeServer::HandleImportAssets));

        if (!CandidateHealthRoute.IsValid() || !CandidateSessionRoute.IsValid() || !CandidateImportRoute.IsValid())
        {
            continue;
        }

        HttpServerModule.StartAllListeners();
        Router = CandidateRouter;
        HealthRouteHandle = CandidateHealthRoute;
        SessionRouteHandle = CandidateSessionRoute;
        ImportRouteHandle = CandidateImportRoute;
        Port = CandidatePort;
        return true;
    }

    return false;
}

bool FEngineWorkflowBridgeServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetBoolField(TEXT("ok"), true);
    Payload->SetStringField(TEXT("protocolVersion"), TEXT("1.0"));
    Payload->SetStringField(TEXT("engineType"), TEXT("unreal"));
    SendJson(OnComplete, 200, Payload);
    return true;
}

bool FEngineWorkflowBridgeServer::HandleSession(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    SendJson(OnComplete, 200, SessionToJsonObject(BuildSessionInfo()));
    return true;
}

bool FEngineWorkflowBridgeServer::HandleImportAssets(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
    const FUTF8ToTCHAR ConvertedBody(reinterpret_cast<const UTF8CHAR*>(Request.Body.GetData()), Request.Body.Num());
    const FString BodyString(ConvertedBody.Length(), ConvertedBody.Get());
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        TSharedRef<FJsonObject> ErrorPayload = MakeShared<FJsonObject>();
        ErrorPayload->SetStringField(TEXT("error"), TEXT("Invalid request payload"));
        SendJson(OnComplete, 400, ErrorPayload);
        return true;
    }

    FString RequestId;
    bool bOverwrite = false;
    const TArray<TSharedPtr<FJsonValue>>* AssetsArray = nullptr;
    Root->TryGetStringField(TEXT("requestId"), RequestId);
    Root->TryGetBoolField(TEXT("overwrite"), bOverwrite);
    if (!Root->TryGetArrayField(TEXT("assets"), AssetsArray) || AssetsArray == nullptr || AssetsArray->Num() == 0)
    {
        TSharedRef<FJsonObject> ErrorPayload = MakeShared<FJsonObject>();
        ErrorPayload->SetStringField(TEXT("error"), TEXT("assets is required"));
        SendJson(OnComplete, 400, ErrorPayload);
        return true;
    }

    Status = TEXT("busy");
    WriteDiscoveryFile();

    bool bAllSucceeded = true;
    TArray<TSharedPtr<FJsonValue>> ResultValues;
    for (const TSharedPtr<FJsonValue>& AssetValue : *AssetsArray)
    {
        const TSharedPtr<FJsonObject>* AssetObject;
        if (!AssetValue.IsValid() || !AssetValue->TryGetObject(AssetObject) || AssetObject == nullptr)
        {
            continue;
        }

        FImportAssetItem Item;
        (*AssetObject)->TryGetStringField(TEXT("sourcePath"), Item.SourcePath);
        (*AssetObject)->TryGetStringField(TEXT("assetType"), Item.AssetType);
        (*AssetObject)->TryGetStringField(TEXT("targetSubdirectory"), Item.TargetSubdirectory);
        (*AssetObject)->TryGetStringField(TEXT("displayName"), Item.DisplayName);

        FImportAssetResult Result;
        if (!Item.AssetType.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
        {
            Result.SourcePath = Item.SourcePath;
            Result.Status = TEXT("failed");
            Result.Message = TEXT("Unsupported assetType");
        }
        else
        {
            Result = ImportAudio(Item, bOverwrite);
        }

        if (!Result.Status.Equals(TEXT("imported"), ESearchCase::IgnoreCase))
        {
            bAllSucceeded = false;
        }

        ResultValues.Add(MakeShared<FJsonValueObject>(ResultToJsonObject(Result)));
    }

    Status = bAllSucceeded ? TEXT("idle") : TEXT("error");
    WriteDiscoveryFile();

    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("requestId"), RequestId);
    Payload->SetBoolField(TEXT("success"), bAllSucceeded);
    Payload->SetArrayField(TEXT("results"), ResultValues);
    SendJson(OnComplete, 200, Payload);
    return true;
}

FEngineWorkflowBridgeServer::FImportAssetResult FEngineWorkflowBridgeServer::ImportAudio(const FImportAssetItem& Item, bool bOverwrite)
{
    FImportAssetResult Result;
    Result.SourcePath = Item.SourcePath;
    Result.Status = TEXT("failed");

    if (Item.SourcePath.IsEmpty() || !FPaths::FileExists(Item.SourcePath))
    {
        Result.Message = TEXT("Source file does not exist");
        return Result;
    }

    const FString Extension = FPaths::GetExtension(Item.SourcePath, true).ToLower();
    const TSet<FString> AllowedExtensions = { TEXT(".wav"), TEXT(".mp3"), TEXT(".ogg"), TEXT(".flac"), TEXT(".aiff") };
    if (!AllowedExtensions.Contains(Extension))
    {
        Result.Message = FString::Printf(TEXT("Unsupported audio extension: %s"), *Extension);
        return Result;
    }

    const FString SafeSubdirectory = NormalizeTargetSubdirectory(Item.TargetSubdirectory);
    if (SafeSubdirectory.IsEmpty() && !Item.TargetSubdirectory.IsEmpty())
    {
        Result.Message = TEXT("targetSubdirectory is invalid");
        return Result;
    }

    FString DestinationPath = TEXT("/Game/WorkflowImports");
    if (!SafeSubdirectory.IsEmpty())
    {
        DestinationPath /= SafeSubdirectory;
    }

    TArray<FString> FilesToImport;
    FilesToImport.Add(Item.SourcePath);

    FEvent* CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
    AsyncTask(ENamedThreads::GameThread, [&Result, &Item, DestinationPath, FilesToImport, bOverwrite, CompletionEvent]()
    {
        TStrongObjectPtr<UAutomatedAssetImportData> ImportData(NewObject<UAutomatedAssetImportData>());
        ImportData->DestinationPath = DestinationPath;
        ImportData->bReplaceExisting = bOverwrite;
        ImportData->Filenames = FilesToImport;

        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        TArray<UObject*> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData.Get());
        if (ImportedAssets.Num() == 0 || ImportedAssets[0] == nullptr)
        {
            Result.Message = TEXT("UE asset import did not return an asset");
            CompletionEvent->Trigger();
            return;
        }

        UObject* ImportedObject = ImportedAssets[0];
        Result.Status = TEXT("imported");
        Result.EngineAssetPath = ImportedObject->GetPathName();
        Result.EngineObjectId = ImportedObject->GetPathName();
        Result.Message = TEXT("Imported successfully");
        CompletionEvent->Trigger();
    });

    CompletionEvent->Wait();
    FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    return Result;
}

void FEngineWorkflowBridgeServer::WriteDiscoveryFile() const
{
    IFileManager::Get().MakeDirectory(*GetDiscoveryDirectory(), true);
    FString Json;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(SessionToJsonObject(BuildSessionInfo()), Writer);
    FFileHelper::SaveStringToFile(Json, *GetDiscoveryFilePath());
}

void FEngineWorkflowBridgeServer::DeleteDiscoveryFile() const
{
    IFileManager::Get().Delete(*GetDiscoveryFilePath(), false, true);
}

FEngineWorkflowBridgeServer::FSessionInfo FEngineWorkflowBridgeServer::BuildSessionInfo() const
{
    FSessionInfo Session;
    Session.ProjectId = ProjectId;
    Session.ProjectName = FApp::GetProjectName();
    Session.ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
    Session.ProcessId = FPlatformProcess::GetCurrentProcessId();
    Session.Endpoint = Endpoint;
    Session.Capabilities = { TEXT("import.audio") };
    Session.Status = Status;
    Session.LastUpdatedUtc = MakeTimestampUtc();
    return Session;
}

FString FEngineWorkflowBridgeServer::GetDiscoveryDirectory() const
{
    return FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("EngineWorkflowBridge"), TEXT("Sessions"));
}

FString FEngineWorkflowBridgeServer::GetDiscoveryFilePath() const
{
    return FPaths::Combine(GetDiscoveryDirectory(), ProjectId + TEXT(".json"));
}

FString FEngineWorkflowBridgeServer::MakeProjectId() const
{
    const FString Normalized = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()).ToLower();
    const FString Hash = FMD5::HashAnsiString(*Normalized).Left(12);
    return FString::Printf(TEXT("unreal-%s"), *Hash);
}

FString FEngineWorkflowBridgeServer::MakeTimestampUtc() const
{
    return FDateTime::UtcNow().ToIso8601();
}

FString FEngineWorkflowBridgeServer::NormalizeTargetSubdirectory(const FString& Input) const
{
    FString Normalized = Input;
    Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
    Normalized.RemoveFromStart(TEXT("/"));
    Normalized.RemoveFromEnd(TEXT("/"));
    if (Normalized.Contains(TEXT("..")))
    {
        return FString();
    }

    if (Normalized.StartsWith(TEXT("/Game/")))
    {
        Normalized.RightChopInline(6, EAllowShrinking::No);
    }

    if (Normalized.StartsWith(TEXT("WorkflowImports/")))
    {
        Normalized.RightChopInline(16, EAllowShrinking::No);
    }

    return Normalized;
}

TSharedRef<FJsonObject> FEngineWorkflowBridgeServer::SessionToJsonObject(const FSessionInfo& Session) const
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("protocolVersion"), Session.ProtocolVersion);
    Json->SetStringField(TEXT("projectId"), Session.ProjectId);
    Json->SetStringField(TEXT("engineType"), Session.EngineType);
    Json->SetStringField(TEXT("projectName"), Session.ProjectName);
    Json->SetStringField(TEXT("projectPath"), Session.ProjectPath);
    Json->SetNumberField(TEXT("processId"), Session.ProcessId);
    Json->SetStringField(TEXT("endpoint"), Session.Endpoint);

    TArray<TSharedPtr<FJsonValue>> CapabilitiesValues;
    for (const FString& Capability : Session.Capabilities)
    {
        CapabilitiesValues.Add(MakeShared<FJsonValueString>(Capability));
    }

    Json->SetArrayField(TEXT("capabilities"), CapabilitiesValues);
    Json->SetStringField(TEXT("status"), Session.Status);
    Json->SetStringField(TEXT("lastUpdatedUtc"), Session.LastUpdatedUtc);
    return Json;
}

TSharedRef<FJsonObject> FEngineWorkflowBridgeServer::ResultToJsonObject(const FImportAssetResult& Result) const
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("sourcePath"), Result.SourcePath);
    Json->SetStringField(TEXT("status"), Result.Status);
    Json->SetStringField(TEXT("engineAssetPath"), Result.EngineAssetPath);
    Json->SetStringField(TEXT("engineObjectId"), Result.EngineObjectId);
    Json->SetStringField(TEXT("message"), Result.Message);
    return Json;
}

void FEngineWorkflowBridgeServer::SendJson(const FHttpResultCallback& OnComplete, int32 StatusCode, const TSharedRef<FJsonObject>& Payload) const
{
    FString Body;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
    FJsonSerializer::Serialize(Payload, Writer);

    TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Body, TEXT("application/json"));
    switch (StatusCode)
    {
    case 200:
        Response->Code = EHttpServerResponseCodes::Ok;
        break;
    case 400:
        Response->Code = EHttpServerResponseCodes::BadRequest;
        break;
    case 404:
        Response->Code = EHttpServerResponseCodes::NotFound;
        break;
    default:
        Response->Code = EHttpServerResponseCodes::ServerError;
        break;
    }
    OnComplete(MoveTemp(Response));
}