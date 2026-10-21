// The native data_export entry point.
//
// The command line is the one of the original Python tool: an explicit
// profile and a new output directory.
#include <cstdio>
#include <exception>
#include <string>

#include <nlohmann/json.hpp>

#include "chaos_data/export.hpp"

namespace {

void usage(const char * program)
{
  std::fprintf(stderr,
               "usage: %s <profile.yaml> --output <new output directory>\n",
               program);
}

}  // namespace

int main(int argc, char ** argv)
{
  if (argc < 2) {
    usage(argc > 0 ? argv[0] : "data_export");
    return 2;
  }
  if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
    usage(argv[0]);
    return 0;
  }
  const std::string profile = argv[1];
  std::string output;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--output") {
      if (index + 1 >= argc) {
        std::fprintf(stderr, "Cannot export training dataset: --output needs a "
                     "value\n");
        return 1;
      }
      output = argv[++index];
    } else if (argument == "--help" || argument == "-h") {
      usage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "Cannot export training dataset: unknown argument %s\n",
                   argument.c_str());
      return 1;
    }
  }
  if (output.empty()) {
    std::fprintf(stderr,
                 "Cannot export training dataset: --output is required\n");
    return 1;
  }
  try {
    const chaos::data::ExportReport report =
      chaos::data::exportDataset(profile, output);
    nlohmann::ordered_json summary;
    summary["output"] = report.output.string();
    nlohmann::ordered_json episodes = nlohmann::ordered_json::array();
    for (const auto & episode : report.episodes) {
      nlohmann::ordered_json entry;
      entry["id"] = episode.id;
      entry["rows"] = episode.rows;
      entry["valid_rows"] = episode.valid_rows;
      entry["path"] = episode.path;
      episodes.push_back(entry);
    }
    summary["episodes"] = episodes;
    summary["synthetic"] = report.synthetic;
    summary["time_verified"] = report.time_verified;
    std::printf("%s\n", summary.dump().c_str());
  } catch (const std::exception & error) {
    std::fprintf(stderr, "Cannot export training dataset: %s\n", error.what());
    return 1;
  }
  return 0;
}
