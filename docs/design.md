# cpp_agent_harness design

## Purpose

`cpp_agent_harness` is a teaching-oriented C++ agent runtime. Its goal is to
make the important parts of an agent observable and replaceable: the core
loop, model adapters, context budgeting and compaction, tools, skills, MCP,
execution policy, and tracing.

The project favors clear module boundaries over production-scale feature
coverage. Each milestone must remain runnable and testable.

## Architecture

```text
CLI / Application
        |
        v
  AgentSession / AgentLoop
     |       |       |
     v       v       v
  Model   Context   Policy
  Client  Manager   Engine
                    |
                    v
               Tool Registry
                /    |    \
          Builtin   MCP   Command
                    Tools
```

The loop coordinates components but does not implement provider protocols,
tool behavior, context compaction, or process execution itself.

## Core loop

For every user turn the runtime:

1. Appends the user message to the canonical conversation log.
2. Checks the working-context budget and compacts eligible history.
3. Builds the provider-neutral model request.
4. Calls the model and records its response.
5. Returns a final answer, or validates and dispatches requested tools.
6. Appends each tool result and repeats until a terminal condition is reached.

Terminal conditions include a final model response, maximum steps, repeated
tool failures, cancellation, timeout, budget exhaustion, and policy denial.

## Context management

The canonical event/history log is distinct from the working context sent to
the model. Compaction therefore never destroys the source record.

Working context is ordered as follows:

1. System and project instructions.
2. Active skill instructions.
3. A checkpoint summary of compacted history.
4. Pinned facts and unresolved work.
5. Recent messages and active tool calls.
6. The current user request.

The initial deterministic compactor is intentionally simple. A later
model-backed compactor will emit a structured checkpoint containing user
requirements, decisions, unresolved tasks, important paths, and known
failures. It must preserve tool-call/result pairing and never turn unfinished
work into completed work.

## Tools and execution

All local and remote tools implement a common interface and publish a
definition plus parameter metadata. `ToolRegistry` validates names and routes
calls. Future versions add JSON Schema validation, asynchronous execution,
timeouts, cancellation, and output limits.

Command execution will use an argv-based process API rather than passing
concatenated text through a shell. A policy layer will classify operations as
allowed, approval-required, or denied based on tool, path, network access,
environment access, and destructive effects.

## Skills

A skill is a directory containing a `SKILL.md` file with frontmatter metadata
and instructions. Startup discovers only names and descriptions. Full
instructions and referenced resources are loaded when the skill is activated.
A skill never grants additional execution privileges; scripts still pass
through the tool and policy layers.

## MCP

The first MCP implementation will be a stdio JSON-RPC client with process
lifecycle management, request IDs, initialization, tool discovery, tool
calls, timeouts, cancellation, and errors. MCP tools will be adapted to the
same local tool interface so the agent loop remains transport-agnostic.

Streamable HTTP, OAuth, resources, prompts, and subscriptions follow after the
stdio tool path is stable.

## Milestones

- M0: provider-neutral types, loop, deterministic context compaction, fake
  model, tool registry, calculator tool, event callback, CLI, and tests.
- M1: OpenAI-compatible model adapter, SSE streaming, tool-call assembly, and
  configuration.
- M2: file/search/patch/command tools, policy decisions, approval, timeout,
  cancellation, and bounded output.
- M3: structured model-backed context checkpoints and history replay.
- M4: skill selection, activation, referenced resources, and injection.
- M5: MCP stdio client and tool adapter.
- M6: SQLite sessions, trace viewer, metrics, record/replay, and fault tests.

## Engineering constraints

- C++20, with provider and transport libraries kept behind interfaces.
- Append-only source history; working context is derived state.
- No hidden tool execution: every transition produces an event.
- New adapters do not require changes to the core loop.
- Every milestone includes deterministic tests without network access.

