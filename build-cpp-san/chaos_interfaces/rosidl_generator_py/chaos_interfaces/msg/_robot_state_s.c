// generated from rosidl_generator_py/resource/_idl_support.c.em
// with input from chaos_interfaces:msg/RobotState.idl
// generated code does not contain a copyright notice
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <stdbool.h>
#ifndef _WIN32
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "numpy/ndarrayobject.h"
#ifndef _WIN32
# pragma GCC diagnostic pop
#endif
#include "rosidl_runtime_c/visibility_control.h"
#include "chaos_interfaces/msg/detail/robot_state__struct.h"
#include "chaos_interfaces/msg/detail/robot_state__functions.h"

#include "rosidl_runtime_c/string.h"
#include "rosidl_runtime_c/string_functions.h"

ROSIDL_GENERATOR_C_IMPORT
bool builtin_interfaces__msg__time__convert_from_py(PyObject * _pymsg, void * _ros_message);
ROSIDL_GENERATOR_C_IMPORT
PyObject * builtin_interfaces__msg__time__convert_to_py(void * raw_ros_message);
ROSIDL_GENERATOR_C_IMPORT
bool geometry_msgs__msg__pose_stamped__convert_from_py(PyObject * _pymsg, void * _ros_message);
ROSIDL_GENERATOR_C_IMPORT
PyObject * geometry_msgs__msg__pose_stamped__convert_to_py(void * raw_ros_message);
ROSIDL_GENERATOR_C_IMPORT
bool geometry_msgs__msg__twist_stamped__convert_from_py(PyObject * _pymsg, void * _ros_message);
ROSIDL_GENERATOR_C_IMPORT
PyObject * geometry_msgs__msg__twist_stamped__convert_to_py(void * raw_ros_message);

ROSIDL_GENERATOR_C_EXPORT
bool chaos_interfaces__msg__robot_state__convert_from_py(PyObject * _pymsg, void * _ros_message)
{
  // check that the passed message is of the expected Python class
  {
    char full_classname_dest[45];
    {
      char * class_name = NULL;
      char * module_name = NULL;
      {
        PyObject * class_attr = PyObject_GetAttrString(_pymsg, "__class__");
        if (class_attr) {
          PyObject * name_attr = PyObject_GetAttrString(class_attr, "__name__");
          if (name_attr) {
            class_name = (char *)PyUnicode_1BYTE_DATA(name_attr);
            Py_DECREF(name_attr);
          }
          PyObject * module_attr = PyObject_GetAttrString(class_attr, "__module__");
          if (module_attr) {
            module_name = (char *)PyUnicode_1BYTE_DATA(module_attr);
            Py_DECREF(module_attr);
          }
          Py_DECREF(class_attr);
        }
      }
      if (!class_name || !module_name) {
        return false;
      }
      snprintf(full_classname_dest, sizeof(full_classname_dest), "%s.%s", module_name, class_name);
    }
    assert(strncmp("chaos_interfaces.msg._robot_state.RobotState", full_classname_dest, 44) == 0);
  }
  chaos_interfaces__msg__RobotState * ros_message = _ros_message;
  {  // stamp
    PyObject * field = PyObject_GetAttrString(_pymsg, "stamp");
    if (!field) {
      return false;
    }
    if (!builtin_interfaces__msg__time__convert_from_py(field, &ros_message->stamp)) {
      Py_DECREF(field);
      return false;
    }
    Py_DECREF(field);
  }
  {  // asset_id
    PyObject * field = PyObject_GetAttrString(_pymsg, "asset_id");
    if (!field) {
      return false;
    }
    assert(PyUnicode_Check(field));
    PyObject * encoded_field = PyUnicode_AsUTF8String(field);
    if (!encoded_field) {
      Py_DECREF(field);
      return false;
    }
    rosidl_runtime_c__String__assign(&ros_message->asset_id, PyBytes_AS_STRING(encoded_field));
    Py_DECREF(encoded_field);
    Py_DECREF(field);
  }
  {  // model
    PyObject * field = PyObject_GetAttrString(_pymsg, "model");
    if (!field) {
      return false;
    }
    assert(PyUnicode_Check(field));
    PyObject * encoded_field = PyUnicode_AsUTF8String(field);
    if (!encoded_field) {
      Py_DECREF(field);
      return false;
    }
    rosidl_runtime_c__String__assign(&ros_message->model, PyBytes_AS_STRING(encoded_field));
    Py_DECREF(encoded_field);
    Py_DECREF(field);
  }
  {  // operational_state
    PyObject * field = PyObject_GetAttrString(_pymsg, "operational_state");
    if (!field) {
      return false;
    }
    assert(PyUnicode_Check(field));
    PyObject * encoded_field = PyUnicode_AsUTF8String(field);
    if (!encoded_field) {
      Py_DECREF(field);
      return false;
    }
    rosidl_runtime_c__String__assign(&ros_message->operational_state, PyBytes_AS_STRING(encoded_field));
    Py_DECREF(encoded_field);
    Py_DECREF(field);
  }
  {  // connected
    PyObject * field = PyObject_GetAttrString(_pymsg, "connected");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->connected = (Py_True == field);
    Py_DECREF(field);
  }
  {  // ready
    PyObject * field = PyObject_GetAttrString(_pymsg, "ready");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->ready = (Py_True == field);
    Py_DECREF(field);
  }
  {  // pose_valid
    PyObject * field = PyObject_GetAttrString(_pymsg, "pose_valid");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->pose_valid = (Py_True == field);
    Py_DECREF(field);
  }
  {  // pose
    PyObject * field = PyObject_GetAttrString(_pymsg, "pose");
    if (!field) {
      return false;
    }
    if (!geometry_msgs__msg__pose_stamped__convert_from_py(field, &ros_message->pose)) {
      Py_DECREF(field);
      return false;
    }
    Py_DECREF(field);
  }
  {  // twist
    PyObject * field = PyObject_GetAttrString(_pymsg, "twist");
    if (!field) {
      return false;
    }
    if (!geometry_msgs__msg__twist_stamped__convert_from_py(field, &ros_message->twist)) {
      Py_DECREF(field);
      return false;
    }
    Py_DECREF(field);
  }
  {  // battery_percentage
    PyObject * field = PyObject_GetAttrString(_pymsg, "battery_percentage");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->battery_percentage = (float)PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // detail
    PyObject * field = PyObject_GetAttrString(_pymsg, "detail");
    if (!field) {
      return false;
    }
    assert(PyUnicode_Check(field));
    PyObject * encoded_field = PyUnicode_AsUTF8String(field);
    if (!encoded_field) {
      Py_DECREF(field);
      return false;
    }
    rosidl_runtime_c__String__assign(&ros_message->detail, PyBytes_AS_STRING(encoded_field));
    Py_DECREF(encoded_field);
    Py_DECREF(field);
  }

  return true;
}

ROSIDL_GENERATOR_C_EXPORT
PyObject * chaos_interfaces__msg__robot_state__convert_to_py(void * raw_ros_message)
{
  /* NOTE(esteve): Call constructor of RobotState */
  PyObject * _pymessage = NULL;
  {
    PyObject * pymessage_module = PyImport_ImportModule("chaos_interfaces.msg._robot_state");
    assert(pymessage_module);
    PyObject * pymessage_class = PyObject_GetAttrString(pymessage_module, "RobotState");
    assert(pymessage_class);
    Py_DECREF(pymessage_module);
    _pymessage = PyObject_CallObject(pymessage_class, NULL);
    Py_DECREF(pymessage_class);
    if (!_pymessage) {
      return NULL;
    }
  }
  chaos_interfaces__msg__RobotState * ros_message = (chaos_interfaces__msg__RobotState *)raw_ros_message;
  {  // stamp
    PyObject * field = NULL;
    field = builtin_interfaces__msg__time__convert_to_py(&ros_message->stamp);
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "stamp", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // asset_id
    PyObject * field = NULL;
    field = PyUnicode_DecodeUTF8(
      ros_message->asset_id.data,
      strlen(ros_message->asset_id.data),
      "replace");
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "asset_id", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // model
    PyObject * field = NULL;
    field = PyUnicode_DecodeUTF8(
      ros_message->model.data,
      strlen(ros_message->model.data),
      "replace");
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "model", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // operational_state
    PyObject * field = NULL;
    field = PyUnicode_DecodeUTF8(
      ros_message->operational_state.data,
      strlen(ros_message->operational_state.data),
      "replace");
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "operational_state", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // connected
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->connected ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "connected", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // ready
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->ready ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "ready", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // pose_valid
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->pose_valid ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "pose_valid", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // pose
    PyObject * field = NULL;
    field = geometry_msgs__msg__pose_stamped__convert_to_py(&ros_message->pose);
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "pose", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // twist
    PyObject * field = NULL;
    field = geometry_msgs__msg__twist_stamped__convert_to_py(&ros_message->twist);
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "twist", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // battery_percentage
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->battery_percentage);
    {
      int rc = PyObject_SetAttrString(_pymessage, "battery_percentage", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // detail
    PyObject * field = NULL;
    field = PyUnicode_DecodeUTF8(
      ros_message->detail.data,
      strlen(ros_message->detail.data),
      "replace");
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "detail", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }

  // ownership of _pymessage is transferred to the caller
  return _pymessage;
}
