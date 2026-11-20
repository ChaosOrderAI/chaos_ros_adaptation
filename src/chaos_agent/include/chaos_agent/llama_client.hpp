#pragma once

#include <chrono>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace chaos_agent
{

/// Minimal OpenAI-compatible chat client for a local llama.cpp llama-server.
///
/// The client posts a `/v1/chat/completions` request with a `tools` list and
/// returns the parsed `tool_calls` of the first choice. It is synchronous and
/// single-shot: the agent loop calls it once per skill boundary, so a slow
/// decision simply delays the next skill rather than blocking a pipeline.
///
/// The client does not own a connection pool: each call opens a new request
/// through libcurl. That matches the once-every-few-seconds cadence of skill
/// decisions and keeps failure handling simple (timeout -> error code).
class LlamaClient
{
public:
  struct ToolCall
  {
    /// The function name the model chose, e.g. "skill_navigate_to".
    std::string name;
    /// JSON arguments object; empty object when the call carries none.
    nlohmann::json arguments = nlohmann::json::object();
    /// The raw `arguments` string as returned by the server, for logging.
    std::string raw_arguments;
  };

  struct Response
  {
    bool ok = false;
    /// Machine-readable failure class when ok is false.
    std::string code;
    /// Human-readable server or transport error.
    std::string message;
    /// Tool calls of the first choice, in order. Empty when the model chose to
    /// answer with plain content instead of a tool call.
    std::vector<ToolCall> tool_calls;
    /// Plain content of the first choice, when present.
    std::string content;
    /// Wall-clock time the request took.
    std::chrono::milliseconds latency{0};
  };

  /// `endpoint` is the server root, e.g. "http://127.0.0.1:8000".
  explicit LlamaClient(
    std::string endpoint,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{5000});

  /// One chat completion with a tool list.
  ///
  /// `system_prompt` is sent as a system message; `user` as the user message;
  /// `tools` is the OpenAI tools array. Image/audio content blocks may be
  /// embedded in `user` through the `content` array form (see agent_loop).
  Response chat(
    const std::string & system_prompt,
    const nlohmann::json & user_content,
    const nlohmann::json & tools,
    const nlohmann::json & extra = nlohmann::json::object());

private:
  std::string endpoint_;
  std::chrono::milliseconds timeout_;
};

}  // namespace chaos_agent
