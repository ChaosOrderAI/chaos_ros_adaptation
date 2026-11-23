#include "chaos_agent/llama_client.hpp"

#include <curl/curl.h>
#include <utility>

namespace chaos_agent
{

namespace
{

size_t write_cb(char * ptr, size_t size, size_t nmemb, void * userdata)
{
  auto * body = static_cast<std::string *>(userdata);
  body->append(ptr, size * nmemb);
  return size * nmemb;
}

}  // namespace

LlamaClient::LlamaClient(std::string endpoint, std::chrono::milliseconds timeout)
: endpoint_(std::move(endpoint)), timeout_(timeout)
{
  if (!endpoint_.empty() && endpoint_.back() == '/') {
    endpoint_.pop_back();
  }
}

LlamaClient::Response LlamaClient::chat(
  const std::string & system_prompt,
  const nlohmann::json & user_content,
  const nlohmann::json & tools,
  const nlohmann::json & extra)
{
  Response response;
  const auto started = std::chrono::steady_clock::now();

  nlohmann::json request = {
    {"model", "gemma-4-e4b"},
    {"messages", nlohmann::json::array({
        {{"role", "system"}, {"content", system_prompt}},
        // Chat-completions requires content to be a string. The planner snapshot
        // is an object; an object is rejected by a spec-compliant server.
        {{"role", "user"}, {"content", user_content.is_string() ?
            user_content.get<std::string>() : user_content.dump()}},
      })},
    {"stream", false},
  };
  if (!tools.empty()) {
    request["tools"] = tools;
    request["tool_choice"] = "auto";
  }
  for (auto it = extra.begin(); it != extra.end(); ++it) {
    request[it.key()] = it.value();
  }

  CURL * curl = curl_easy_init();
  if (!curl) {
    response.code = "transport.init_failed";
    response.message = "curl_easy_init returned null";
    return response;
  }
  std::string body;
  const std::string payload = request.dump();
  const std::string url = endpoint_ + "/v1/chat/completions";

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.data());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_.count()));
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

  struct curl_slist * headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  const CURLcode rc = curl_easy_perform(curl);
  long http_status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  response.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - started);

  if (rc != CURLE_OK) {
    response.code = "transport.request_failed";
    response.message = curl_easy_strerror(rc);
    return response;
  }
  if (http_status != 200) {
    response.code = "transport.http_" + std::to_string(http_status);
    response.message = body;
    return response;
  }

  nlohmann::json doc;
  try {
    doc = nlohmann::json::parse(body);
  } catch (const std::exception & e) {
    response.code = "response.invalid_json";
    response.message = e.what();
    return response;
  }

  const auto & choices = doc.value("choices", nlohmann::json::array());
  if (choices.empty()) {
    response.code = "response.no_choices";
    response.message = "chat completion returned no choices";
    return response;
  }
  const auto & message = choices.front().value("message", nlohmann::json::object());
  if (message.contains("content") && message["content"].is_string()) {
    response.content = message["content"].get<std::string>();
  }
  if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
    for (const auto & call : message["tool_calls"]) {
      ToolCall tc;
      const auto & function = call.value("function", nlohmann::json::object());
      tc.name = function.value("name", "");
      tc.raw_arguments = function.value("arguments", "");
      if (!tc.raw_arguments.empty()) {
        try {
          tc.arguments = nlohmann::json::parse(tc.raw_arguments);
          if (!tc.arguments.is_object()) {
            tc.arguments = nlohmann::json::object();
            tc.arguments_valid = false;
          }
        } catch (const std::exception &) {
          tc.arguments = nlohmann::json::object();
          tc.arguments_valid = false;
        }
      }
      if (!tc.name.empty()) {
        response.tool_calls.push_back(std::move(tc));
      }
    }
  }
  response.ok = true;
  return response;
}

}  // namespace chaos_agent
