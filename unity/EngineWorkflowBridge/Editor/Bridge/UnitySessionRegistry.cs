using System;
using System.Diagnostics;
using System.IO;
using EngineWorkflowBridge.Protocol;
using UnityEditor;
using UnityEngine;

namespace EngineWorkflowBridge
{
    internal sealed class UnitySessionRegistry
    {
        private readonly string _projectId;
        private readonly string _endpoint;

        public UnitySessionRegistry(string projectId, string endpoint)
        {
            _projectId = projectId;
            _endpoint = endpoint;
        }

        public string DiscoveryFilePath =>
            Path.Combine(UnityBridgePaths.DiscoveryDirectory, _projectId + ".json");

        public void Write(string status)
        {
            Directory.CreateDirectory(UnityBridgePaths.DiscoveryDirectory);
            var session = BuildSession(status);
            var json = JsonUtility.ToJson(session, true);
            File.WriteAllText(DiscoveryFilePath, json);
        }

        public void Delete()
        {
            if (File.Exists(DiscoveryFilePath))
            {
                File.Delete(DiscoveryFilePath);
            }
        }

        public SessionInfo BuildSession(string status)
        {
            return new SessionInfo
            {
                projectId = _projectId,
                projectName = Path.GetFileName(UnityBridgePaths.ProjectPath),
                projectPath = UnityBridgePaths.ProjectPath.Replace('\\', '/'),
                processId = Process.GetCurrentProcess().Id,
                endpoint = _endpoint,
                capabilities = new[] { "import.audio" },
                status = status,
                lastUpdatedUtc = DateTime.UtcNow.ToString("O")
            };
        }
    }
}
