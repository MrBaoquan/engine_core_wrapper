using System;
using System.IO;

namespace EngineWorkflowBridge
{
    internal static class UnityBridgeLog
    {
        private static readonly object SyncRoot = new object();

        private static string LogDirectory
        {
            get
            {
                var baseDirectory = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                return Path.Combine(baseDirectory, "EngineWorkflowBridge", "Logs");
            }
        }

        private static string LogPath
        {
            get { return Path.Combine(LogDirectory, "unity-bridge.log"); }
        }

        public static void Info(string message)
        {
            Write("INFO", message);
        }

        public static void Error(string message)
        {
            Write("ERROR", message);
        }

        private static void Write(string level, string message)
        {
            try
            {
                lock (SyncRoot)
                {
                    Directory.CreateDirectory(LogDirectory);
                    File.AppendAllText(LogPath, DateTime.Now.ToString("O") + " [" + level + "] " + message + Environment.NewLine);
                }
            }
            catch
            {
            }
        }
    }
}