// Thin Python bindings over the native chaos_assets library.
//
// The binding only marshals data: every validation, merge and digest is
// performed by the C++ implementation. The Python package re-exports these
// names so that the launch glue and the test suite keep the original import
// paths.
//
// The public surface of the binding uses plain Python objects instead of the
// native JSON type, so a caller never depends on the internal document
// representation of the engine.

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <vector>

#include "chaos_assets/cli.hpp"
#include "chaos_assets/collection.hpp"
#include "chaos_assets/description.hpp"
#include "chaos_assets/inventory.hpp"
#include "chaos_assets/json.hpp"
#include "chaos_assets/paths.hpp"
#include "chaos_assets/sensors.hpp"
#include "chaos_assets/yaml.hpp"

namespace py = pybind11;
using chaos::assets::Json;

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
  if (py::isinstance<py::int_>(value)) {
    // A Python integer is arbitrary in size, a native document is not. An
    // integer outside the signed 64-bit range is a configuration error and
    // not a cast error, so the caller sees the same kind of rejection as
    // for a value that is read from a file.
    try {
      return Json(value.cast<int64_t>());
    } catch (const py::cast_error&) {
      throw chaos::assets::ConfigurationError("integer exceeds signed 64-bit range");
    }
  }
  if (py::isinstance<py::float_>(value)) return Json(value.cast<double>());
  if (py::isinstance<py::str>(value)) return Json(value.cast<std::string>());
  if (py::isinstance<py::list>(value)) {
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

// The immutable inventory snapshot.
class PyInventory {
 public:
  explicit PyInventory(chaos::assets::Inventory inventory) : inventory_(std::move(inventory)) {}

  const chaos::assets::Inventory& native() const { return inventory_; }

 private:
  chaos::assets::Inventory inventory_;
};

// Accept a string or any path-like object, like the original Python API.
std::string to_path_string(const py::handle& value) {
  if (py::isinstance<py::str>(value)) return value.cast<std::string>();
  return py::module_::import("os").attr("fspath")(value).cast<std::string>();
}

py::list assets_to_python(const std::vector<Json>& assets) {
  py::list result;
  for (const Json& asset : assets) result.append(to_python(asset));
  return result;
}

}  // namespace

PYBIND11_MODULE(_chaos_assets_core, module) {
  module.doc() = "Native configuration engine of the chaos_assets package.";
  py::register_exception<chaos::assets::ConfigurationError>(module, "ConfigurationError",
                                                             PyExc_ValueError);

  py::class_<PyInventory>(module, "Inventory")
      .def_property_readonly(
          "path", [](const PyInventory& self) { return self.native().path(); })
      .def_property_readonly(
          "hosts", [](const PyInventory& self) { return to_python(self.native().hosts()); })
      .def_property_readonly(
          "assets", [](const PyInventory& self) { return assets_to_python(self.native().assets()); })
      .def_property_readonly(
          "transports", [](const PyInventory& self) { return to_python(self.native().transports()); })
      .def("as_dict", [](const PyInventory& self) { return to_python(self.native().as_dict()); })
      .def(
          "select",
          [](const PyInventory& self, const std::string& host_id,
             const std::vector<std::string>& asset_ids) {
            return assets_to_python(self.native().select(host_id, asset_ids));
          },
          py::arg("host_id"), py::arg("asset_ids") = std::vector<std::string>());

  module.def(
      "load_inventory",
      [](const py::object& path, const std::string& mode) {
        return PyInventory(chaos::assets::load_inventory(to_path_string(path), mode));
      },
      py::arg("path"), py::arg("mode") = std::string());

  module.def(
      "resolve_inventory_json",
      [](const py::object& path, const std::string& mode, const std::string& host) {
        return chaos::assets::resolve_inventory_json(to_path_string(path), mode, host);
      },
      py::arg("path"), py::arg("mode") = std::string(), py::arg("host") = std::string());
  module.def(
      "load_yaml_string",
      [](const std::string& content, const std::string& where) {
        return to_python(chaos::assets::load_yaml_string(content, where));
      },
      py::arg("content"), py::arg("where") = std::string("document"));
  module.def(
      "load_yaml_file",
      [](const py::object& path) {
        return to_python(chaos::assets::load_yaml_file(to_path_string(path)));
      },
      py::arg("path"));
  module.def(
      "canonical_json",
      [](const py::object& value) { return chaos::assets::canonical_json(from_python(value)); },
      py::arg("value"));
  module.def(
      "config_digest",
      [](const py::object& value) { return chaos::assets::config_digest(from_python(value)); },
      py::arg("value"));
  module.def(
      "resolve_path",
      [](const py::object& value, const std::string& base, const std::string& where) {
        return chaos::assets::resolve_path(from_python(value), base, where);
      },
      py::arg("value"), py::arg("base"), py::arg("where"));

  py::module_ sensors = module.def_submodule("sensors", "Sensor resolution helpers.");
  sensors.def(
      "resolve_sensors",
      [](const py::object& model, const py::object& instance, const py::object& model_directory,
         const py::object& inventory_directory, const py::object& asset) {
        return to_python(chaos::assets::resolve_sensors(from_python(model), from_python(instance),
                                                         to_path_string(model_directory),
                                                         to_path_string(inventory_directory),
                                                         from_python(asset)));
      },
      py::arg("model_sensors"), py::arg("instance_sensors"), py::arg("model_directory"),
      py::arg("inventory_directory"), py::arg("asset"));
  sensors.def(
      "validate_sensor_graph",
      [](const py::object& assets) {
        Json list = Json::array();
        for (const auto& asset : py::reinterpret_borrow<py::list>(assets)) {
          list.push_back(from_python(asset));
        }
        chaos::assets::validate_sensor_graph(list);
      },
      py::arg("assets"));
  sensors.def(
      "resolve_topic",
      [](const py::object& value, const std::string& namespace_, const std::string& where) {
        return chaos::assets::resolve_topic(from_python(value), namespace_, where);
      },
      py::arg("value"), py::arg("namespace"), py::arg("where"));

  py::module_ collection =
      module.def_submodule("collection", "Recording policy resolution helpers.");
  collection.def(
      "resolve_collection",
      [](const py::object& model, const py::object& instance, const py::object& model_dir,
         const py::object& instance_dir, const py::object& asset) {
        return to_python(chaos::assets::resolve_collection(from_python(model), from_python(instance),
                                                            to_path_string(model_dir),
                                                            to_path_string(instance_dir),
                                                            from_python(asset)));
      },
      py::arg("model"), py::arg("instance"), py::arg("model_dir"), py::arg("instance_dir"),
      py::arg("asset"));

  py::module_ description =
      module.def_submodule("description", "Robot description resolution helpers.");
  description.def(
      "resolve_description",
      [](const py::object& model, const py::object& instance, const std::string& model_directory,
         const std::string& inventory_directory, const py::object& asset) {
        return to_python(chaos::assets::resolve_description(from_python(model), from_python(instance),
                                                             model_directory, inventory_directory,
                                                             from_python(asset)));
      },
      py::arg("model"), py::arg("instance"), py::arg("model_directory"),
      py::arg("inventory_directory"), py::arg("asset"));
  description.def(
      "validate_description_graph",
      [](const py::object& assets) {
        Json list = Json::array();
        for (const auto& asset : py::reinterpret_borrow<py::list>(assets)) {
          list.push_back(from_python(asset));
        }
        chaos::assets::validate_description_graph(list);
      },
      py::arg("assets"));
}
