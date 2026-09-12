cmake_minimum_required(VERSION 3.20)

project(cpp_agent_harness VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(agent_harness
    src/agent_loop.cpp
    src/context_manager.cpp
    src/skill_registry.cpp
    src/tool_registry.cpp
    src/tools/calculator_tool.cpp
)

target_include_directories(agent_harness PUBLIC include)
target_compile_options(agent_harness PRIVATE -Wall -Wextra -Wpedantic)

add_executable(cpp-agent apps/agent_cli/main.cpp)
target_link_libraries(cpp-agent PRIVATE agent_harness)

enable_testing()
add_executable(agent-tests tests/agent_tests.cpp)
target_link_libraries(agent-tests PRIVATE agent_harness)
add_test(NAME agent-tests COMMAND agent-tests)

