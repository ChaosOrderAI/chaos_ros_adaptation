// Thin Python bindings over the native chaos_bringup planner.
//
// The binding only marshals data: the planning, the selection, the
// collision rules, the component derivation and the preflight checks are
// performed by the C++ implementation. The launch glue keeps the ROS action
// assembly in Python and delegates every decision to this module.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <vector>

#include "chaos_assets/errors.hpp"
#include "chaos_bringup/components.hpp"
#include "chaos_bringup/planner.hpp"
#include "chaos_bringup/runtime.hpp"

namespace py = pybind11;
using chaos::bringup::Json;

namespace {

// Convert a native document into plain Python objects.
py::object to_python(const Json& value) {
  if (value.is_null()) return py::none();
  if (value.is_boolean()) return py::bool_(value.get<bool>());
  if (value.is_number_integer()) return py::int_(value.get<int64_t>());
  if (value.is_number_unsigned()) return py::int_(value.get<uint64_t>());
  if (value.is_number_float()) return py::float_(value.get<double>());
  if (value.is_string()) return py::str(value.get_ref<const std::string&>());
  if (value.is_array()) {
    py::list result;
    for (const auto& item : value) result.append(to_python(item));
    return result;
  }
  if (value.is_object()) {
    py::dict result;
    for (auto it = value.begin(); it != value.end(); ++it) {
      result[py::str(it.key())] = to_python(it.value());
    }
    return result;
  }
  return py::none();
}

// Convert plain Python objects into a native document.
Json from_python(const py::handle& value) {
  if (value.is_none()) return Json();
  if (py::isinstance<py::bool_>(value)) return Json(value.cast<bool>());
  if (py::isinstance<py::int_>(value)) return Json(value.cast<int64_t>());
  if (py::isinstance<py::float_>(value)) return Json(value.cast<double>());
  if (py::isinstance<py::str>(value)) return Json(value.cast<std::string>());
  if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value)) {
    Json result = Json::array();
    for (const auto& item : value) result.push_back(from_python(item));
    return result;
  }
  if (py::isinstance<py::dict>(value)) {
    Json result = Json::object();
    for (const auto& item : py::reinterpret_borrow<py::dict>(value)) {
      result[py::str(item.first).cast<std::string>()] = from_python(item.second);
    }
    return result;
  }
  throw std::invalid_argument("expected a JSON compatible value");
}

// Accept a string or any path-like object.
std::string to_path_string(const py::handle& value) {
  if (py::isinstance<py::str>(value)) return value.cast<std::string>();
  return py::module_::import("os").attr("fspath")(value).cast<std::string>();
}

// The plan items of a Python object, as a native plan.
//
// The object is borrowed as an iterable instead of cast to a list: a plan is
// a sequence in Python, and a caller may pass a tuple, a generator or a
// wrapper class. An unchecked cast to a list would read past the end of an
// object of another type, so the type is checked and a wrong input is
// reported as a Python exception.
chaos::bringup::LaunchPlan plan_from_python(const py::handle& items) {
  if (!py::isinstance<py::iterable>(items) || py::isinstance<py::str>(items) ||
      py::isinstance<py::bytes>(items)) {
    throw std::invalid_argument("expected an iterable of plan items");
  }
  std::vector<Json> encoded;
  for (const py::handle& item : py::reinterpret_borrow<py::iterable>(items)) {
    encoded.push_back(from_python(item));
  }
  return chaos::bringup::LaunchPlan::from_items(encoded);
}

py::list plan_to_python(const chaos::bringup::LaunchPlan& plan) {
  py::list result;
  for (const Json& item : plan.items()) result.append(to_python(item));
  return result;
}

// The MQTT adapter configuration producer, injected by the caller.
//
// The adapter configuration format belongs to chaos_mqtt; the planner only
// consumes it to validate the MQTT identity of an asset.
chaos::bringup::MqttConfigFactory factory_from_python(const py::object& factory) {
  if (factory.is_none()) return chaos::bringup::MqttConfigFactory();
  return [factory](const Json& asset, const Json& profile) -> Json {
    py::object result = factory(to_python(asset), to_python(profile));
    return from_python(result);
  };
}

// The configuration error of the asset engine.
//
// The planner and the asset engine share one exception type, so a caller of
// both packages catches one error. The type of chaos_assets is reused
// instead of a second registration, because two registrations of the same C++
// type would replace each other in the exception map of pybind11.
py::object configuration_error_type() {
  static py::object type = py::module_::import("chaos_assets").attr("ConfigurationError");
  return type;
}

}  // namespace

PYBIND11_MODULE(_chaos_bringup_core, module) {
  module.doc() = "Native launch planner of the chaos_bringup package.";
  // A configuration error of the planner is reported with the exception type
  // of the asset engine. The translator runs before the default handling of
  // pybind11, so the type of chaos_assets is used for both packages.
  py::register_exception_translator([](std::exception_ptr pointer) {
    try {
      if (pointer) std::rethrow_exception(pointer);
    } catch (const chaos::assets::ConfigurationError& error) {
      PyErr_SetString(configuration_error_type().ptr(), error.what());
    }
  });

  module.def(
      "prepare_plan",
      [](const py::object& inventory_path, const std::string& host_id, const std::string& mode,
         const std::vector<std::string>& asset_ids, bool enable_mqtt, bool check_files,
         const py::object& factory) {
        chaos::bringup::PlanRequest request;
        request.inventory_path = to_path_string(inventory_path);
        request.host_id = host_id;
        request.mode = mode;
        request.asset_ids = asset_ids;
        request.enable_mqtt = enable_mqtt;
        request.check_files = check_files;
        return plan_to_python(chaos::bringup::prepare_plan(request, factory_from_python(factory)));
      },
      py::arg("inventory_path"), py::arg("host_id"), py::arg("mode") = std::string(),
      py::arg("asset_ids") = std::vector<std::string>(), py::arg("enable_mqtt") = false,
      py::arg("check_files") = true, py::arg("mqtt_config") = py::none());

  module.def(
      "prepare_edge_plan",
      [](const py::object& inventory_path, const std::string& asset_id, bool enable_mqtt,
         bool check_files, const py::object& factory) {
        return plan_to_python(chaos::bringup::prepare_edge_plan(
            to_path_string(inventory_path), asset_id, enable_mqtt, check_files,
            factory_from_python(factory)));
      },
      py::arg("inventory_path"), py::arg("asset_id"), py::arg("enable_mqtt") = true,
      py::arg("check_files") = true, py::arg("mqtt_config") = py::none());

  module.def(
      "resolve_mqtt_executable",
      [](const std::string& executable, const py::object& prefix_resolver) {
        chaos::bringup::PackagePrefixResolver resolver;
        if (!prefix_resolver.is_none()) {
          resolver = [prefix_resolver](const std::string& package) {
            return prefix_resolver(package).cast<std::string>();
          };
        }
        return chaos::bringup::resolve_mqtt_executable(executable, resolver);
      },
      py::arg("executable") = std::string("mqtt_transport"), py::arg("prefix_resolver") = py::none());

  module.def(
      "plan_summary",
      [](const py::object& items) {
        return to_python(chaos::bringup::plan_summary(plan_from_python(items)));
      },
      py::arg("items"));

  module.def(
      "preflight",
      [](const py::object& items, const std::vector<std::string>& registered_writers,
         const py::object& backend_available, const py::object& executable_available,
         const py::object& validate_asset, bool check_files) {
        chaos::bringup::PreflightOptions options;
        options.registered_writers = registered_writers;
        options.check_files = check_files;
        if (!backend_available.is_none()) {
          options.backend_available = [backend_available](const std::string& plugin) {
            return backend_available(plugin).cast<bool>();
          };
        }
        if (!executable_available.is_none()) {
          options.executable_available =
              [executable_available](const std::string& package, const std::string& executable) {
                return executable_available(package, executable).cast<bool>();
              };
        }
        if (!validate_asset.is_none()) {
          options.validate_asset = [validate_asset](const Json& asset) {
            validate_asset(to_python(asset));
          };
        }
        chaos::bringup::preflight(plan_from_python(items), options);
      },
      py::arg("items"), py::arg("registered_writers") = std::vector<std::string>(),
      py::arg("backend_available") = py::none(), py::arg("executable_available") = py::none(),
      py::arg("validate_asset") = py::none(), py::arg("check_files") = true);

  module.def(
      "snapshot_parameter_files",
      [](const py::object& items, const py::object& root) {
        return to_python(
            chaos::bringup::snapshot_parameter_files(plan_from_python(items), to_path_string(root)));
      },
      py::arg("items"), py::arg("root"));

  // The functions that carry a document are wrapped: the public surface of
  // the binding uses plain Python objects, so a caller never depends on the
  // internal document representation of the engine.
  module.def(
      "component_asset",
      [](const py::object& asset) { return to_python(chaos::bringup::component_asset(from_python(asset))); },
      py::arg("asset"));
  module.def(
      "sensor_summary",
      [](const py::object& asset) { return to_python(chaos::bringup::sensor_summary(from_python(asset))); },
      py::arg("asset"));
  module.def(
      "description_summary",
      [](const py::object& asset) {
        return to_python(chaos::bringup::description_summary(from_python(asset)));
      },
      py::arg("asset"));
  module.def(
      "collection_summary",
      [](const py::object& asset) {
        return to_python(chaos::bringup::collection_summary(from_python(asset)));
      },
      py::arg("asset"));
  module.def(
      "validate_asset_with_runtime",
      [](const py::object& asset) {
        chaos::bringup::validate_asset_with_runtime(from_python(asset),
                                                     chaos::bringup::RuntimeInterface());
      },
      py::arg("asset"));

  module.def(
      "native_backend_available",
      [](const std::string& plugin) {
        return chaos::bringup::native_backend_available(plugin,
                                                        chaos::bringup::RuntimeInterface());
      },
      py::arg("plugin"));

  module.def("require_executable", &chaos::bringup::require_executable, py::arg("package"),
             py::arg("executable"));
  module.def(
      "validate_parameter_file", &chaos::bringup::validate_parameter_file, py::arg("content"),
      py::arg("node_name"), py::arg("source"));
  module.def("check_output_directory",
             [](const py::object& path) {
               chaos::bringup::check_output_directory(to_path_string(path));
             },
             py::arg("path"));

  auto specs = [](const std::vector<chaos::bringup::ProcessSpec>& processes) {
    py::list result;
    for (const auto& spec : processes) {
      py::dict entry;
      entry["label"] = spec.label;
      entry["package"] = spec.package;
      entry["executable"] = spec.executable;
      entry["name"] = spec.name;
      entry["namespace"] = spec.node_namespace;
      entry["arguments"] = to_python(spec.arguments);
      entry["parameters"] = to_python(spec.parameters);
      entry["remappings"] = to_python(Json(spec.remappings));
      entry["transport"] = spec.transport;
      entry["command"] = to_python(spec.command);
      result.append(entry);
    }
    return result;
  };

  module.def(
      "driver_processes",
      [specs](const py::object& asset) { return specs(chaos::bringup::driver_processes(from_python(asset))); },
      py::arg("asset"));
  module.def(
      "description_processes",
      [specs](const py::object& asset) {
        return specs(chaos::bringup::description_processes(from_python(asset)));
      },
      py::arg("asset"));
  module.def(
      "sensor_processes",
      [specs](const py::object& asset, const py::object& parameter_files) {
        return specs(chaos::bringup::sensor_processes(from_python(asset), from_python(parameter_files)));
      },
      py::arg("asset"), py::arg("parameter_files"));
  module.def(
      "collection_processes",
      [specs](const py::object& asset) {
        return specs(chaos::bringup::collection_processes(from_python(asset)));
      },
      py::arg("asset"));
}
