# cpp_agent_harness

A small, teaching-oriented C++20 agent runtime built one observable layer at a
time.

The current M0 prototype includes:

- A provider-neutral agent loop.
- Message, model response, tool call, and event types.
- A tool registry and calculator tool.
- Context budgeting with deterministic history compaction.
- Filesystem skill discovery from `SKILL.md`.
- A fake model and interactive CLI.
- Dependency-free unit/integration tests.

See [docs/design.md](docs/design.md) for the architecture and roadmap.

## Build

With Make:

```sh
make
make test
```

With CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run

```sh
./cpp-agent
```

Try a normal message or exercise the tool loop:

```text
> calc 21 * 2
assistant: The tool returned: 42
```

Use `./cpp-agent --trace` to print loop and tool events.

## Status

This is the M0 foundation, not a production sandbox. Model-provider adapters,
safe command execution, structured compaction, and MCP are tracked as the next
milestones in the design document.
