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

                PendingActions.Enqueue(() =>
                {
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

                waitHandle.Wait();
                if (exception != null)
                {
                    throw exception;
                }

                return result;
            }
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
