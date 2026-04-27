using System;
using System.Collections.Generic;

namespace EngineWorkflowBridge.Protocol
{
    [Serializable]
    public class SessionInfo
    {
        public string protocolVersion = "1.0";
        public string projectId;
        public string engineType = "unity";
        public string projectName;
        public string projectPath;
        public int processId;
        public string endpoint;
        public string[] capabilities;
        public string status;
        public string lastUpdatedUtc;
    }

    [Serializable]
    public class HealthResponse
    {
        public bool ok = true;
        public string protocolVersion = "1.0";
        public string engineType = "unity";
    }

    [Serializable]
    public class ImportAssetsRequest
    {
        public string requestId;
        public bool overwrite;
        public List<ImportAssetItem> assets;
    }

    [Serializable]
    public class ImportAssetItem
    {
        public string sourcePath;
        public string assetType;
        public string targetSubdirectory;
        public string displayName;
    }

    [Serializable]
    public class ImportAssetsResponse
    {
        public string requestId;
        public bool success;
        public List<ImportAssetResult> results = new List<ImportAssetResult>();
    }

    [Serializable]
    public class ImportAssetResult
    {
        public string sourcePath;
        public string status;
        public string engineAssetPath;
        public string engineObjectId;
        public string message;
    }
}
