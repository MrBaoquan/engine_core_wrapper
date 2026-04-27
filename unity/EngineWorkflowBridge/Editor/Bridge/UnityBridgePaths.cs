using System;
using System.IO;
using UnityEngine;

namespace EngineWorkflowBridge
{
    internal static class UnityBridgePaths
    {
        public const string ProtocolVersion = "1.0";
        public const string EngineType = "unity";
        public const string ImportRoot = "Assets/WorkflowImports";

        public static string DiscoveryDirectory
        {
            get
            {
                var baseDirectory = Environment.GetFolderPath(
                    Environment.SpecialFolder.LocalApplicationData
                );
                return Path.Combine(baseDirectory, "EngineWorkflowBridge", "Sessions");
            }
        }

        public static string ProjectPath =>
            Path.GetFullPath(Path.Combine(Application.dataPath, ".."));

        public static string MakeProjectId()
        {
            var normalized = ProjectPath.Replace('\\', '/').ToLowerInvariant();
            using (var hasher = System.Security.Cryptography.MD5.Create())
            {
                var bytes = System.Text.Encoding.UTF8.GetBytes(normalized);
                var hash = hasher.ComputeHash(bytes);
                return $"unity-{BitConverter.ToString(hash).Replace("-", string.Empty).Substring(0, 12).ToLowerInvariant()}";
            }
        }
    }
}
