using System;
using System.Collections.Concurrent;
using System.Threading;
using UnityEditor;

namespace EngineWorkflowBridge
{
    [InitializeOnLoad]
    internal static class UnityMainThreadDispatcher
    {
        private static readonly ConcurrentQueue<Action> PendingActions =
            new ConcurrentQueue<Action>();
        private static readonly int MainThreadId;
        private static readonly TimeSpan DispatchTimeout = TimeSpan.FromSeconds(25);

        static UnityMainThreadDispatcher()
        {
            MainThreadId = Thread.CurrentThread.ManagedThreadId;
            EditorApplication.update += Flush;
        }

        public static T Run<T>(Func<T> action)
        {
            if (action == null)
            {
                throw new ArgumentNullException(nameof(action));
            }

            if (Thread.CurrentThread.ManagedThreadId == MainThreadId)
            {
                return action();
            }

            using (var waitHandle = new ManualResetEventSlim(false))
            {
                T result = default;
                Exception exception = null;
                var completed = 0;

                PendingActions.Enqueue(() =>
                {
                    if (Interlocked.CompareExchange(ref completed, 1, 0) != 0)
                    {
                        return;
                    }

                    try
                    {
                        result = action();
                    }
                    catch (Exception ex)
                    {
                        exception = ex;
                    }
                    finally
                    {
                        waitHandle.Set();
                    }
                });

                if (!waitHandle.Wait(DispatchTimeout))
                {
                    Interlocked.Exchange(ref completed, 1);
                    throw new TimeoutException("Timed out waiting for the Unity editor main thread.");
                }

                if (exception != null)
                {
                    throw exception;
                }

                return result;
            }
        }

        public static void Post(Action action)
        {
            if (action == null)
            {
                throw new ArgumentNullException(nameof(action));
            }

            PendingActions.Enqueue(() =>
            {
                try
                {
                    UnityBridgeLog.Info("Running posted main-thread action.");
                    action();
                }
                catch (Exception ex)
                {
                    UnityBridgeLog.Error("Main-thread action failed: " + ex);
                    UnityEngine.Debug.LogError($"[EngineWorkflowBridge] Main thread action failed: {ex}");
                }
            });
        }

        private static void Flush()
        {
            while (PendingActions.TryDequeue(out var action))
            {
                action();
            }
        }
    }
}
