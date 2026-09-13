# cpp_agent_harness 实现方案

> 状态：主设计文档
>
> 目标版本：从教学型 M0 原型逐步演进到完整的单 Agent Harness
>
> 最后更新：2026-09-12

## 1. 项目定位

`cpp_agent_harness` 是一个用于学习 Agent 工作原理的 C++20 实现。项目重点不是堆叠功能，而是把一个 Agent 从接收请求到完成任务的整个过程拆成可观察、可替换、可测试的模块：

- Agent 核心循环；
- 模型调用及流式响应；
- 消息、会话和 Context 管理；
- Token 预算与上下文压缩；
- Tool Calling 和本地指令执行；
- Skill 的发现、激活和资源加载；
- MCP Server 的连接与工具适配；
- 权限策略、审批、超时和取消；
- Trace、持久化和确定性回放。

项目采用“每个里程碑都可以独立运行和验证”的方式迭代。第一阶段聚焦单 Agent，不在核心模型稳定前引入多 Agent、GUI、分布式执行或复杂 RAG。

## 2. 范围

### 2.1 最终应具备的基础能力

- OpenAI-compatible 模型接口；
- 非流式和 SSE 流式模型调用；
- 文本响应和 Tool Call 增量拼装；
- 多轮 Agent Loop；
- 本地文件、搜索、Patch 和 Command Tool；
- Context Token 预算和自动压缩；
- 原始会话日志与工作上下文分离；
- `SKILL.md` 发现、匹配、激活与资源读取；
- MCP stdio 客户端和工具适配；
- 工具参数校验、超时、取消和输出限制；
- `Allow / RequireApproval / Deny` 执行策略；
- SQLite 会话持久化；
- 事件日志、Trace 和 Record/Replay；
- CLI 交互入口。

### 2.2 第一阶段不做

- 多 Agent 编排；
- 图形界面；
- 远程分布式执行器；
- 完整容器或虚拟机沙箱；
- 向量数据库与知识库检索；
- MCP Streamable HTTP 和 OAuth；
- 跨用户长期记忆。

这些能力可以在单 Agent Harness 的边界稳定后作为独立扩展加入。

## 3. 总体架构

```text
CLI / Host Application
          |
          v
     AgentSession
          |
          v
      AgentLoop <---------------- Cancellation / Budget
     /    |    \                            |
    v     v     v                           v
Prompt  Model  Context                  PolicyEngine
Builder Client Manager                      |
    |     |      |                          v
    |     |      |                     ApprovalGate
    |     |      |                          |
    +-----+------+--------------------------+
                       |
                       v
                  ToolRegistry
                 /     |      \
                v      v       v
          BuiltinTool McpTool CommandTool
                       |       |
                       v       v
                 McpManager ProcessRunner
                       |
                       v
                  JSON-RPC stdio

所有状态变化 ---------------------> EventLog / Trace / Persistence
```

核心约束：

1. `AgentLoop` 只负责编排，不直接实现 HTTP、MCP、文件或进程逻辑。
2. 本地 Tool 和 MCP Tool 都适配为统一的 `ITool`。
3. 原始会话是只追加的事实记录，工作 Context 是可以随时重建的派生状态。
4. 所有模型调用、压缩、审批和工具执行都产生事件，不允许隐式执行。
5. Provider、Transport 和 Storage 必须位于接口之后，更换实现不应修改核心 Loop。

## 4. 核心领域模型

### 4.1 Message

内部消息模型不直接使用任何一家模型供应商的数据结构：

```cpp
enum class Role {
    System,
    Developer,
    User,
    Assistant,
    Tool
};

struct Message {
    MessageId id;
    Role role;
    std::string content;
    std::vector<ToolCall> tool_calls;
    std::optional<ToolCallId> tool_call_id;
    MessageMetadata metadata;
    bool pinned{false};
};
```

Provider Adapter 负责把内部消息转换为具体 API 请求，并把具体 API 响应还原为 `ModelResponse`。

### 4.2 Session 与日志

建议区分以下对象：

- `AgentSession`：一次连续 Agent 会话的入口和生命周期；
- `ConversationLog`：只追加的完整消息记录；
- `EventLog`：模型请求、Tool 调用、压缩、审批等运行事件；
- `WorkingContext`：本轮真正发送给模型的消息集合；
- `ContextCheckpoint`：压缩历史生成的结构化检查点；
- `AgentConfig`：模型、预算、Loop、工具和策略配置。

Context 被压缩后，`ConversationLog` 和 `EventLog` 仍然保留原始记录，以支持审计、重放和重新生成摘要。

### 4.3 Tool Call

```cpp
struct ToolCall {
    ToolCallId id;
    std::string name;
    Json arguments;
};

struct ToolResult {
    ToolCallId call_id;
    bool ok;
    Json value;
    std::string display_text;
    ToolError error;
    ExecutionMetrics metrics;
};
```

M0 暂时使用字符串参数映射，接入模型和 MCP 时统一迁移到 JSON 值类型。

## 5. Agent 核心循环

### 5.1 每轮执行顺序

```text
接收用户输入
    |
    v
写入 ConversationLog
    |
    v
检查取消、步数、Token 和费用预算
    |
    v
必要时压缩 Context
    |
    v
PromptBuilder 组装 WorkingContext
    |
    v
ModelClient 请求模型
    |
    v
解析文本 / Tool Calls / Finish Reason
    |
    +---- 最终回答 --------------------------> 完成
    |
    v
校验 Tool Call 参数
    |
    v
PolicyEngine: Allow / Approval / Deny
    |
    v
ToolRegistry 执行工具
    |
    v
写入 ToolResult 和运行事件
    |
    +----------------------------------------> 下一轮
```

参考伪代码：

```cpp
while (!session.finished()) {
    budgets.check(session);

    if (context.should_compact()) {
        context.compact(session.history());
    }

    const auto request = prompt_builder.build(
        session, context, active_skills, tools.definitions());
    const auto response = co_await model.generate(request, cancellation);
    session.append(response);

    if (response.is_final()) {
        return response.text;
    }

    for (const auto& call : response.tool_calls()) {
        schema_validator.validate(call);
        auto decision = policy.evaluate(call, execution_context);

        if (decision.requires_approval()) {
            decision = co_await approval.request(call, decision.reason());
        }

        auto result = co_await tools.execute(call, decision, cancellation);
        session.append(result);
    }
}
```

### 5.2 显式状态机

Loop 应逐步演进为显式状态机，避免所有逻辑堆在一个 `while` 中：

```cpp
enum class LoopState {
    AcceptInput,
    CheckBudget,
    CompactContext,
    BuildRequest,
    RequestModel,
    HandleResponse,
    AwaitApproval,
    ExecuteTools,
    Finished,
    Failed,
    Cancelled
};
```

状态转换产生 `AgentEvent`。这样可以暂停审批、恢复会话、观察运行过程，并针对单个状态编写测试。

### 5.3 终止条件

- 模型返回最终回答；
- 达到最大 Loop 步数；
- 超过 Token 或费用预算；
- Tool 连续失败达到阈值；
- 模型连续返回无效响应；
- 用户取消；
- 模型或 Tool 执行超时；
- Policy 拒绝且无法继续；
- Harness 内部错误。

每种终止必须返回结构化原因，不能只返回一个布尔值。

## 6. Context 管理与压缩

### 6.1 两层存储

```text
Canonical History                    Working Context
完整且只追加                         每轮动态生成
不因压缩而删除                       受模型窗口限制
用于恢复、审计和重放                 允许摘要、裁剪和重排
```

不得直接删除历史消息来实现压缩。压缩只改变下一次模型请求使用的 `WorkingContext`。

### 6.2 Working Context 顺序

1. System 指令；
2. Developer、项目和工作区指令；
3. 当前激活的 Skill 指令；
4. 最近一次有效的结构化摘要；
5. 固定保留的用户约束、事实和未完成事项；
6. 最近 N 轮原始消息；
7. 未闭合的 Tool Call 与 Tool Result；
8. 当前用户请求。

### 6.3 Token 预算策略

初始默认值：

- Context 达到模型窗口的 70% 时触发压缩；
- 压缩后的目标占用为 40%～50%；
- 最近 4～8 轮不压缩；
- 给模型输出预留固定 Token；
- Tool 输出设置单条和累计上限；
- 超长 Tool 输出先独立裁剪或摘要，再参与会话压缩。

M0 使用 `字符数 / 4` 的确定性估算。M1 接入 Provider 后增加可替换的 `ITokenEstimator`，允许按模型使用真实 tokenizer。

### 6.4 压缩流程

```text
选择可压缩消息
      |
      v
保护指令、最近消息、未闭合 Tool Call 和 pinned 项
      |
      v
提取用户要求 / 决策 / 事实 / 未完成事项 / 文件状态
      |
      v
生成 ContextCheckpoint
      |
      v
校验必要信息与 Tool 配对
      |
      v
原子替换当前有效 Checkpoint
```

结构化摘要：

```cpp
struct ContextCheckpoint {
    std::string narrative;
    std::vector<std::string> user_requirements;
    std::vector<std::string> decisions;
    std::vector<std::string> unresolved_tasks;
    std::vector<std::string> important_paths;
    std::vector<std::string> known_failures;
    MessageId compacted_through;
    std::vector<MessageId> source_messages;
};
```

### 6.5 压缩不变量

- System、Developer 和用户硬性要求不能丢失；
- Tool Call 与 Tool Result 不能形成非法序列；
- 未完成任务不能被摘要成已完成；
- 摘要不能覆盖比它更新的事实；
- 路径、标识符、错误码和重要配置要精确保留；
- 压缩模型失败时仍能使用确定性降级策略；
- 每个 Checkpoint 可以追溯到原始消息范围。

## 7. 模型接口

```cpp
class IModelClient {
public:
    virtual ~IModelClient() = default;

    virtual Task<ModelResponse> generate(
        const ModelRequest& request,
        CancellationToken cancellation) = 0;

    virtual AsyncStream<ModelStreamEvent> stream(
        const ModelRequest& request,
        CancellationToken cancellation) = 0;
};
```

OpenAI-compatible Adapter 负责：

- 请求 JSON 编码；
- API Key 和 endpoint 配置；
- HTTP 状态和错误映射；
- SSE 分帧；
- UTF-8 和跨 chunk 内容拼接；
- 多个 Tool Call 的 index/id 增量组装；
- finish reason 处理；
- usage、Token 和耗时统计；
- 可重试错误分类和退避。

模型客户端不直接执行 Tool，也不决定是否压缩 Context。

## 8. Tool 系统

### 8.1 统一接口

```cpp
class ITool {
public:
    virtual ~ITool() = default;
    virtual ToolDefinition definition() const = 0;

    virtual Task<ToolResult> execute(
        const Json& arguments,
        ExecutionContext& context,
        CancellationToken cancellation) = 0;
};
```

`ToolRegistry` 负责：

- 注册和名称冲突检查；
- 向模型暴露 Tool Definition；
- JSON Schema 参数校验；
- 本地 Tool 与 MCP Tool 路由；
- 超时和取消传播；
- Tool 结果规范化；
- 单次和累计输出大小限制；
- 统一事件记录。

### 8.2 首批内建工具

- `read_file`：按范围读取文本；
- `list_files`：受限目录枚举；
- `search_text`：优先调用 `rg`；
- `apply_patch`：局部、安全地修改文件；
- `run_command`：执行 argv 形式的子进程；
- `get_current_time`：演示无副作用工具；
- `calculator`：测试最小 Tool Loop。

文件修改优先使用 Patch，不允许模型默认覆盖整个文件。

## 9. 命令与指令执行

禁止把未验证字符串直接传给 `std::system()`。基础接口：

```cpp
struct CommandRequest {
    std::string program;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::chrono::milliseconds timeout;
    std::map<std::string, std::string> environment;
    std::size_t max_stdout_bytes;
    std::size_t max_stderr_bytes;
};

struct CommandResult {
    int exit_code;
    std::string stdout_text;
    std::string stderr_text;
    bool timed_out;
    bool cancelled;
    bool output_truncated;
};
```

Linux 第一版使用 `posix_spawn` 或隔离后的 Boost.Process 实现：

- 默认不经过 `/bin/sh -c`；
- 程序和 argv 分开传递；
- 工作目录必须解析和校验；
- 环境变量使用允许列表；
- stdout 和 stderr 分流、限长并支持增量事件；
- 使用进程组实现超时和取消；
- 记录启动时间、结束时间和退出原因；
- Shell 语法作为单独且高风险的 Tool 提供。

### 9.1 Policy Engine

```cpp
enum class PolicyDecision {
    Allow,
    RequireApproval,
    Deny
};
```

策略检查维度：

- Tool 名称和声明的副作用；
- 读、写或删除；
- 目标路径及其真实路径；
- 是否越出工作区；
- 是否联网；
- 是否读取敏感环境变量；
- 命令和参数风险；
- 目标范围是否过宽；
- 当前用户是否已经批准匹配规则。

Skill、MCP Server 或模型输出都不能绕过 Policy Engine。

## 10. Skill 系统

### 10.1 目录格式

```text
skills/
└── cpp-review/
    ├── SKILL.md
    ├── references/
    │   └── checklist.md
    ├── scripts/
    │   └── run-clang-tidy.sh
    └── assets/
```

```yaml
---
name: cpp-review
description: Review C++ ownership, lifetime and concurrency problems.
---

# Instructions

1. Inspect ownership and lifetime.
2. Check error and cancellation paths.
3. Run focused tests when available.
```

### 10.2 生命周期

```text
启动时扫描 name + description
          |
          v
SkillMatcher 根据用户请求选择候选
          |
          v
SkillLoader 完整加载 SKILL.md
          |
          v
安全解析 references / scripts / assets
          |
          v
写入 ActiveSkillSet 并注入 Context
```

模块职责：

- `SkillRegistry`：发现、索引和重名检查；
- `SkillMatcher`：显式名称匹配和模型辅助选择；
- `SkillLoader`：完整加载指令及相对资源；
- `ActiveSkillSet`：维护当前会话激活状态；
- `PromptBuilder`：按指令优先级注入 Skill。

Skill 是指令和资源包，不是权限包。Skill 中的脚本必须通过 Tool、Policy 和 Approval 执行。

## 11. MCP

### 11.1 第一阶段协议范围

先实现 MCP stdio Client：

1. 根据配置启动 MCP Server 子进程；
2. 建立 stdin/stdout JSON-RPC Transport；
3. 分配和匹配请求 ID；
4. 发送 `initialize`；
5. 处理 Server capabilities；
6. 发送 initialized notification；
7. 调用 `tools/list`；
8. 将工具注册为 `McpToolAdapter`；
9. 调用 `tools/call`；
10. 处理错误、超时、取消和 Server 退出；
11. 正常关闭或按策略重启进程。

```text
MCP tools/list
      |
      v
McpToolAdapter implements ITool
      |
      v
ToolRegistry
      |
      v
AgentLoop
```

核心组件：

- `McpServerConfig`；
- `McpProcess`；
- `JsonRpcTransport`；
- `PendingRequestTable`；
- `McpClient`；
- `McpToolAdapter`；
- `McpManager`。

必须处理：

- stdout 半包、粘包和非法 JSON；
- stderr 与协议 stdout 隔离；
- 重复、未知和超时请求 ID；
- notification 没有 response；
- Server 意外退出；
- Tool schema 冲突；
- 大结果和二进制内容；
- Agent 取消向 MCP 请求传播。

stdio Tool 路径稳定后，再增加 Resources、Prompts、进度通知、订阅、Streamable HTTP 和 OAuth。

## 12. 异步、取消与并行

核心领域层定义自己的抽象，避免直接绑定某个异步库：

```cpp
template <typename T>
class Task;

class CancellationToken;

class IExecutor;
```

基础设施层可以先使用线程池，后续替换为 Boost.Asio。第一版默认串行执行 Tool Call，只有满足以下条件时才允许并行：

- Tool Definition 声明可并行；
- Tool 没有互相依赖；
- Policy 允许；
- 不会并发修改同一资源；
- 结果仍按原 Tool Call 顺序写回模型上下文。

取消必须从 CLI/Host 一直传播到 Model Client、Tool、ProcessRunner 和 MCP Client。

## 13. 可观察性与持久化

主要事件：

- `loop.started / loop.finished / loop.failed`；
- `context.compaction.started / completed / failed`；
- `model.requested / stream.delta / responded / failed`；
- `tool.requested / policy.decided / approval.requested`；
- `tool.started / output.delta / finished / failed`；
- `mcp.server.started / request / response / exited`；
- `session.checkpointed / restored`。

事件必须带有 session ID、step、时间、相关 call ID、耗时和结果状态。凭据及敏感 Tool 输出在落盘前进行脱敏。

SQLite 初始表：

- `sessions`；
- `messages`；
- `events`；
- `context_checkpoints`；
- `tool_executions`；
- `active_skills`。

Record/Replay 模式记录模型与外部 Tool 边界结果，使测试可以在不联网的情况下确定性重放一次 Agent 运行。

## 14. 推荐技术栈

- 语言：C++20；
- 构建：CMake，保留 Makefile 作为最小环境入口；
- JSON：`nlohmann/json`；
- HTTP：`libcurl`；
- 异步与 IO：领域层自定义接口，基础设施层逐步使用 Boost.Asio；
- 子进程：Linux `posix_spawn` 或 Boost.Process；
- 日志：`spdlog`；
- CLI：`CLI11`；
- 测试：Catch2 或 GoogleTest；
- 持久化：SQLite。

第三方库在需要其对应功能的里程碑才引入。M0 保持标准库零依赖，确保核心 Loop 可以直接学习和调试。

## 15. 目录规划

```text
cpp_agent_harness/
├── CMakeLists.txt
├── Makefile
├── apps/
│   └── agent_cli/
├── include/agent/
│   ├── core/
│   ├── context/
│   ├── model/
│   ├── tools/
│   ├── skills/
│   ├── mcp/
│   ├── policy/
│   ├── process/
│   └── tracing/
├── src/
│   ├── core/
│   ├── context/
│   ├── model/
│   ├── tools/
│   ├── skills/
│   ├── mcp/
│   ├── policy/
│   ├── process/
│   └── tracing/
├── skills/
├── config/
│   └── agent.example.yaml
├── docs/
├── tests/
│   ├── unit/
│   ├── integration/
│   └── fixtures/
└── examples/
```

M0 为减少样板代码暂时采用较浅目录；模块增长时按上述结构迁移。

## 16. 里程碑与验收标准

### M0：核心骨架——已完成

- Provider-neutral 数据结构；
- `IModel` 和 `ITool`；
- Agent Loop；
- 确定性 Context 压缩；
- Tool Registry；
- Calculator Tool；
- Skill frontmatter 扫描；
- Fake Model、CLI 和 Trace；
- 无网络测试。

验收：Fake Model 发出 Tool Call，Tool 执行后第二次模型调用返回最终答案；Context 超预算后保留指令和最近消息。

### M1：真实模型

- JSON 值和 JSON Schema；
- OpenAI-compatible Client；
- libcurl HTTP；
- SSE parser；
- Tool Call 增量拼装；
- endpoint、model、API Key 和超时配置；
- Fake Transport 和录制响应测试。

验收：真实模型能调用 Calculator Tool；录制的 SSE 数据可以无网络重放，任意 chunk 边界下解析结果一致。

### M2：本地执行工具

- `read_file / list_files / search_text / apply_patch`；
- argv-based ProcessRunner；
- `run_command`；
- Policy、Approval、超时、取消和输出限制；
- 工作区路径校验。

验收：Agent 可以修改示例工程并运行测试；越界读取和危险命令被拦截或要求批准。

### M3：完整 Context 管理

- `ITokenEstimator`；
- 结构化 `ContextCheckpoint`；
- 模型压缩与确定性降级；
- pinned 事实和未完成任务；
- Tool 配对保护；
- 原始历史回放。

验收：长对话压缩后仍保留首轮约束、最新决策和未完成事项，且可由原始日志重新生成工作 Context。

### M4：完整 Skill

- Skill 匹配与显式激活；
- 完整指令注入；
- references、scripts 和 assets；
- 路径安全；
- ActiveSkillSet 事件和持久化。

验收：添加一个 Skill 无需重新编译；Skill 脚本仍受 Policy 管理。

### M5：MCP stdio

- JSON-RPC Transport；
- Server 生命周期；
- initialize；
- `tools/list` 和 `tools/call`；
- 超时、取消和进程退出；
- MCP Tool Adapter；
- 本地测试 MCP Server。

验收：连接示例 MCP Server，并由 Agent 像调用本地 Tool 一样发现和调用其工具。

### M6：可靠性与可观察性

- SQLite 会话；
- 恢复与 Checkpoint；
- Trace 查看；
- Token、费用、耗时和错误指标；
- Record/Replay；
- 故障注入测试。

验收：模型响应、Tool 结果和 MCP 消息可录制并确定性重放；程序异常退出后能恢复会话。

## 17. 关键测试场景

- 模型一次返回多个 Tool Call；
- 流式响应在任意字节位置断开；
- Tool 参数不是合法 JSON 或不满足 Schema；
- Tool 超时、取消或产生超大输出；
- 用户在模型或 Tool 执行期间取消；
- MCP Server 启动失败或执行中退出；
- JSON-RPC 收到未知、重复或超时 ID；
- Context 压缩恰好发生在 Tool Call 和 Tool Result 附近；
- 模型重复调用同一个失败 Tool；
- Skill 引用工作区之外的资源；
- 文件路径包含符号链接和 `..`；
- 命令尝试删除或覆盖关键目录；
- 压缩模型自身失败；
- 会话在写入消息或 Checkpoint 期间中断；
- Replay 结果与原运行事件序列不一致。

## 18. 当前实现状态

| 模块 | 状态 | 说明 |
|---|---|---|
| 核心 Loop | M0 可运行 | 串行 Tool Call、步数和连续错误限制 |
| Context | M0 可运行 | 字符估算和确定性文本摘要 |
| Tool Registry | M1 进行中 | 已迁移 JSON 参数与 Schema，完整校验待实现 |
| Calculator Tool | 可运行 | 用于验证最小闭环 |
| Skill Registry | M0 可运行 | 扫描 frontmatter，尚未注入 Loop |
| CLI / Trace | M0 可运行 | Fake Model 交互演示 |
| OpenAI Model | M1 可运行 | Responses Codec、SSE、配置和真实 HTTP Transport 已实现 |
| 文件与命令工具 | 未实现 | M2 |
| 结构化压缩 | 未实现 | M3 |
| Skill 激活 | 未实现 | M4 |
| MCP stdio | 未实现 | M5 |
| SQLite / Replay | 未实现 | M6 |

## 19. 近期实施顺序

下一轮从 M1 开始，推荐严格按以下顺序推进：

1. 引入统一 JSON 类型，迁移 Tool arguments；
2. 抽象 `IHttpTransport`，先实现 Fake Transport；
3. 实现非流式 OpenAI-compatible 请求；
4. 实现独立 SSE Parser 并进行 chunk 边界测试；
5. 实现 Tool Call 增量组装；
6. 接入真实 HTTP Transport；
7. 加入配置、凭据脱敏和错误分类；
8. 用真实模型跑通 Calculator Tool；
9. 录制响应作为后续离线回归 fixture。

在这一阶段结束前，不让 HTTP、JSON 或 Provider 细节渗入 `AgentLoop`。

## 20. 设计决策记录

- **C++20**：兼顾现代类型和协程扩展空间，同时保持工具链可获得性。
- **单 Agent 优先**：先把状态、工具、压缩和恢复做正确，多 Agent 只是更高层编排。
- **内部 Provider-neutral 类型**：防止核心逻辑绑定单一模型 API。
- **原始历史与 Working Context 分离**：压缩不破坏事实来源。
- **Skill 是指令包而不是插件权限**：所有副作用仍通过统一工具策略执行。
- **MCP Tool 适配为 ITool**：Agent Loop 不感知工具来源和传输协议。
- **stdio MCP 优先**：最容易观察 JSON-RPC 和子进程生命周期，适合学习。
- **argv 执行优先于 Shell**：避免字符串解释和注入成为默认行为。
- **事件驱动可观察性**：调试 Agent 的关键是能解释每一次状态转换。
- **零依赖 M0**：先验证领域边界，再在对应里程碑引入第三方库。
