CXX ?= c++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2
CPPFLAGS ?= -Iinclude -Ithird_party

LIB_SOURCES := \
	src/agent_loop.cpp \
	src/context_manager.cpp \
	src/openai/responses_model.cpp \
	src/openai/responses_stream.cpp \
	src/openai/sse_parser.cpp \
	src/skill_registry.cpp \
	src/tool_registry.cpp \
	src/tools/calculator_tool.cpp

.PHONY: all test clean

all: cpp-agent

cpp-agent: $(LIB_SOURCES) apps/agent_cli/main.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

agent-tests: $(LIB_SOURCES) tests/agent_tests.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $^ -o $@

test: agent-tests
	./agent-tests

clean:
	$(RM) cpp-agent agent-tests
