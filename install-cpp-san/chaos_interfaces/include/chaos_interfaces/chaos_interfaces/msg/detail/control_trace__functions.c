// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/msg/detail/control_trace__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/detail/header__functions.h"
// Member `asset_id`
// Member `request_id`
// Member `command`
// Member `stage`
// Member `code`
#include "rosidl_runtime_c/string_functions.h"
// Member `velocity`
#include "geometry_msgs/msg/detail/twist__functions.h"

bool
chaos_interfaces__msg__ControlTrace__init(chaos_interfaces__msg__ControlTrace * msg)
{
  if (!msg) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__init(&msg->header)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__init(&msg->asset_id)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  // sequence
  // monotonic_ns
  // request_id
  if (!rosidl_runtime_c__String__init(&msg->request_id)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  // command
  if (!rosidl_runtime_c__String__init(&msg->command)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  // stage
  if (!rosidl_runtime_c__String__init(&msg->stage)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  // velocity_valid
  // velocity
  if (!geometry_msgs__msg__Twist__init(&msg->velocity)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  // valid_for_s
  // code
  if (!rosidl_runtime_c__String__init(&msg->code)) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__msg__ControlTrace__fini(chaos_interfaces__msg__ControlTrace * msg)
{
  if (!msg) {
    return;
  }
  // header
  std_msgs__msg__Header__fini(&msg->header);
  // asset_id
  rosidl_runtime_c__String__fini(&msg->asset_id);
  // sequence
  // monotonic_ns
  // request_id
  rosidl_runtime_c__String__fini(&msg->request_id);
  // command
  rosidl_runtime_c__String__fini(&msg->command);
  // stage
  rosidl_runtime_c__String__fini(&msg->stage);
  // velocity_valid
  // velocity
  geometry_msgs__msg__Twist__fini(&msg->velocity);
  // valid_for_s
  // code
  rosidl_runtime_c__String__fini(&msg->code);
}

bool
chaos_interfaces__msg__ControlTrace__are_equal(const chaos_interfaces__msg__ControlTrace * lhs, const chaos_interfaces__msg__ControlTrace * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__are_equal(
      &(lhs->header), &(rhs->header)))
  {
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->asset_id), &(rhs->asset_id)))
  {
    return false;
  }
  // sequence
  if (lhs->sequence != rhs->sequence) {
    return false;
  }
  // monotonic_ns
  if (lhs->monotonic_ns != rhs->monotonic_ns) {
    return false;
  }
  // request_id
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->request_id), &(rhs->request_id)))
  {
    return false;
  }
  // command
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->command), &(rhs->command)))
  {
    return false;
  }
  // stage
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->stage), &(rhs->stage)))
  {
    return false;
  }
  // velocity_valid
  if (lhs->velocity_valid != rhs->velocity_valid) {
    return false;
  }
  // velocity
  if (!geometry_msgs__msg__Twist__are_equal(
      &(lhs->velocity), &(rhs->velocity)))
  {
    return false;
  }
  // valid_for_s
  if (lhs->valid_for_s != rhs->valid_for_s) {
    return false;
  }
  // code
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->code), &(rhs->code)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__msg__ControlTrace__copy(
  const chaos_interfaces__msg__ControlTrace * input,
  chaos_interfaces__msg__ControlTrace * output)
{
  if (!input || !output) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__copy(
      &(input->header), &(output->header)))
  {
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__copy(
      &(input->asset_id), &(output->asset_id)))
  {
    return false;
  }
  // sequence
  output->sequence = input->sequence;
  // monotonic_ns
  output->monotonic_ns = input->monotonic_ns;
  // request_id
  if (!rosidl_runtime_c__String__copy(
      &(input->request_id), &(output->request_id)))
  {
    return false;
  }
  // command
  if (!rosidl_runtime_c__String__copy(
      &(input->command), &(output->command)))
  {
    return false;
  }
  // stage
  if (!rosidl_runtime_c__String__copy(
      &(input->stage), &(output->stage)))
  {
    return false;
  }
  // velocity_valid
  output->velocity_valid = input->velocity_valid;
  // velocity
  if (!geometry_msgs__msg__Twist__copy(
      &(input->velocity), &(output->velocity)))
  {
    return false;
  }
  // valid_for_s
  output->valid_for_s = input->valid_for_s;
  // code
  if (!rosidl_runtime_c__String__copy(
      &(input->code), &(output->code)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__msg__ControlTrace *
chaos_interfaces__msg__ControlTrace__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__ControlTrace * msg = (chaos_interfaces__msg__ControlTrace *)allocator.allocate(sizeof(chaos_interfaces__msg__ControlTrace), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__msg__ControlTrace));
  bool success = chaos_interfaces__msg__ControlTrace__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__msg__ControlTrace__destroy(chaos_interfaces__msg__ControlTrace * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__msg__ControlTrace__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__msg__ControlTrace__Sequence__init(chaos_interfaces__msg__ControlTrace__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__ControlTrace * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__msg__ControlTrace)) {
      return false;
    }
    data = (chaos_interfaces__msg__ControlTrace *)allocator.zero_allocate(size, sizeof(chaos_interfaces__msg__ControlTrace), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__msg__ControlTrace__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__msg__ControlTrace__fini(&data[i - 1]);
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
chaos_interfaces__msg__ControlTrace__Sequence__fini(chaos_interfaces__msg__ControlTrace__Sequence * array)
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
      chaos_interfaces__msg__ControlTrace__fini(&array->data[i]);
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

chaos_interfaces__msg__ControlTrace__Sequence *
chaos_interfaces__msg__ControlTrace__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__ControlTrace__Sequence * array = (chaos_interfaces__msg__ControlTrace__Sequence *)allocator.allocate(sizeof(chaos_interfaces__msg__ControlTrace__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__msg__ControlTrace__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__msg__ControlTrace__Sequence__destroy(chaos_interfaces__msg__ControlTrace__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__msg__ControlTrace__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__msg__ControlTrace__Sequence__are_equal(const chaos_interfaces__msg__ControlTrace__Sequence * lhs, const chaos_interfaces__msg__ControlTrace__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__msg__ControlTrace__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__msg__ControlTrace__Sequence__copy(
  const chaos_interfaces__msg__ControlTrace__Sequence * input,
  chaos_interfaces__msg__ControlTrace__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__msg__ControlTrace)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__msg__ControlTrace);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__msg__ControlTrace * data =
      (chaos_interfaces__msg__ControlTrace *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__msg__ControlTrace__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__msg__ControlTrace__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__msg__ControlTrace__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
