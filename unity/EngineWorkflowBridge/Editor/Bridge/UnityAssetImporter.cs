using System;
using System.Collections.Generic;
using System.IO;
using EngineWorkflowBridge.Protocol;
using UnityEditor;
using UnityEngine;

namespace EngineWorkflowBridge
{
    internal static class UnityAssetImporter
    {
        private static readonly HashSet<string> AllowedAudioExtensions = new HashSet<string>(
            StringComparer.OrdinalIgnoreCase
        )
        {
            ".wav",
            ".mp3",
            ".ogg",
            ".flac",
            ".aiff"
        };

        public static ImportAssetResult ImportAudio(ImportAssetItem item, bool overwrite)
        {
            UnityBridgeLog.Info("ImportAudio request source='" + item.sourcePath + "' targetSubdirectory='" + item.targetSubdirectory + "' displayName='" + item.displayName + "'");
            var result = CopyAudioIntoProject(item, overwrite);
            if (string.Equals(result.status, "imported", StringComparison.OrdinalIgnoreCase)
                && !string.IsNullOrWhiteSpace(result.engineAssetPath))
            {
                UnityBridgeLog.Info("Copied audio to '" + result.engineAssetPath + "'; queueing AssetDatabase refresh.");
                UnityMainThreadDispatcher.Post(() => RefreshImportedAudio(result.engineAssetPath));
            }

            return result;
        }

        private static ImportAssetResult CopyAudioIntoProject(ImportAssetItem item, bool overwrite)
        {
            var result = new ImportAssetResult
            {
                sourcePath = item.sourcePath,
                status = "failed",
                message = "Unhandled import failure"
            };

            if (string.IsNullOrWhiteSpace(item.sourcePath) || !Path.IsPathRooted(item.sourcePath))
            {
                result.message = "sourcePath must be an absolute path";
                return result;
            }

            if (!File.Exists(item.sourcePath))
            {
                result.message = "Source file does not exist";
                return result;
            }

            var extension = Path.GetExtension(item.sourcePath);
            if (!AllowedAudioExtensions.Contains(extension))
            {
                result.message = $"Unsupported audio extension: {extension}";
                return result;
            }

            var safeSubdirectory = NormalizeTargetSubdirectory(item.targetSubdirectory);
            if (safeSubdirectory == null)
            {
                result.message = "targetSubdirectory is invalid";
                return result;
            }

            var fileName = BuildTargetFileName(item, extension);
            var assetDirectory = string.IsNullOrEmpty(safeSubdirectory)
                ? UnityBridgePaths.ImportRoot
                : $"{UnityBridgePaths.ImportRoot}/{safeSubdirectory}";

            var absoluteDirectory = Path.Combine(
                UnityBridgePaths.ProjectPath,
                assetDirectory.Replace('/', Path.DirectorySeparatorChar)
            );
            Directory.CreateDirectory(absoluteDirectory);

            var assetPath = $"{assetDirectory}/{fileName}";
            var absoluteTargetPath = Path.Combine(absoluteDirectory, fileName);

            if (File.Exists(absoluteTargetPath) && !overwrite)
            {
                result.status = "skipped";
                result.message = "Target asset already exists";
                result.engineAssetPath = assetPath;
                return result;
            }

            File.Copy(item.sourcePath, absoluteTargetPath, overwrite);
            UnityBridgeLog.Info("Copied file source='" + item.sourcePath + "' target='" + absoluteTargetPath + "'");

            result.status = "imported";
            result.engineAssetPath = assetPath;
            result.engineObjectId = string.Empty;
            result.message = "Copied into Unity project; AssetDatabase refresh queued";
            return result;
        }

        private static void RefreshImportedAudio(string assetPath)
        {
            UnityBridgeLog.Info("Refreshing AssetDatabase for '" + assetPath + "'");
            AssetDatabase.ImportAsset(
                assetPath,
                ImportAssetOptions.ForceSynchronousImport | ImportAssetOptions.ForceUpdate
            );
            AssetDatabase.Refresh(ImportAssetOptions.ForceSynchronousImport);
            UnityBridgeLog.Info("AssetDatabase refresh completed for '" + assetPath + "'");
        }

        private static string BuildTargetFileName(ImportAssetItem item, string extension)
        {
            if (string.IsNullOrWhiteSpace(item.displayName))
            {
                return Path.GetFileName(item.sourcePath);
            }

            foreach (var invalidChar in Path.GetInvalidFileNameChars())
            {
                item.displayName = item.displayName.Replace(invalidChar, '_');
            }

            return item.displayName + extension;
        }

        private static string NormalizeTargetSubdirectory(string input)
        {
            if (string.IsNullOrWhiteSpace(input))
            {
                return string.Empty;
            }

            var normalized = input.Replace('\\', '/').Trim('/');
            if (normalized.Contains("..", StringComparison.Ordinal))
            {
                return null;
            }

            if (normalized.StartsWith("Assets/", StringComparison.OrdinalIgnoreCase))
            {
                normalized = normalized.Substring("Assets/".Length);
            }

            return normalized;
        }
    }
}
