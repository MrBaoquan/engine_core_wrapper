using System;
using System.IO;
using UnityEngine;

namespace EngineWorkflowBridge
{
    internal static class UnityBridgePaths
    {
        private static string _projectPath;

        public const string ProtocolVersion = "1.0";
        public const string EngineType = "unity";
        public const string ImportRoot = "Assets/ArtAssets";

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

        public static string ProjectPath
        {
            get
            {
                if (string.IsNullOrEmpty(_projectPath))
                {
                    throw new InvalidOperationException("UnityBridgePaths is not initialized.");
                }

                return _projectPath;
            }
        }

        public static void Initialize()
        {
            _projectPath = Path.GetFullPath(Path.Combine(Application.dataPath, ".."));
        }

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
