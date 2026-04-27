# 引擎插件对接协议

## 目标

本协议由 Unity 和 Unreal Editor 插件共同实现，用于向外部调度方稳定暴露以下能力：

1. 识别当前打开的编辑器会话属于哪种引擎
2. 查询当前打开的工程及其能力信息
3. 将本地资源导入目标工程

本仓库不包含桌面助手实现，下面的协议内容就是双方的对接边界。

如果你是桌面助理开发者，优先阅读 [docs/desktop-assistant-integration.md](desktop-assistant-integration.md)。

## 传输方式

- 传输协议：`localhost HTTP`
- 编码格式：`UTF-8 JSON`
- 会话发现方式：每个插件都会在 `%LOCALAPPDATA%/EngineWorkflowBridge/Sessions/` 下写入一个发现文件
- 一个编辑器实例只暴露一个 HTTP 端点

## 发现文件

每个插件写入一个名为 `{projectId}.json` 的文件。

示例：

```json
{
  "protocolVersion": "1.0",
  "projectId": "unity-83f8a8d6c6b2",
  "engineType": "unity",
  "projectName": "AudioSandbox",
  "projectPath": "F:/Projects/AudioSandbox",
  "processId": 12048,
  "endpoint": "http://127.0.0.1:38241",
  "capabilities": ["import.audio"],
  "status": "idle",
  "lastUpdatedUtc": "2026-04-27T08:40:00Z"
}
```

生命周期规则：

- 插件启动时：创建或覆盖发现文件
- 心跳更新时：刷新 `status` 和 `lastUpdatedUtc`
- 编辑器关闭时：尽力删除发现文件
- 陈旧文件判定：如果 `lastUpdatedUtc` 早于调用方设定的超时时间，则应视为过期会话

## 引擎类型

允许值：

- `unity`
- `unreal`

引擎类型必须由插件自身声明。只要发现的是在线会话，调用方就不应再通过工程目录结构去猜测引擎类型。

## 通用接口

基础地址：发现文件中的 `endpoint` 字段。

### `GET /api/v1/health`

用途：健康检查。

响应：

```json
{
  "ok": true,
  "protocolVersion": "1.0",
  "engineType": "unity"
}
```

### `GET /api/v1/session`

用途：获取当前编辑器会话信息。

响应体结构见 [schemas/session-info.schema.json](../schemas/session-info.schema.json)。

### `POST /api/v1/import-assets`

用途：向当前工程导入一个或多个本地资源。

请求体结构见 [schemas/import-assets-request.schema.json](../schemas/import-assets-request.schema.json)。

响应体结构见 [schemas/import-assets-response.schema.json](../schemas/import-assets-response.schema.json)。

## 导入规则

### 允许的资源类型

当前 MVP 版本只保证音频导入：

- `.wav`
- `.mp3`
- `.ogg`
- `.flac`
- `.aiff`

### 路径规则

- `sourcePath` 必须是本机绝对路径
- `targetSubdirectory` 是相对工程导入根目录的逻辑子目录
- `targetSubdirectory` 不允许包含 `..`
- Unity 中的真实落点为 `Assets/WorkflowImports/{targetSubdirectory}`
- Unreal 中的逻辑落点为 `/Game/WorkflowImports/{targetSubdirectory}`

### 覆盖规则

- `overwrite = true`：若引擎支持覆盖导入，则替换已有文件或资源
- `overwrite = false`：如果目标资源已存在，则该条资源导入失败

### 结果规则

即使某条资源导入失败，`results` 中也必须返回对应结果项。

## 导入请求示例

```json
{
  "requestId": "c0b35c74-37aa-4b13-a8c2-3702361f4d8c",
  "overwrite": true,
  "assets": [
    {
      "sourcePath": "D:/workflow/output/bgm_main.wav",
      "assetType": "audio",
      "targetSubdirectory": "Audio/BGM",
      "displayName": "bgm_main"
    },
    {
      "sourcePath": "D:/workflow/output/click.ogg",
      "assetType": "audio",
      "targetSubdirectory": "Audio/UI",
      "displayName": "ui_click"
    }
  ]
}
```

## 导入响应示例

```json
{
  "requestId": "c0b35c74-37aa-4b13-a8c2-3702361f4d8c",
  "success": true,
  "results": [
    {
      "sourcePath": "D:/workflow/output/bgm_main.wav",
      "status": "imported",
      "engineAssetPath": "Assets/WorkflowImports/Audio/BGM/bgm_main.wav",
      "engineObjectId": "f0969668890445f4fbb0e7dbb322d2b5",
      "message": "Imported successfully"
    },
    {
      "sourcePath": "D:/workflow/output/click.ogg",
      "status": "imported",
      "engineAssetPath": "Assets/WorkflowImports/Audio/UI/ui_click.ogg",
      "engineObjectId": "4f07c2d2ab8d4e0ba8c9c5f68dc7dbba",
      "message": "Imported successfully"
    }
  ]
}
```

## 错误模型

建议的 HTTP 状态码约定：

- `200`：请求已处理，具体成功失败看每个结果项
- `400`：请求 JSON 非法，或逻辑路径不合法
- `404`：接口不存在
- `500`：插件内部异常

单条资源结果中的 `status` 允许值：

- `imported`
- `skipped`
- `failed`

## 引擎差异说明

### Unity

- 导入根目录为 `Assets/WorkflowImports`
- `engineAssetPath` 返回 Unity 资源路径
- `engineObjectId` 在可获取时返回 Unity GUID

### Unreal

- 导入根目录为 `/Game/WorkflowImports`
- `engineAssetPath` 返回 UE 资源对象路径
- `engineObjectId` 在可获取时返回导入后的 UObject 路径

## 版本管理

- 当前版本：`1.0`
- 任何破坏性变更都必须提升主版本，并同步更新文档与发现文件中的版本号
