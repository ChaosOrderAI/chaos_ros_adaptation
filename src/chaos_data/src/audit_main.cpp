// Native audit CLI: keeps the original entry point names (audit,
// data_audit) and the original command line.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "chaos_data/audit.hpp"
#include "chaos_data/file_io.hpp"

namespace {

void usage(const char * program)
{
  std::fprintf(stderr,
               "usage: %s <bag_path> [--storage-id mcap|sqlite3] "
               "[--max-difference-s 0.1] [--max-topics 256] [--max-messages N] "
               "[--index PATH] [--output PATH]\n",
               program);
}

// Strict numeric parsing: the whole token has to be consumed.
long long parseInteger(const std::string & token, const char * name)
{
  std::size_t consumed = 0;
  long long value = 0;
  try {
    value = std::stoll(token, &consumed);
  } catch (const std::exception &) {
    throw std::invalid_argument(std::string(name) + " must be an integer");
  }
  if (consumed != token.size()) {
    throw std::invalid_argument(std::string(name) + " must be an integer");
  }
  return value;
}

double parseDouble(const std::string & token, const char * name)
{
  std::size_t consumed = 0;
  double value = 0.0;
  try {
    value = std::stod(token, &consumed);
  } catch (const std::exception &) {
    throw std::invalid_argument(std::string(name) + " must be a number");
  }
  if (consumed != token.size()) {
    throw std::invalid_argument(std::string(name) + " must be a number");
  }
  return value;
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 2) {
    usage(argc > 0 ? argv[0] : "audit");
    return 2;
  }
  const std::string first = argv[1];
  if (first == "--help" || first == "-h") {
    usage(argv[0]);
    return 0;
  }
  const std::string bag_path = first;
  chaos::data::AuditOptions options;
  std::string output_path;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    auto next = [&](const char * name) -> std::string {
      if (index + 1 >= argc) {
        throw std::invalid_argument(std::string(name) + " needs a value");
      }
      return argv[++index];
    };
    try {
      if (argument == "--storage-id") {
        options.storage_id = next("--storage-id");
      } else if (argument == "--max-difference-s") {
        options.max_difference_s = parseDouble(next("--max-difference-s"),
                                               "--max-difference-s");
      } else if (argument == "--max-topics") {
        // The value is parsed as a 64 bit number and checked before it is
        // narrowed, so a value that does not fit into the 32 bit range is
        // refused instead of wrapping silently.
        const long long value =
          parseInteger(next("--max-topics"), "--max-topics");
        if (value < 1 || value > 2147483647LL) {
          throw std::invalid_argument(
            "--max-topics must be an integer in [1, 2147483647]");
        }
        options.max_topics = static_cast<int>(value);
      } else if (argument == "--max-messages") {
        options.max_messages = parseInteger(next("--max-messages"),
                                            "--max-messages");
      } else if (argument == "--index") {
        options.index_path = next("--index");
      } else if (argument == "--output") {
        output_path = next("--output");
      } else if (argument == "--help" || argument == "-h") {
        usage(argv[0]);
        return 0;
      } else {
        std::fprintf(stderr, "Cannot audit bag: unknown argument %s\n",
                     argument.c_str());
        return 1;
      }
    } catch (const std::exception & error) {
      std::fprintf(stderr, "Cannot audit bag: %s\n", error.what());
      return 1;
    }
  }

  try {
    const nlohmann::json report = chaos::data::auditBag(bag_path, options);
    const std::string text = report.dump(2) + "\n";
    if (output_path.empty()) {
      if (std::fwrite(text.data(), 1, text.size(), stdout) != text.size() ||
          std::fflush(stdout) != 0) {
        throw std::runtime_error("cannot write the report to the output");
      }
    } else {
      // The report is created exclusively: an existing file is never
      // replaced, because it is the evidence of an earlier audit.
      const std::filesystem::path target = chaos::data::canonicalPath(output_path);
      std::error_code error;
      if (std::filesystem::exists(target, error)) {
        throw std::runtime_error("output report already exists: " +
                                 output_path);
      }
      const std::filesystem::path bag = chaos::data::canonicalPath(bag_path);
      if (chaos::data::pathIsInside(bag, target)) {
        // The report must not pollute the bag that is being audited.
        throw std::runtime_error(
          "output report must be outside the source bag");
      }
      chaos::data::ExclusiveFile file(target);
      file.write(text);
      // The close is checked: a write error can appear when the buffer is
      // flushed, and the exit code has to reflect that.
      file.close();
    }
  } catch (const std::exception & error) {
    std::fprintf(stderr, "Cannot audit bag: %s\n", error.what());
    return 1;
  }
  return 0;
}
