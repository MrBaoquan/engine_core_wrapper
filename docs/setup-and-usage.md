# 安装与使用说明

## 仓库结构

- `unity/EngineWorkflowBridge`：Unity 包式 Editor 插件
- `ue/EngineWorkflowBridge`：Unreal Editor 插件
- `docs/engine-plugin-protocol.md`：协议文档
- `schemas/*.json`：主要请求与响应体的 JSON Schema

## Unity 安装方式

将 `unity/EngineWorkflowBridge` 作为本地包复制或引用到 Unity 工程中。

推荐在 `Packages/manifest.json` 中添加：

```json
{
  "dependencies": {
    "com.aicoreworks.engineworkflowbridge": "file:../engine_core_wrapper/unity/EngineWorkflowBridge"
  }
}
```

Unity 打开工程后的行为：

- 在 `38240-38339` 范围内选择一个空闲端口，启动本机 HTTP 监听
- 在 `%LOCALAPPDATA%/EngineWorkflowBridge/Sessions/` 下写入发现文件
- 提供 `GET /api/v1/health`
- 提供 `GET /api/v1/session`
- 提供 `POST /api/v1/import-assets`

导入后的音频资源默认落在 `Assets/WorkflowImports/...`。

## Unreal 安装方式

将 `ue/EngineWorkflowBridge` 复制到目标工程的 `Plugins/` 目录下。

目录结构应类似：

```text
YourProject/
  Plugins/
    EngineWorkflowBridge/
      EngineWorkflowBridge.uplugin
      Source/
```

如果插件未自动启用，请在编辑器中手动启用。

Unreal 打开工程后的行为：

- 在 `38240-38339` 范围内选择一个空闲端口，启动本机 HTTP 监听
- 在 `%LOCALAPPDATA%/EngineWorkflowBridge/Sessions/` 下写入发现文件
- 暴露与 Unity 一致的三个接口

导入后的音频资源默认落在 `/Game/WorkflowImports/...`。

## 调用示例

健康检查：

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:38241/api/v1/health
```

获取会话信息：

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:38241/api/v1/session
```

导入音频：

```powershell
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
} | ConvertTo-Json -Depth 4

Invoke-RestMethod -Uri http://127.0.0.1:38241/api/v1/import-assets -Method Post -ContentType 'application/json' -Body $body
```

## 当前限制

- 当前 MVP 版本只承诺音频导入能力
- 本仓库不包含桌面助手实现
- Unity 和 Unreal 的真实编译与运行验证仍需在本机实际引擎环境中完成

## 建议下一步

1. 在真实 Unity 工程中验证本地包能否正常加载与导入
2. 在真实 Unreal 工程中验证插件能否编译、启动并完成导入
3. 为后续纹理、FBX 等资源类型补充能力协商字段
