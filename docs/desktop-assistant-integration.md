# 桌面助理快速对接说明

这份文档只讲一件事：桌面助理应该怎么接入 Unity/UE 插件。

如果你不关心插件内部实现，只需要按下面 3 步对接即可。

## 一句话说明

每个已打开的 Unity 或 Unreal 工程，都会在本机启动一个 HTTP 服务，并在固定目录写一个发现文件。

桌面助理的职责只有 3 个：

1. 读取发现文件，拿到当前已打开的工程列表
2. 选择目标工程
3. 调用目标工程的 HTTP 接口发起导入

## 对接流程

### 第 1 步：扫描已打开工程

扫描目录：

```text
%LOCALAPPDATA%/EngineWorkflowBridge/Sessions/
```

目录下每个 `*.json` 文件代表一个已打开的工程会话。

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

桌面助理至少要读取这些字段：

- `projectId`：工程唯一 ID，后续用于界面显示和内部缓存
- `engineType`：引擎类型，只会是 `unity` 或 `unreal`
- `projectName`：工程名
- `projectPath`：工程路径
- `endpoint`：插件开放的 HTTP 地址
- `capabilities`：当前支持的能力，当前只保证 `import.audio`
- `status`：当前状态，建议只在 `idle` 时发起导入
- `lastUpdatedUtc`：最后更新时间，用于判断会话是否过期

### 第 2 步：选择目标工程

桌面助理把扫描到的会话展示给用户即可。

建议展示字段：

- 工程名：`projectName`
- 引擎类型：`engineType`
- 工程路径：`projectPath`
- 状态：`status`

如果同一时刻打开了多个工程，桌面助理应该让用户显式选择目标工程，而不是自动猜测。

### 第 3 步：调用目标工程接口

拿到目标工程的 `endpoint` 后，直接发 HTTP 请求。

当前只需要接入 3 个接口。

## 接口一：健康检查

### 请求

```http
GET /api/v1/health
```

### 完整地址示例

```text
http://127.0.0.1:38241/api/v1/health
```

### 成功响应

```json
{
  "ok": true,
  "protocolVersion": "1.0",
  "engineType": "unity"
}
```

### 用途

- 判断插件是否在线
- 判断端口是否可访问
- 判断当前工程属于 Unity 还是 Unreal

## 接口二：获取当前工程信息

### 请求

```http
GET /api/v1/session
```

### 完整地址示例

```text
http://127.0.0.1:38241/api/v1/session
```

### 成功响应

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

### 用途

- 二次确认当前工程信息
- 判断目标工程是否支持某个能力
- 在导入前确认工程状态是否可用

## 接口三：导入资源

### 请求

```http
POST /api/v1/import-assets
Content-Type: application/json
```

### 请求体

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
    }
  ]
}
```

### 字段说明

- `requestId`：本次请求唯一 ID，建议使用 UUID
- `overwrite`：是否允许覆盖同名资源
- `assets`：要导入的资源数组，当前至少传 1 条

单条资源字段说明：

- `sourcePath`：本机资源绝对路径
- `assetType`：当前固定传 `audio`
- `targetSubdirectory`：导入目标子目录，不要带 `..`
- `displayName`：导入后的资源名，不带扩展名

### 成功响应

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
    }
  ]
}
```

### 响应字段说明

- `success`：整批请求是否全部成功
- `results`：每条资源的处理结果

单条结果字段说明：

- `sourcePath`：原始路径
- `status`：`imported`、`skipped`、`failed`
- `engineAssetPath`：导入后的引擎资源路径
- `engineObjectId`：引擎内部对象标识
- `message`：结果说明

## 桌面助理最小接入方案

如果桌面助理只做 MVP，对接逻辑可以压缩成下面这个流程：

1. 扫描 `%LOCALAPPDATA%/EngineWorkflowBridge/Sessions/*.json`
2. 读取所有会话，展示 `projectName + engineType + projectPath`
3. 用户选中一个工程
4. 调用 `GET {endpoint}/api/v1/health`
5. 调用 `POST {endpoint}/api/v1/import-assets`
6. 把 `results` 展示给用户

## 直接可抄的伪代码

```text
sessions = read all json files from %LOCALAPPDATA%/EngineWorkflowBridge/Sessions/
aliveSessions = filter sessions by lastUpdatedUtc and endpoint not empty
selected = user choose one session

health = GET selected.endpoint + "/api/v1/health"
if health.ok != true:
    show error
    stop

body = {
  requestId: uuid,
  overwrite: true,
  assets: [
    {
      sourcePath: "D:/workflow/output/bgm_main.wav",
      assetType: "audio",
      targetSubdirectory: "Audio/BGM",
      displayName: "bgm_main"
    }
  ]
}

result = POST selected.endpoint + "/api/v1/import-assets" with json body
show result.results
```

## PowerShell 调用示例

### 读取会话列表

```powershell
$dir = Join-Path $env:LOCALAPPDATA 'EngineWorkflowBridge\Sessions'
Get-ChildItem $dir -Filter *.json | ForEach-Object {
  Get-Content $_.FullName -Raw | ConvertFrom-Json
}
```

### 调用导入接口

```powershell
$endpoint = 'http://127.0.0.1:38241'

$body = @{
  requestId = [guid]::NewGuid().ToString()
  overwrite = $true
  assets = @(
    @{
      sourcePath = 'D:/workflow/output/bgm_main.wav'
      assetType = 'audio'
      targetSubdirectory = 'Audio/BGM'
      displayName = 'bgm_main'
    }
  )
} | ConvertTo-Json -Depth 5

Invoke-RestMethod -Uri ($endpoint + '/api/v1/import-assets') -Method Post -ContentType 'application/json' -Body $body
```

## 桌面助理需要遵守的规则

1. 不要自己写入 Unity 或 Unreal 工程目录，必须通过插件接口导入
2. 不要猜引擎类型，以发现文件中的 `engineType` 为准
3. 不要把 `targetSubdirectory` 传成绝对路径
4. 不要传 `..`，避免越界路径
5. 不要假设所有工程都支持所有能力，先看 `capabilities`
6. 导入前最好先调用一次 `health`
7. 如果 `status` 不是 `idle`，建议先提示用户，而不是强行导入

## 当前 MVP 限制

当前版本只保证以下能力：

- 查询会话
- 查询工程信息
- 导入音频资源

当前不保证：

- 纹理导入
- 模型导入
- 进度推送
- 导入任务队列
- 回调通知

## 相关文档

- 详细协议说明见 [docs/engine-plugin-protocol.md](engine-plugin-protocol.md)
- 安装说明见 [docs/setup-and-usage.md](setup-and-usage.md)