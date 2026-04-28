using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using EngineWorkflowBridge.Protocol;
using UnityEditor;

namespace EngineWorkflowBridge
{
    internal sealed class UnitySessionRegistry
    {
        private readonly string _projectId;
        private readonly string _endpoint;
        private readonly object _writeLock = new object();

        public UnitySessionRegistry(string projectId, string endpoint)
        {
            _projectId = projectId;
            _endpoint = endpoint;
        }

        public string DiscoveryFilePath =>
            Path.Combine(UnityBridgePaths.DiscoveryDirectory, _projectId + ".json");

        public void Write(string status)
        {
            lock (_writeLock)
            {
                Directory.CreateDirectory(UnityBridgePaths.DiscoveryDirectory);
                var session = BuildSession(status);
                var json = SerializeSession(session);
                File.WriteAllText(DiscoveryFilePath, json);
            }
        }

        public void Delete()
        {
            lock (_writeLock)
            {
                if (File.Exists(DiscoveryFilePath))
                {
                    File.Delete(DiscoveryFilePath);
                }
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

        private static string SerializeSession(SessionInfo session)
        {
            var capabilities = session.capabilities == null
                ? string.Empty
                : string.Join(",", session.capabilities.Select(item => "\"" + Escape(item) + "\""));

            var builder = new StringBuilder();
            builder.AppendLine("{");
            AppendProperty(builder, "protocolVersion", session.protocolVersion, true);
            AppendProperty(builder, "projectId", session.projectId, true);
            AppendProperty(builder, "engineType", session.engineType, true);
            AppendProperty(builder, "projectName", session.projectName, true);
            AppendProperty(builder, "projectPath", session.projectPath, true);
            builder.Append("  \"processId\": ").Append(session.processId).AppendLine(",");
            AppendProperty(builder, "endpoint", session.endpoint, true);
            builder.Append("  \"capabilities\": [").Append(capabilities).AppendLine("],");
            AppendProperty(builder, "status", session.status, true);
            AppendProperty(builder, "lastUpdatedUtc", session.lastUpdatedUtc, false);
            builder.AppendLine("}");
            return builder.ToString();
        }

        private static void AppendProperty(StringBuilder builder, string name, string value, bool comma)
        {
            builder.Append("  \"").Append(name).Append("\": \"").Append(Escape(value)).Append("\"");
            if (comma)
            {
                builder.Append(',');
            }
            builder.AppendLine();
        }

        private static string Escape(string value)
        {
            if (string.IsNullOrEmpty(value))
            {
                return string.Empty;
            }

            return value
                .Replace("\\", "\\\\")
                .Replace("\"", "\\\"")
                .Replace("\r", "\\r")
                .Replace("\n", "\\n");
        }
    }
}
