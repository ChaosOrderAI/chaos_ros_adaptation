// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:msg/RobotState.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/msg/detail/robot_state__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `stamp`
#include "builtin_interfaces/msg/detail/time__functions.h"
// Member `asset_id`
// Member `model`
// Member `operational_state`
// Member `detail`
#include "rosidl_runtime_c/string_functions.h"
// Member `pose`
#include "geometry_msgs/msg/detail/pose_stamped__functions.h"
// Member `twist`
#include "geometry_msgs/msg/detail/twist_stamped__functions.h"

bool
chaos_interfaces__msg__RobotState__init(chaos_interfaces__msg__RobotState * msg)
{
  if (!msg) {
    return false;
  }
  // stamp
  if (!builtin_interfaces__msg__Time__init(&msg->stamp)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__init(&msg->asset_id)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  // model
  if (!rosidl_runtime_c__String__init(&msg->model)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  // operational_state
  if (!rosidl_runtime_c__String__init(&msg->operational_state)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  // connected
  // ready
  // pose_valid
  // pose
  if (!geometry_msgs__msg__PoseStamped__init(&msg->pose)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  // twist
  if (!geometry_msgs__msg__TwistStamped__init(&msg->twist)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  // battery_percentage
  // detail
  if (!rosidl_runtime_c__String__init(&msg->detail)) {
    chaos_interfaces__msg__RobotState__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__msg__RobotState__fini(chaos_interfaces__msg__RobotState * msg)
{
  if (!msg) {
    return;
  }
  // stamp
  builtin_interfaces__msg__Time__fini(&msg->stamp);
  // asset_id
  rosidl_runtime_c__String__fini(&msg->asset_id);
  // model
  rosidl_runtime_c__String__fini(&msg->model);
  // operational_state
  rosidl_runtime_c__String__fini(&msg->operational_state);
  // connected
  // ready
  // pose_valid
  // pose
  geometry_msgs__msg__PoseStamped__fini(&msg->pose);
  // twist
  geometry_msgs__msg__TwistStamped__fini(&msg->twist);
  // battery_percentage
  // detail
  rosidl_runtime_c__String__fini(&msg->detail);
}

bool
chaos_interfaces__msg__RobotState__are_equal(const chaos_interfaces__msg__RobotState * lhs, const chaos_interfaces__msg__RobotState * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // stamp
  if (!builtin_interfaces__msg__Time__are_equal(
      &(lhs->stamp), &(rhs->stamp)))
  {
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->asset_id), &(rhs->asset_id)))
  {
    return false;
  }
  // model
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->model), &(rhs->model)))
  {
    return false;
  }
  // operational_state
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->operational_state), &(rhs->operational_state)))
  {
    return false;
  }
  // connected
  if (lhs->connected != rhs->connected) {
    return false;
  }
  // ready
  if (lhs->ready != rhs->ready) {
    return false;
  }
  // pose_valid
  if (lhs->pose_valid != rhs->pose_valid) {
    return false;
  }
  // pose
  if (!geometry_msgs__msg__PoseStamped__are_equal(
      &(lhs->pose), &(rhs->pose)))
  {
    return false;
  }
  // twist
  if (!geometry_msgs__msg__TwistStamped__are_equal(
      &(lhs->twist), &(rhs->twist)))
  {
    return false;
  }
  // battery_percentage
  if (lhs->battery_percentage != rhs->battery_percentage) {
    return false;
  }
  // detail
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->detail), &(rhs->detail)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__msg__RobotState__copy(
  const chaos_interfaces__msg__RobotState * input,
  chaos_interfaces__msg__RobotState * output)
{
  if (!input || !output) {
    return false;
  }
  // stamp
  if (!builtin_interfaces__msg__Time__copy(
      &(input->stamp), &(output->stamp)))
  {
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__copy(
      &(input->asset_id), &(output->asset_id)))
  {
    return false;
  }
  // model
  if (!rosidl_runtime_c__String__copy(
      &(input->model), &(output->model)))
  {
    return false;
  }
  // operational_state
  if (!rosidl_runtime_c__String__copy(
      &(input->operational_state), &(output->operational_state)))
  {
    return false;
  }
  // connected
  output->connected = input->connected;
  // ready
  output->ready = input->ready;
  // pose_valid
  output->pose_valid = input->pose_valid;
  // pose
  if (!geometry_msgs__msg__PoseStamped__copy(
      &(input->pose), &(output->pose)))
  {
    return false;
  }
  // twist
  if (!geometry_msgs__msg__TwistStamped__copy(
      &(input->twist), &(output->twist)))
  {
    return false;
  }
  // battery_percentage
  output->battery_percentage = input->battery_percentage;
  // detail
  if (!rosidl_runtime_c__String__copy(
      &(input->detail), &(output->detail)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__msg__RobotState *
chaos_interfaces__msg__RobotState__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__RobotState * msg = (chaos_interfaces__msg__RobotState *)allocator.allocate(sizeof(chaos_interfaces__msg__RobotState), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__msg__RobotState));
  bool success = chaos_interfaces__msg__RobotState__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__msg__RobotState__destroy(chaos_interfaces__msg__RobotState * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__msg__RobotState__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__msg__RobotState__Sequence__init(chaos_interfaces__msg__RobotState__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__RobotState * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__msg__RobotState)) {
      return false;
    }
    data = (chaos_interfaces__msg__RobotState *)allocator.zero_allocate(size, sizeof(chaos_interfaces__msg__RobotState), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__msg__RobotState__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__msg__RobotState__fini(&data[i - 1]);
      }
      allocator.deallocate(data, allocator.state);
      return false;
    }
  }
  array->data = data;
  array->size = size;
  array->capacity = size;
  return true;
}

void
chaos_interfaces__msg__RobotState__Sequence__fini(chaos_interfaces__msg__RobotState__Sequence * array)
{
  if (!array) {
    return;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  if (array->data) {
    // ensure that data and capacity values are consistent
    assert(array->capacity > 0);
    // finalize all array elements
    for (size_t i = 0; i < array->capacity; ++i) {
      chaos_interfaces__msg__RobotState__fini(&array->data[i]);
    }
    allocator.deallocate(array->data, allocator.state);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
  } else {
    // ensure that data, size, and capacity values are consistent
    assert(0 == array->size);
    assert(0 == array->capacity);
  }
}

chaos_interfaces__msg__RobotState__Sequence *
chaos_interfaces__msg__RobotState__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__RobotState__Sequence * array = (chaos_interfaces__msg__RobotState__Sequence *)allocator.allocate(sizeof(chaos_interfaces__msg__RobotState__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__msg__RobotState__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__msg__RobotState__Sequence__destroy(chaos_interfaces__msg__RobotState__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__msg__RobotState__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__msg__RobotState__Sequence__are_equal(const chaos_interfaces__msg__RobotState__Sequence * lhs, const chaos_interfaces__msg__RobotState__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__msg__RobotState__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__msg__RobotState__Sequence__copy(
  const chaos_interfaces__msg__RobotState__Sequence * input,
  chaos_interfaces__msg__RobotState__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__msg__RobotState)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__msg__RobotState);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__msg__RobotState * data =
      (chaos_interfaces__msg__RobotState *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__msg__RobotState__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__msg__RobotState__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__msg__RobotState__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
