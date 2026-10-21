// Thin Python binding for the native C++ adapter-config generator. The
// business logic lives entirely in libchaos_mqtt_config; this module only
// converts between Python dicts and nlohmann::json.
#include <pybind11/pybind11.h>

#include <nlohmann/json.hpp>

#include "chaos_mqtt/adapter_config.hpp"

namespace py = pybind11;

namespace
{

nlohmann::json to_json(const py::handle & value)
{
  if (py::isinstance<py::dict>(value)) {
    nlohmann::json result = nlohmann::json::object();
    for (const auto & item : py::cast<py::dict>(value)) {
      result[py::cast<std::string>(py::str(item.first))] = to_json(item.second);
    }
    return result;
  }
  if (py::isinstance<py::list>(value) || py::isinstance<py::tuple>(value)) {
    nlohmann::json result = nlohmann::json::array();
    for (const auto & item : py::cast<py::sequence>(value)) {
      result.push_back(to_json(item));
    }
    return result;
  }
  if (value.is_none()) {
    return nullptr;
  }
  if (py::isinstance<py::bool_>(value)) {
    return py::cast<bool>(value);
  }
  if (py::isinstance<py::int_>(value)) {
    const auto number = py::cast<int64_t>(value);
    if (number >= 0) {
      return static_cast<uint64_t>(number);
    }
    return number;
  }
  if (py::isinstance<py::float_>(value)) {
    return py::cast<double>(value);
  }
  if (py::isinstance<py::str>(value)) {
    return py::cast<std::string>(value);
  }
  throw py::type_error("unsupported value type in adapter config input");
}

py::object to_python(const nlohmann::json & value)
{
  switch (value.type()) {
    case nlohmann::json::value_t::null:
      return py::none();
    case nlohmann::json::value_t::boolean:
      return py::bool_(value.get<bool>());
    case nlohmann::json::value_t::number_integer:
      return py::int_(value.get<int64_t>());
    case nlohmann::json::value_t::number_unsigned:
      return py::int_(value.get<uint64_t>());
    case nlohmann::json::value_t::number_float:
      return py::float_(value.get<double>());
    case nlohmann::json::value_t::string:
      return py::str(value.get<std::string>());
    case nlohmann::json::value_t::array: {
      py::list result;
      for (const auto & item : value) {
        result.append(to_python(item));
      }
      return std::move(result);
    }
    case nlohmann::json::value_t::object: {
      py::dict result;
      for (const auto & item : value.items()) {
        result[py::str(item.key())] = to_python(item.value());
      }
      return std::move(result);
    }
    default:
      throw py::type_error("unsupported value type in adapter config output");
  }
}

py::object build_adapter_config_py(const py::dict & asset, const py::dict & transport_profile)
{
  try {
    return to_python(
      chaos_mqtt::build_adapter_config(to_json(asset), to_json(transport_profile)));
  } catch (const std::invalid_argument & error) {
    throw py::value_error(error.what());
  }
}

}  // namespace

PYBIND11_MODULE(_chaos_mqtt_core, module)
{
  module.def("build_adapter_config", &build_adapter_config_py,
    "Generate the Rust adapter configuration from resolved asset data (native C++).");
}
