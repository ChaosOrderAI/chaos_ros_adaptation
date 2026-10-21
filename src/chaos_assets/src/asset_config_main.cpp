// Read-only configuration validation and deterministic resolved inventory output.
//
// The command keeps the original Python entry point contract:
//   asset_config INVENTORY [--host HOST] [--mode {mock,real}] [--output FILE]
// A rejected configuration is reported on stderr and exits with status 2.

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "chaos_assets/cli.hpp"
#include "chaos_assets/errors.hpp"

namespace {

void usage() {
  std::cerr << "usage: asset_config INVENTORY [--host HOST] [--mode {mock,real}] [--output FILE]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string inventory;
  std::string host;
  std::string mode;
  std::string output;
  bool have_mode = false;

  std::vector<std::string> arguments(argv + 1, argv + argc);
  for (size_t index = 0; index < arguments.size(); ++index) {
    const std::string& argument = arguments[index];
    auto value_of = [&](const std::string& name) -> std::string {
      if (index + 1 >= arguments.size()) {
        std::cerr << "asset_config: " << name << " requires a value\n";
        std::exit(2);
      }
      return arguments[++index];
    };
    if (argument == "--host") {
      host = value_of("--host");
    } else if (argument == "--mode") {
      mode = value_of("--mode");
      have_mode = true;
    } else if (argument == "--output") {
      output = value_of("--output");
    } else if (argument == "--help" || argument == "-h") {
      usage();
      return 0;
    } else if (!argument.empty() && argument[0] == '-') {
      std::cerr << "asset_config: unknown option " << argument << "\n";
      usage();
      return 2;
    } else if (inventory.empty()) {
      inventory = argument;
    } else {
      std::cerr << "asset_config: unexpected argument " << argument << "\n";
      usage();
      return 2;
    }
  }
  if (inventory.empty()) {
    usage();
    return 2;
  }
  if (have_mode && mode != "mock" && mode != "real") {
    std::cerr << "configuration error: mode override must be mock or real\n";
    return 2;
  }

  try {
    const std::string content = chaos::assets::resolve_inventory_json(inventory, mode, host);
    if (!output.empty()) {
      std::ofstream stream(output, std::ios::binary);
      if (!stream) {
        std::cerr << "asset_config: cannot write " << output << "\n";
        return 2;
      }
      stream << content;
      if (!stream) {
        std::cerr << "asset_config: cannot write " << output << "\n";
        return 2;
      }
    } else {
      std::cout << content;
    }
  } catch (const chaos::assets::ConfigurationError& error) {
    std::cerr << "configuration error: " << error.what() << "\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "configuration error: " << error.what() << "\n";
    return 2;
  }
  return 0;
}
