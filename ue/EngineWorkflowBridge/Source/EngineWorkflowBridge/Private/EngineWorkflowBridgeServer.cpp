#include "EngineWorkflowBridgeServer.h"

#include "AssetToolsModule.h"
#include "Async/Async.h"
#include "AutomatedAssetImportData.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
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
#include "SocketSubsystem.h"
#include "Sockets.h"
#include "UObject/Object.h"

#if ENGINE_MAJOR_VERSION >= 5
#define ENGINE_WORKFLOW_TICKER FTSTicker::GetCoreTicker()
#else
#define ENGINE_WORKFLOW_TICKER FTicker::GetCoreTicker()
#endif

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
    WriteLog(TEXT("Start requested projectId=") + ProjectId + TEXT(" project=") + FApp::GetProjectName());
    if (!TryBindPort())
    {
        UE_LOG(LogTemp, Error, TEXT("EngineWorkflowBridge failed to bind a localhost port"));
        WriteLog(TEXT("Failed to bind any localhost port in range 38240-38339"));
        return;
    }

    Endpoint = TEXT("http://127.0.0.1:") + FString::FromInt(static_cast<int32>(Port));
    WriteDiscoveryFile();
    if (!HeartbeatTickerHandle.IsValid())
    {
        HeartbeatTickerHandle = ENGINE_WORKFLOW_TICKER.AddTicker(
            FTickerDelegate::CreateRaw(this, &FEngineWorkflowBridgeServer::HandleHeartbeat),
            2.0f);
    }
    UE_LOG(LogTemp, Display, TEXT("EngineWorkflowBridge listening on %s, session %s"), *Endpoint, *GetDiscoveryFilePath());
    WriteLog(TEXT("Listening endpoint=") + Endpoint + TEXT(" session=") + GetDiscoveryFilePath());
}

void FEngineWorkflowBridgeServer::Stop()
{
    WriteLog(TEXT("Stop requested endpoint=") + Endpoint);
    if (HeartbeatTickerHandle.IsValid())
    {
        ENGINE_WORKFLOW_TICKER.RemoveTicker(HeartbeatTickerHandle);
        HeartbeatTickerHandle.Reset();
    }

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
    WriteLog(TEXT("Stopped"));
}

bool FEngineWorkflowBridgeServer::TryBindPort()
{
    FHttpServerModule &HttpServerModule = FHttpServerModule::Get();
    for (uint32 CandidatePort = 38240; CandidatePort < 38340; ++CandidatePort)
    {
        if (!IsPortAvailable(CandidatePort))
        {
            WriteLog(TEXT("Skip port ") + FString::FromInt(static_cast<int32>(CandidatePort)) + TEXT(": already occupied by another process"));
            continue;
        }

        TSharedPtr<IHttpRouter> CandidateRouter = HttpServerModule.GetHttpRouter(CandidatePort);
        if (!CandidateRouter.IsValid())
        {
            WriteLog(TEXT("Skip port ") + FString::FromInt(static_cast<int32>(CandidatePort)) + TEXT(": router unavailable"));
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
            WriteLog(TEXT("Skip port ") + FString::FromInt(static_cast<int32>(CandidatePort)) + TEXT(": route bind failed health=") + (CandidateHealthRoute.IsValid() ? TEXT("true") : TEXT("false")) + TEXT(" session=") + (CandidateSessionRoute.IsValid() ? TEXT("true") : TEXT("false")) + TEXT(" import=") + (CandidateImportRoute.IsValid() ? TEXT("true") : TEXT("false")));
            continue;
        }

        HttpServerModule.StartAllListeners();
        Router = CandidateRouter;
        HealthRouteHandle = CandidateHealthRoute;
        SessionRouteHandle = CandidateSessionRoute;
        ImportRouteHandle = CandidateImportRoute;
        Port = CandidatePort;
        WriteLog(TEXT("Bound HTTP routes on port ") + FString::FromInt(static_cast<int32>(Port)));
        return true;
    }

    return false;
}

bool FEngineWorkflowBridgeServer::IsPortAvailable(uint32 CandidatePort) const
{
    ISocketSubsystem *SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
    if (SocketSubsystem == nullptr)
    {
        WriteLog(TEXT("Port probe skipped for ") + FString::FromInt(static_cast<int32>(CandidatePort)) + TEXT(": socket subsystem unavailable"));
        return true;
    }

    bool bIsValidAddress = false;
    TSharedRef<FInternetAddr> Address = SocketSubsystem->CreateInternetAddr();
    Address->SetIp(TEXT("127.0.0.1"), bIsValidAddress);
    Address->SetPort(CandidatePort);
    if (!bIsValidAddress)
    {
        WriteLog(TEXT("Port probe failed for ") + FString::FromInt(static_cast<int32>(CandidatePort)) + TEXT(": invalid loopback address"));
        return false;
    }

    FSocket *ProbeSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("EngineWorkflowBridgePortProbe"), false);
    if (ProbeSocket == nullptr)
    {
        WriteLog(TEXT("Port probe failed for ") + FString::FromInt(static_cast<int32>(CandidatePort)) + TEXT(": socket creation failed"));
        return false;
    }

    ProbeSocket->SetReuseAddr(false);
    const bool bCanBind = ProbeSocket->Bind(*Address);
    ProbeSocket->Close();
    SocketSubsystem->DestroySocket(ProbeSocket);
    return bCanBind;
}

bool FEngineWorkflowBridgeServer::HandleHeartbeat(float DeltaTime)
{
    WriteDiscoveryFile();
    return true;
}

bool FEngineWorkflowBridgeServer::HandleHealth(const FHttpServerRequest &Request, const FHttpResultCallback &OnComplete)
{
    WriteLog(TEXT("GET /api/v1/health"));
    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetBoolField(TEXT("ok"), true);
    Payload->SetStringField(TEXT("protocolVersion"), TEXT("1.0"));
    Payload->SetStringField(TEXT("engineType"), TEXT("unreal"));
    SendJson(OnComplete, 200, Payload);
    return true;
}

bool FEngineWorkflowBridgeServer::HandleSession(const FHttpServerRequest &Request, const FHttpResultCallback &OnComplete)
{
    WriteLog(TEXT("GET /api/v1/session"));
    SendJson(OnComplete, 200, SessionToJsonObject(BuildSessionInfo()));
    return true;
}

bool FEngineWorkflowBridgeServer::HandleImportAssets(const FHttpServerRequest &Request, const FHttpResultCallback &OnComplete)
{
    WriteLog(TEXT("POST /api/v1/import-assets bytes=") + FString::FromInt(Request.Body.Num()));
    const FUTF8ToTCHAR ConvertedBody(reinterpret_cast<const UTF8CHAR *>(Request.Body.GetData()), Request.Body.Num());
    const FString BodyString(ConvertedBody.Length(), ConvertedBody.Get());
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        WriteLog(TEXT("Import rejected: invalid JSON payload"));
        TSharedRef<FJsonObject> ErrorPayload = MakeShared<FJsonObject>();
        ErrorPayload->SetStringField(TEXT("error"), TEXT("Invalid request payload"));
        SendJson(OnComplete, 400, ErrorPayload);
        return true;
    }

    FString RequestId;
    bool bOverwrite = false;
    const TArray<TSharedPtr<FJsonValue>> *AssetsArray = nullptr;
    Root->TryGetStringField(TEXT("requestId"), RequestId);
    Root->TryGetBoolField(TEXT("overwrite"), bOverwrite);
    if (!Root->TryGetArrayField(TEXT("assets"), AssetsArray) || AssetsArray == nullptr || AssetsArray->Num() == 0)
    {
        WriteLog(TEXT("Import rejected requestId=") + RequestId + TEXT(": assets is required"));
        TSharedRef<FJsonObject> ErrorPayload = MakeShared<FJsonObject>();
        ErrorPayload->SetStringField(TEXT("error"), TEXT("assets is required"));
        SendJson(OnComplete, 400, ErrorPayload);
        return true;
    }

    SetStatus(TEXT("busy"));
    WriteDiscoveryFile();
    WriteLog(TEXT("Import started requestId=") + RequestId + TEXT(" overwrite=") + (bOverwrite ? TEXT("true") : TEXT("false")) + TEXT(" assetCount=") + FString::FromInt(AssetsArray->Num()));

    bool bAllSucceeded = true;
    TArray<TSharedPtr<FJsonValue>> ResultValues;
    for (const TSharedPtr<FJsonValue> &AssetValue : *AssetsArray)
    {
        const TSharedPtr<FJsonObject> *AssetObject;
        if (!AssetValue.IsValid() || !AssetValue->TryGetObject(AssetObject) || AssetObject == nullptr)
        {
            continue;
        }

        FImportAssetItem Item;
        (*AssetObject)->TryGetStringField(TEXT("sourcePath"), Item.SourcePath);
        (*AssetObject)->TryGetStringField(TEXT("assetType"), Item.AssetType);
        (*AssetObject)->TryGetStringField(TEXT("targetSubdirectory"), Item.TargetSubdirectory);
        (*AssetObject)->TryGetStringField(TEXT("displayName"), Item.DisplayName);
        WriteLog(TEXT("Import item source=") + Item.SourcePath + TEXT(" type=") + Item.AssetType + TEXT(" target=") + Item.TargetSubdirectory + TEXT(" display=") + Item.DisplayName);

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
        WriteLog(TEXT("Import item result status=") + Result.Status + TEXT(" path=") + Result.EngineAssetPath + TEXT(" message=") + Result.Message);
    }

    SetStatus(bAllSucceeded ? TEXT("idle") : TEXT("error"));
    WriteDiscoveryFile();

    TSharedRef<FJsonObject> Payload = MakeShared<FJsonObject>();
    Payload->SetStringField(TEXT("requestId"), RequestId);
    Payload->SetBoolField(TEXT("success"), bAllSucceeded);
    Payload->SetArrayField(TEXT("results"), ResultValues);
    WriteLog(TEXT("Import finished requestId=") + RequestId + TEXT(" success=") + (bAllSucceeded ? TEXT("true") : TEXT("false")));
    SendJson(OnComplete, 200, Payload);
    return true;
}

FEngineWorkflowBridgeServer::FImportAssetResult FEngineWorkflowBridgeServer::ImportAudio(const FImportAssetItem &Item, bool bOverwrite)
{
    FImportAssetResult Result;
    Result.SourcePath = Item.SourcePath;
    Result.Status = TEXT("failed");

    if (Item.SourcePath.IsEmpty() || !FPaths::FileExists(Item.SourcePath))
    {
        Result.Message = TEXT("Source file does not exist");
        WriteLog(TEXT("ImportAudio failed: source missing path=") + Item.SourcePath);
        return Result;
    }

    const FString Extension = FPaths::GetExtension(Item.SourcePath, true).ToLower();
    const TSet<FString> AllowedExtensions = {TEXT(".wav"), TEXT(".mp3"), TEXT(".ogg"), TEXT(".flac"), TEXT(".aiff")};
    if (!AllowedExtensions.Contains(Extension))
    {
        Result.Message = TEXT("Unsupported audio extension: ") + Extension;
        WriteLog(TEXT("ImportAudio failed: unsupported extension=") + Extension + TEXT(" source=") + Item.SourcePath);
        return Result;
    }

    const FString SafeDestinationPath = NormalizeTargetSubdirectory(Item.TargetSubdirectory);
    if (SafeDestinationPath.IsEmpty() && !Item.TargetSubdirectory.IsEmpty())
    {
        Result.Message = TEXT("targetSubdirectory is invalid");
        WriteLog(TEXT("ImportAudio failed: invalid targetSubdirectory=") + Item.TargetSubdirectory);
        return Result;
    }

    FString DestinationPath = TEXT("/Game/ArtAssets");
    if (!SafeDestinationPath.IsEmpty())
    {
        DestinationPath = SafeDestinationPath;
    }
    WriteLog(TEXT("ImportAudio dispatch source=") + Item.SourcePath + TEXT(" destination=") + DestinationPath + TEXT(" overwrite=") + (bOverwrite ? TEXT("true") : TEXT("false")));

    FString SourceBaseName = Item.DisplayName.IsEmpty()
                                 ? FPaths::GetBaseFilename(Item.SourcePath)
                                 : Item.DisplayName;
    SourceBaseName = FPaths::MakeValidFileName(SourceBaseName);
    if (SourceBaseName.IsEmpty())
    {
        SourceBaseName = TEXT("DesktopPetAudio");
    }

    const FString CacheDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EngineWorkflowBridge"), TEXT("ImportCache"));
    IFileManager::Get().MakeDirectory(*CacheDirectory, true);
    const FString LocalImportPath = FPaths::Combine(
        CacheDirectory,
        SourceBaseName + TEXT("_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8) + Extension);

    if (!FPlatformFileManager::Get().GetPlatformFile().CopyFile(*LocalImportPath, *Item.SourcePath))
    {
        Result.Message = TEXT("Failed to copy source audio into local UE import cache");
        WriteLog(TEXT("ImportAudio failed: copy to cache failed source=") + Item.SourcePath + TEXT(" cache=") + LocalImportPath);
        return Result;
    }

    WriteLog(TEXT("ImportAudio copied to local cache=") + LocalImportPath);

    TArray<FString> FilesToImport;
    FilesToImport.Add(LocalImportPath);

    auto RunImportOnGameThread = [&Result, DestinationPath, FilesToImport, bOverwrite]()
    {
        UAutomatedAssetImportData *ImportData = NewObject<UAutomatedAssetImportData>();
        ImportData->AddToRoot();
        ImportData->DestinationPath = DestinationPath;
        ImportData->bReplaceExisting = bOverwrite;
        ImportData->Filenames = FilesToImport;

        FAssetToolsModule &AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
        TArray<UObject *> ImportedAssets = AssetToolsModule.Get().ImportAssetsAutomated(ImportData);
        ImportData->RemoveFromRoot();
        if (ImportedAssets.Num() == 0 || ImportedAssets[0] == nullptr)
        {
            Result.Message = TEXT("UE asset import did not return an asset");
            return;
        }

        UObject *ImportedObject = ImportedAssets[0];
        Result.Status = TEXT("imported");
        Result.EngineAssetPath = ImportedObject->GetPathName();
        Result.EngineObjectId = ImportedObject->GetPathName();
        Result.Message = TEXT("Imported successfully");
    };

    if (IsInGameThread())
    {
        WriteLog(TEXT("ImportAudio running directly on GameThread"));
        RunImportOnGameThread();
    }
    else
    {
        FEvent *CompletionEvent = FPlatformProcess::GetSynchEventFromPool(true);
        AsyncTask(ENamedThreads::GameThread, [&RunImportOnGameThread, CompletionEvent]()
                  {
            RunImportOnGameThread();
            CompletionEvent->Trigger(); });

        CompletionEvent->Wait();
        FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
    }
    WriteLog(TEXT("ImportAudio completed status=") + Result.Status + TEXT(" object=") + Result.EngineAssetPath + TEXT(" message=") + Result.Message);

    IFileManager::Get().Delete(*LocalImportPath, false, true);
    return Result;
}

void FEngineWorkflowBridgeServer::WriteDiscoveryFile() const
{
    FScopeLock Lock(&DiscoveryFileLock);
    IFileManager::Get().MakeDirectory(*GetDiscoveryDirectory(), true);
    FString Json;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
    FJsonSerializer::Serialize(SessionToJsonObject(BuildSessionInfo()), Writer);
    FFileHelper::SaveStringToFile(Json, *GetDiscoveryFilePath());
}

void FEngineWorkflowBridgeServer::DeleteDiscoveryFile() const
{
    FScopeLock Lock(&DiscoveryFileLock);
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
    Session.Capabilities = {TEXT("import.audio")};
    Session.Status = GetStatus();
    Session.LastUpdatedUtc = MakeTimestampUtc();
    return Session;
}

FString FEngineWorkflowBridgeServer::GetDiscoveryDirectory() const
{
    const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
    if (!LocalAppData.IsEmpty())
    {
        return FPaths::Combine(LocalAppData, TEXT("EngineWorkflowBridge"), TEXT("Sessions"));
    }

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
    return TEXT("unreal-") + Hash;
}

FString FEngineWorkflowBridgeServer::MakeTimestampUtc() const
{
    return FDateTime::UtcNow().ToIso8601();
}

FString FEngineWorkflowBridgeServer::GetLogPath() const
{
    const FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
    const FString LogRoot = !LocalAppData.IsEmpty()
                                ? FPaths::Combine(LocalAppData, TEXT("EngineWorkflowBridge"), TEXT("Logs"))
                                : FPaths::Combine(FPlatformProcess::UserSettingsDir(), TEXT("EngineWorkflowBridge"), TEXT("Logs"));
    return FPaths::Combine(LogRoot, TEXT("ue-bridge.log"));
}

void FEngineWorkflowBridgeServer::WriteLog(const FString &Message) const
{
    const FString BridgeLogPath = GetLogPath();
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(BridgeLogPath), true);
    const FString Line = MakeTimestampUtc() + TEXT(" [") + ProjectId + TEXT("] ") + Message + LINE_TERMINATOR;
    FFileHelper::SaveStringToFile(Line, *BridgeLogPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
}

FString FEngineWorkflowBridgeServer::GetStatus() const
{
    FScopeLock Lock(&StateLock);
    return Status;
}

void FEngineWorkflowBridgeServer::SetStatus(const FString &NewStatus)
{
    FScopeLock Lock(&StateLock);
    Status = NewStatus;
}

FString FEngineWorkflowBridgeServer::NormalizeTargetSubdirectory(const FString &Input) const
{
    FString Normalized = Input;
    Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
    Normalized.RemoveFromEnd(TEXT("/"));
    if (Normalized.Contains(TEXT("..")))
    {
        return FString();
    }

    if (Normalized.IsEmpty())
    {
        return FString();
    }

    if (Normalized.StartsWith(TEXT("/Game/")))
    {
        return Normalized;
    }

    if (Normalized.StartsWith(TEXT("Game/")))
    {
        return TEXT("/") + Normalized;
    }

    const bool bAbsoluteProjectPath = Normalized.StartsWith(TEXT("/"));
    Normalized.RemoveFromStart(TEXT("/"));

    if (Normalized.StartsWith(TEXT("WorkflowImports/")))
    {
        return TEXT("/Game/") + Normalized;
    }

    if (Normalized.StartsWith(TEXT("ArtAssets/")))
    {
        return TEXT("/Game/") + Normalized;
    }

    if (bAbsoluteProjectPath)
    {
        return TEXT("/Game/") + Normalized;
    }

    return TEXT("/Game/ArtAssets/") + Normalized;
}

TSharedRef<FJsonObject> FEngineWorkflowBridgeServer::SessionToJsonObject(const FSessionInfo &Session) const
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
    for (const FString &Capability : Session.Capabilities)
    {
        CapabilitiesValues.Add(MakeShared<FJsonValueString>(Capability));
    }

    Json->SetArrayField(TEXT("capabilities"), CapabilitiesValues);
    Json->SetStringField(TEXT("status"), Session.Status);
    Json->SetStringField(TEXT("lastUpdatedUtc"), Session.LastUpdatedUtc);
    return Json;
}

TSharedRef<FJsonObject> FEngineWorkflowBridgeServer::ResultToJsonObject(const FImportAssetResult &Result) const
{
    TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    Json->SetStringField(TEXT("sourcePath"), Result.SourcePath);
    Json->SetStringField(TEXT("status"), Result.Status);
    Json->SetStringField(TEXT("engineAssetPath"), Result.EngineAssetPath);
    Json->SetStringField(TEXT("engineObjectId"), Result.EngineObjectId);
    Json->SetStringField(TEXT("message"), Result.Message);
    return Json;
}

void FEngineWorkflowBridgeServer::SendJson(const FHttpResultCallback &OnComplete, int32 StatusCode, const TSharedRef<FJsonObject> &Payload) const
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