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

The M1 protocol layer under development adds:

- JSON-valued tool schemas and arguments.
- A provider-neutral HTTP transport boundary.
- OpenAI Responses API request and response encoding.
- SSE framing across arbitrary network chunk boundaries.
- Streaming text and function-call argument assembly.
- Deterministic protocol tests using a fake HTTP transport.
- JSON configuration with environment-variable overrides.
- A real curl-based HTTP transport for Responses API endpoints.
- A reserved provider boundary for future local-model protocols.

See [docs/design.md](docs/design.md) for the architecture and roadmap.
See [docs/configuration.md](docs/configuration.md) for startup configuration.

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

On the first interactive start, the CLI checks for API configuration and opens
a setup wizard when none is found. Non-secret settings can be saved to
`config/agent.local.json`; API keys are never written there.

Start the offline demo explicitly:

```sh
./cpp-agent --demo
```

Try a normal message or exercise the tool loop:

```text
> calc 21 * 2
assistant: The tool returned: 42
```

Responses API output is streamed to the terminal by default. Built-in commands
are handled locally; use `/help`, `/model`, `/skills`, `/config`, or `/quit`.
`/model MODEL_ID` switches the model for the current session without rewriting
the configuration file.

Use `./cpp-agent --demo --trace` to print loop and tool events in Demo mode.

## API configuration

Copy the example, select `responses_api`, set the model, and keep the API key
in an environment variable:

```sh
cp config/agent.example.json config/agent.local.json
export OPENAI_API_KEY='your-api-key'
./cpp-agent --config config/agent.local.json
```

Validate configuration without accessing the network:

```sh
./cpp-agent --config config/agent.local.json --check-config
```

A local server that implements the Responses API can be selected with a
localhost `api.base_url` and `api.require_api_key: false`. The separate
`local` provider is reserved for a future protocol and is not executable yet.

## Status

This is the M0 foundation, not a production sandbox. Model-provider adapters,
safe command execution, structured compaction, and MCP are tracked as the next
milestones in the design document.

The Responses API codec and transport follow the official API reference:
<https://developers.openai.com/api/reference/resources/responses/methods/create>.
The network transport remains separate from the codec so protocol tests never
require an external API key.
