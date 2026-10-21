// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:msg/CommandResult.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/msg/detail/command_result__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `stamp`
#include "builtin_interfaces/msg/detail/time__functions.h"
// Member `request_id`
// Member `code`
// Member `message`
#include "rosidl_runtime_c/string_functions.h"

bool
chaos_interfaces__msg__CommandResult__init(chaos_interfaces__msg__CommandResult * msg)
{
  if (!msg) {
    return false;
  }
  // stamp
  if (!builtin_interfaces__msg__Time__init(&msg->stamp)) {
    chaos_interfaces__msg__CommandResult__fini(msg);
    return false;
  }
  // request_id
  if (!rosidl_runtime_c__String__init(&msg->request_id)) {
    chaos_interfaces__msg__CommandResult__fini(msg);
    return false;
  }
  // success
  // code
  if (!rosidl_runtime_c__String__init(&msg->code)) {
    chaos_interfaces__msg__CommandResult__fini(msg);
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__init(&msg->message)) {
    chaos_interfaces__msg__CommandResult__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__msg__CommandResult__fini(chaos_interfaces__msg__CommandResult * msg)
{
  if (!msg) {
    return;
  }
  // stamp
  builtin_interfaces__msg__Time__fini(&msg->stamp);
  // request_id
  rosidl_runtime_c__String__fini(&msg->request_id);
  // success
  // code
  rosidl_runtime_c__String__fini(&msg->code);
  // message
  rosidl_runtime_c__String__fini(&msg->message);
}

bool
chaos_interfaces__msg__CommandResult__are_equal(const chaos_interfaces__msg__CommandResult * lhs, const chaos_interfaces__msg__CommandResult * rhs)
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
  // request_id
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->request_id), &(rhs->request_id)))
  {
    return false;
  }
  // success
  if (lhs->success != rhs->success) {
    return false;
  }
  // code
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->code), &(rhs->code)))
  {
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->message), &(rhs->message)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__msg__CommandResult__copy(
  const chaos_interfaces__msg__CommandResult * input,
  chaos_interfaces__msg__CommandResult * output)
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
  // request_id
  if (!rosidl_runtime_c__String__copy(
      &(input->request_id), &(output->request_id)))
  {
    return false;
  }
  // success
  output->success = input->success;
  // code
  if (!rosidl_runtime_c__String__copy(
      &(input->code), &(output->code)))
  {
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__copy(
      &(input->message), &(output->message)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__msg__CommandResult *
chaos_interfaces__msg__CommandResult__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__CommandResult * msg = (chaos_interfaces__msg__CommandResult *)allocator.allocate(sizeof(chaos_interfaces__msg__CommandResult), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__msg__CommandResult));
  bool success = chaos_interfaces__msg__CommandResult__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__msg__CommandResult__destroy(chaos_interfaces__msg__CommandResult * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__msg__CommandResult__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__msg__CommandResult__Sequence__init(chaos_interfaces__msg__CommandResult__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__CommandResult * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__msg__CommandResult)) {
      return false;
    }
    data = (chaos_interfaces__msg__CommandResult *)allocator.zero_allocate(size, sizeof(chaos_interfaces__msg__CommandResult), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__msg__CommandResult__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__msg__CommandResult__fini(&data[i - 1]);
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
chaos_interfaces__msg__CommandResult__Sequence__fini(chaos_interfaces__msg__CommandResult__Sequence * array)
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
      chaos_interfaces__msg__CommandResult__fini(&array->data[i]);
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

chaos_interfaces__msg__CommandResult__Sequence *
chaos_interfaces__msg__CommandResult__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__msg__CommandResult__Sequence * array = (chaos_interfaces__msg__CommandResult__Sequence *)allocator.allocate(sizeof(chaos_interfaces__msg__CommandResult__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__msg__CommandResult__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__msg__CommandResult__Sequence__destroy(chaos_interfaces__msg__CommandResult__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__msg__CommandResult__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__msg__CommandResult__Sequence__are_equal(const chaos_interfaces__msg__CommandResult__Sequence * lhs, const chaos_interfaces__msg__CommandResult__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__msg__CommandResult__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__msg__CommandResult__Sequence__copy(
  const chaos_interfaces__msg__CommandResult__Sequence * input,
  chaos_interfaces__msg__CommandResult__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__msg__CommandResult)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__msg__CommandResult);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__msg__CommandResult * data =
      (chaos_interfaces__msg__CommandResult *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__msg__CommandResult__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__msg__CommandResult__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__msg__CommandResult__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
