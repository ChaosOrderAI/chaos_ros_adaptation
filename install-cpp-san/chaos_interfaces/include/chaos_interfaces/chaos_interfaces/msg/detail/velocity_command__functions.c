// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:msg/VelocityCommand.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/msg/detail/velocity_command__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/detail/header__functions.h"
// Member `request_id`
// Member `lease_token`
#include "rosidl_runtime_c/string_functions.h"
// Member `velocity`
#include "geometry_msgs/msg/detail/twist__functions.h"

bool
chaos_interfaces__msg__VelocityCommand__init(chaos_interfaces__msg__VelocityCommand * msg)
{
  if (!msg) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__init(&msg->header)) {
    chaos_interfaces__msg__VelocityCommand__fini(msg);
    return false;
  }
  // request_id
  if (!rosidl_runtime_c__String__init(&msg->request_id)) {
    chaos_interfaces__msg__VelocityCommand__fini(msg);
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__init(&msg->lease_token)) {
    chaos_interfaces__msg__VelocityCommand__fini(msg);
    return false;
  }
  // valid_for_s
  // velocity
  if (!geometry_msgs__msg__Twist__init(&msg->velocity)) {
    chaos_interfaces__msg__VelocityCommand__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__msg__VelocityCommand__fini(chaos_interfaces__msg__VelocityCommand * msg)
{
  if (!msg) {
    return;
  }
  // header
  std_msgs__msg__Header__fini(&msg->header);
  // request_id
  rosidl_runtime_c__String__fini(&msg->request_id);
  // lease_token
  rosidl_runtime_c__String__fini(&msg->lease_token);
  // valid_for_s
  // velocity
  geometry_msgs__msg__Twist__fini(&msg->velocity);
}

bool
chaos_interfaces__msg__VelocityCommand__are_equal(const chaos_interfaces__msg__VelocityCommand * lhs, const chaos_interfaces__msg__VelocityCommand * rhs)
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
  // request_id
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->request_id), &(rhs->request_id)))
  {
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->lease_token), &(rhs->lease_token)))
  {
    return false;
  }
  // valid_for_s
  if (lhs->valid_for_s != rhs->valid_for_s) {
    return false;
  }
  // velocity
  if (!geometry_msgs__msg__Twist__are_equal(
      &(lhs->velocity), &(rhs->velocity)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__msg__VelocityCommand__copy(
  const chaos_interfaces__msg__VelocityCommand * input,
  chaos_interfaces__msg__VelocityCommand * output)
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
  // request_id
  if (!rosidl_runtime_c__String__copy(
      &(input->request_id), &(output->request_id)))
  {
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__copy(
      &(input->lease_token), &(output->lease_token)))
  {
    return false;
  }
  // valid_for_s
  output->valid_for_s = input->valid_for_s;
  // velocity
  if (!geometry_msgs__msg__Twist__copy(
      &(input->velocity), &(output->velocity)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__msg__VelocityCommand *
chaos_interfaces__msg__VelocityCommand__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__VelocityCommand * msg = (chaos_interfaces__msg__VelocityCommand *)allocator.allocate(sizeof(chaos_interfaces__msg__VelocityCommand), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__msg__VelocityCommand));
  bool success = chaos_interfaces__msg__VelocityCommand__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__msg__VelocityCommand__destroy(chaos_interfaces__msg__VelocityCommand * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__msg__VelocityCommand__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__msg__VelocityCommand__Sequence__init(chaos_interfaces__msg__VelocityCommand__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__VelocityCommand * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__msg__VelocityCommand)) {
      return false;
    }
    data = (chaos_interfaces__msg__VelocityCommand *)allocator.zero_allocate(size, sizeof(chaos_interfaces__msg__VelocityCommand), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__msg__VelocityCommand__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__msg__VelocityCommand__fini(&data[i - 1]);
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
chaos_interfaces__msg__VelocityCommand__Sequence__fini(chaos_interfaces__msg__VelocityCommand__Sequence * array)
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
      chaos_interfaces__msg__VelocityCommand__fini(&array->data[i]);
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

chaos_interfaces__msg__VelocityCommand__Sequence *
chaos_interfaces__msg__VelocityCommand__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__VelocityCommand__Sequence * array = (chaos_interfaces__msg__VelocityCommand__Sequence *)allocator.allocate(sizeof(chaos_interfaces__msg__VelocityCommand__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__msg__VelocityCommand__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__msg__VelocityCommand__Sequence__destroy(chaos_interfaces__msg__VelocityCommand__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__msg__VelocityCommand__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__msg__VelocityCommand__Sequence__are_equal(const chaos_interfaces__msg__VelocityCommand__Sequence * lhs, const chaos_interfaces__msg__VelocityCommand__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__msg__VelocityCommand__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__msg__VelocityCommand__Sequence__copy(
  const chaos_interfaces__msg__VelocityCommand__Sequence * input,
  chaos_interfaces__msg__VelocityCommand__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__msg__VelocityCommand)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__msg__VelocityCommand);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__msg__VelocityCommand * data =
      (chaos_interfaces__msg__VelocityCommand *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__msg__VelocityCommand__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__msg__VelocityCommand__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__msg__VelocityCommand__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
