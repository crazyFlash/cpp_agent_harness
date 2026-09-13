# 配置说明

## 启动流程

交互终端无参数启动时，程序按以下顺序检查：

1. `--config PATH`；
2. `CPP_AGENT_CONFIG`；
3. 默认文件 `config/agent.local.json`；
4. `CPP_AGENT_PROVIDER` 等纯环境变量配置；
5. 以上都不存在时进入 API 配置向导。

```bash
./cpp-agent
```

向导依次询问 API Base URL、模型、是否需要 API Key 以及 Key 所在的环境变量。如果环境变量中没有 Key，会关闭终端回显后读取一次，仅保留在当前进程内。保存配置时只写入非敏感设置。

也可以显式重新运行向导：

```bash
./cpp-agent --init-config
```

非交互环境不会等待输入；没有配置时会返回错误。离线 Demo 必须显式启动：

```bash
./cpp-agent --demo
```

## 配置来源与优先级

启动配置按以下顺序合并，后面的值覆盖前面的值：

1. 程序默认值；
2. `--config PATH` 或 `CPP_AGENT_CONFIG` 指定的 JSON 文件；
3. `CPP_AGENT_*` 环境变量；
4. `--trace` 或 `--no-trace` 命令行开关。

建议复制示例文件，并使用不会被 Git 跟踪的 `.local.json` 后缀：

```bash
cp config/agent.example.json config/agent.local.json
```

检查配置语法和字段：

```bash
./cpp-agent --config config/agent.local.json --check-config
```

配置检查不会访问网络，也不会要求 API Key。

## Provider

### Demo

```json
{
  "provider": "demo",
  "trace": true
}
```

Demo Provider 不访问网络，用于验证 Agent Loop 和 Calculator Tool。

### Responses API

```json
{
  "provider": "responses_api",
  "api": {
    "base_url": "https://api.openai.com/v1",
    "model": "your-model-id",
    "api_key_env": "OPENAI_API_KEY",
    "require_api_key": true,
    "timeout_ms": 60000,
    "store": false,
    "stream": true
  }
}
```

API Key 不需要写进配置文件：

```bash
export OPENAI_API_KEY='your-api-key'
./cpp-agent --config config/agent.local.json
```

也可以通过 `CPP_AGENT_API_KEY` 直接覆盖配置指定的环境变量。程序不会把 Key 打印到启动信息或 Trace 中。

### Responses-compatible 本地服务

如果本地服务实现了 Responses API，可以立即复用同一个 Provider：

```json
{
  "provider": "responses_api",
  "api": {
    "base_url": "http://127.0.0.1:8080/v1",
    "model": "local-model",
    "api_key_env": "",
    "require_api_key": false,
    "timeout_ms": 60000,
    "store": false,
    "stream": true
  }
}
```

Harness 会在 `base_url` 后补充 `/responses`。如果 `base_url` 已以 `/responses` 结尾，则不会重复追加。

### 未确定协议的本地模型

```json
{
  "provider": "local",
  "local": {
    "protocol": "reserved",
    "endpoint": "",
    "model": "",
    "options": {}
  }
}
```

该结构只预留未来适配边界。`options` 可以保存协议特有配置，但当前版本不会启动 `local` Provider。等协议确定后，将通过新的 `IModel` Adapter 实现，而不是修改 Agent Loop。

## 可用环境变量

| 环境变量 | 用途 |
|---|---|
| `CPP_AGENT_CONFIG` | 默认配置文件路径 |
| `CPP_AGENT_PROVIDER` | `demo`、`responses_api` 或 `local` |
| `CPP_AGENT_API_BASE_URL` | API Base URL |
| `CPP_AGENT_MODEL` | 模型名称 |
| `CPP_AGENT_API_KEY` | 直接提供 API Key，优先级最高 |
| `CPP_AGENT_API_KEY_ENV` | 指定保存 API Key 的环境变量名称 |
| `CPP_AGENT_API_REQUIRE_KEY` | `true/false` 或 `1/0` |
| `CPP_AGENT_API_TIMEOUT_MS` | 请求超时毫秒数 |
| `CPP_AGENT_API_STREAM` | 是否使用 SSE 流式响应，默认 `true` |
| `CPP_AGENT_TRACE` | 是否输出 Agent Trace |

## 交互式内置指令

内置指令在本地处理，不会发送给模型：

```text
/help
/status
/model [MODEL_ID]
/tools [TOOL_NAME]
/skills [SKILL_NAME]
/mcp
/context
/usage
/history
/compact
/clear
/new
/trace [on|off]
/stream [on|off]
/config
/quit
```

`/model MODEL_ID` 只修改当前进程中的 Responses API 模型，不会改写配置文件；
`/config` 只显示脱敏配置，不显示 API Key。

交互式终端支持 Tab 补全（指令、Skill 名称、Tool 名称和开关参数）、上下键历史、
UTF-8 退格和 Ctrl-D/Ctrl-C 退出。`/mcp` 当前展示 MCP 状态入口；stdio MCP
连接、工具发现和调用仍按设计文档的 M5 里程碑实现。

每轮模型调用后会显示 API 返回的 input/output/total token，以及本地 context
预算、消息数、摘要状态和 Loop steps。Context 数值带 `≈`，表示它来自当前
`字符数 / 4` 的确定性估算，不冒充 Provider tokenizer 的精确值。

## 其他配置

```json
{
  "skills_directory": "skills",
  "context": {
    "max_estimated_tokens": 4096,
    "compact_at_ratio": 0.7,
    "keep_recent_messages": 6
  },
  "loop": {
    "max_steps": 16,
    "max_consecutive_tool_errors": 3
  }
}
```

未知字段会被拒绝，避免拼写错误被静默忽略。

## HTTP Transport

当前实现通过系统 `curl` 可执行文件发送请求，因此运行真实 API 前需要确保：

```bash
curl --version
```

Transport 不调用 Shell。API Key 被写入权限为 `0600`、目录权限为 `0700` 的短生命周期临时配置，并在请求结束后清理；Key 不出现在子进程参数中。Transport 禁用用户级 `.curlrc`，并对 `localhost`、`127.0.0.1` 和 `::1` 默认绕过系统代理。
