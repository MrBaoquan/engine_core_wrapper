using System;
using System.Collections.Generic;
using UnityEngine;

namespace EngineWorkflowBridge.Protocol
{
    internal static class JsonHelper
    {
        [Serializable]
        private class Wrapper<T>
        {
            public List<T> items;
        }

        public static string ToJson<T>(T instance, bool prettyPrint = false)
        {
            return JsonUtility.ToJson(instance, prettyPrint);
        }

        public static T FromJson<T>(string json)
        {
            return JsonUtility.FromJson<T>(json);
        }
    }
}
