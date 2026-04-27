using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using EngineWorkflowBridge.Protocol;
using UnityEditor;
using UnityEngine;

namespace EngineWorkflowBridge
{
    [InitializeOnLoad]
    internal static class UnityBridgeServer
    {
        private static HttpListener _listener;
        private static CancellationTokenSource _cancellation;
        private static UnitySessionRegistry _registry;
        private static string _projectId;
        private static int _port;
        private static volatile string _status = "idle";

        static UnityBridgeServer()
        {
            Start();
            AssemblyReloadEvents.beforeAssemblyReload += Stop;
            EditorApplication.quitting += Stop;
        }

        private static void Start()
        {
            if (_listener != null)
            {
                return;
            }

            _projectId = UnityBridgePaths.MakeProjectId();
            _port = BindAvailablePort();
            _listener = new HttpListener();
            _listener.Prefixes.Add($"http://127.0.0.1:{_port}/");
            _listener.Start();
            _cancellation = new CancellationTokenSource();
            _registry = new UnitySessionRegistry(_projectId, $"http://127.0.0.1:{_port}");
            _registry.Write(_status);
            Task.Run(() => ListenLoop(_cancellation.Token));
            Debug.Log($"[EngineWorkflowBridge] Listening on port {_port}");
        }

        private static void Stop()
        {
            try
            {
                _cancellation?.Cancel();
                _listener?.Stop();
                _listener?.Close();
                _registry?.Delete();
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[EngineWorkflowBridge] Shutdown warning: {ex.Message}");
            }
            finally
            {
                _listener = null;
                _cancellation = null;
                _registry = null;
            }
        }

        private static async Task ListenLoop(CancellationToken cancellationToken)
        {
            while (
                !cancellationToken.IsCancellationRequested
                && _listener != null
                && _listener.IsListening
            )
            {
                HttpListenerContext context;
                try
                {
                    context = await _listener.GetContextAsync();
                }
                catch (Exception)
                {
                    break;
                }

                _ = Task.Run(() => HandleRequest(context), cancellationToken);
            }
        }

        private static void HandleRequest(HttpListenerContext context)
        {
            try
            {
                var path = context.Request.Url?.AbsolutePath ?? string.Empty;
                if (context.Request.HttpMethod == "GET" && path == "/api/v1/health")
                {
                    WriteJson(context.Response, 200, JsonUtility.ToJson(new HealthResponse()));
                    return;
                }

                if (context.Request.HttpMethod == "GET" && path == "/api/v1/session")
                {
                    WriteJson(
                        context.Response,
                        200,
                        JsonUtility.ToJson(_registry.BuildSession(_status))
                    );
                    return;
                }

                if (context.Request.HttpMethod == "POST" && path == "/api/v1/import-assets")
                {
                    string payload;
                    using (
                        var reader = new StreamReader(
                            context.Request.InputStream,
                            context.Request.ContentEncoding ?? Encoding.UTF8
                        )
                    )
                    {
                        payload = reader.ReadToEnd();
                    }

                    var request = JsonUtility.FromJson<ImportAssetsRequest>(payload);
                    if (request == null || request.assets == null || request.assets.Count == 0)
                    {
                        WriteJson(context.Response, 400, "{\"error\":\"Invalid request payload\"}");
                        return;
                    }

                    _status = "busy";
                    _registry.Write(_status);
                    var response = new ImportAssetsResponse
                    {
                        requestId = request.requestId,
                        success = true
                    };

                    foreach (var asset in request.assets)
                    {
                        ImportAssetResult result;
                        try
                        {
                            if (
                                !string.Equals(
                                    asset.assetType,
                                    "audio",
                                    StringComparison.OrdinalIgnoreCase
                                )
                            )
                            {
                                result = new ImportAssetResult
                                {
                                    sourcePath = asset.sourcePath,
                                    status = "failed",
                                    message = "Unsupported assetType"
                                };
                            }
                            else
                            {
                                result = UnityMainThreadDispatcher.Run(
                                    () => UnityAssetImporter.ImportAudio(asset, request.overwrite)
                                );
                            }
                        }
                        catch (Exception ex)
                        {
                            result = new ImportAssetResult
                            {
                                sourcePath = asset.sourcePath,
                                status = "failed",
                                message = ex.Message
                            };
                        }

                        if (result.status == "failed")
                        {
                            response.success = false;
                        }

                        response.results.Add(result);
                    }

                    _status = response.success ? "idle" : "error";
                    _registry.Write(_status);
                    WriteJson(context.Response, 200, JsonUtility.ToJson(response));
                    return;
                }

                WriteJson(context.Response, 404, "{\"error\":\"Not found\"}");
            }
            catch (Exception ex)
            {
                Debug.LogError($"[EngineWorkflowBridge] Request handling failed: {ex}");
                try
                {
                    WriteJson(context.Response, 500, "{\"error\":\"Internal server error\"}");
                }
                catch { }
            }
        }

        private static int BindAvailablePort()
        {
            for (var port = 38240; port < 38340; port++)
            {
                try
                {
                    var probe = new TcpListener(IPAddress.Loopback, port);
                    probe.Start();
                    probe.Stop();
                    return port;
                }
                catch { }
            }

            throw new InvalidOperationException(
                "Could not bind a local port for EngineWorkflowBridge."
            );
        }

        private static void WriteJson(HttpListenerResponse response, int statusCode, string payload)
        {
            var buffer = Encoding.UTF8.GetBytes(payload);
            response.StatusCode = statusCode;
            response.ContentType = "application/json";
            response.ContentEncoding = Encoding.UTF8;
            response.ContentLength64 = buffer.Length;
            using (var stream = response.OutputStream)
            {
                stream.Write(buffer, 0, buffer.Length);
            }
        }
    }
}
