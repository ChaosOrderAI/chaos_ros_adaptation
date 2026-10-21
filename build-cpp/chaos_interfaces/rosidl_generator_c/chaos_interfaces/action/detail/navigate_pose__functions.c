// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:action/NavigatePose.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/action/detail/navigate_pose__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `lease_token`
#include "rosidl_runtime_c/string_functions.h"
// Member `target`
#include "geometry_msgs/msg/detail/pose_stamped__functions.h"
// Member `expires_at`
#include "builtin_interfaces/msg/detail/time__functions.h"

bool
chaos_interfaces__action__NavigatePose_Goal__init(chaos_interfaces__action__NavigatePose_Goal * msg)
{
  if (!msg) {
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__init(&msg->lease_token)) {
    chaos_interfaces__action__NavigatePose_Goal__fini(msg);
    return false;
  }
  // target
  if (!geometry_msgs__msg__PoseStamped__init(&msg->target)) {
    chaos_interfaces__action__NavigatePose_Goal__fini(msg);
    return false;
  }
  // expires_at
  if (!builtin_interfaces__msg__Time__init(&msg->expires_at)) {
    chaos_interfaces__action__NavigatePose_Goal__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_Goal__fini(chaos_interfaces__action__NavigatePose_Goal * msg)
{
  if (!msg) {
    return;
  }
  // lease_token
  rosidl_runtime_c__String__fini(&msg->lease_token);
  // target
  geometry_msgs__msg__PoseStamped__fini(&msg->target);
  // expires_at
  builtin_interfaces__msg__Time__fini(&msg->expires_at);
}

bool
chaos_interfaces__action__NavigatePose_Goal__are_equal(const chaos_interfaces__action__NavigatePose_Goal * lhs, const chaos_interfaces__action__NavigatePose_Goal * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->lease_token), &(rhs->lease_token)))
  {
    return false;
  }
  // target
  if (!geometry_msgs__msg__PoseStamped__are_equal(
      &(lhs->target), &(rhs->target)))
  {
    return false;
  }
  // expires_at
  if (!builtin_interfaces__msg__Time__are_equal(
      &(lhs->expires_at), &(rhs->expires_at)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_Goal__copy(
  const chaos_interfaces__action__NavigatePose_Goal * input,
  chaos_interfaces__action__NavigatePose_Goal * output)
{
  if (!input || !output) {
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__copy(
      &(input->lease_token), &(output->lease_token)))
  {
    return false;
  }
  // target
  if (!geometry_msgs__msg__PoseStamped__copy(
      &(input->target), &(output->target)))
  {
    return false;
  }
  // expires_at
  if (!builtin_interfaces__msg__Time__copy(
      &(input->expires_at), &(output->expires_at)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_Goal *
chaos_interfaces__action__NavigatePose_Goal__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Goal * msg = (chaos_interfaces__action__NavigatePose_Goal *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_Goal), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_Goal));
  bool success = chaos_interfaces__action__NavigatePose_Goal__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_Goal__destroy(chaos_interfaces__action__NavigatePose_Goal * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_Goal__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_Goal__Sequence__init(chaos_interfaces__action__NavigatePose_Goal__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Goal * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_Goal)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_Goal *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_Goal), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_Goal__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_Goal__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_Goal__Sequence__fini(chaos_interfaces__action__NavigatePose_Goal__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_Goal__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_Goal__Sequence *
chaos_interfaces__action__NavigatePose_Goal__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Goal__Sequence * array = (chaos_interfaces__action__NavigatePose_Goal__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_Goal__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_Goal__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_Goal__Sequence__destroy(chaos_interfaces__action__NavigatePose_Goal__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_Goal__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_Goal__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_Goal__Sequence * lhs, const chaos_interfaces__action__NavigatePose_Goal__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_Goal__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_Goal__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_Goal__Sequence * input,
  chaos_interfaces__action__NavigatePose_Goal__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_Goal)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_Goal);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_Goal * data =
      (chaos_interfaces__action__NavigatePose_Goal *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_Goal__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_Goal__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_Goal__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `code`
// Member `message`
// already included above
// #include "rosidl_runtime_c/string_functions.h"

bool
chaos_interfaces__action__NavigatePose_Result__init(chaos_interfaces__action__NavigatePose_Result * msg)
{
  if (!msg) {
    return false;
  }
  // success
  // code
  if (!rosidl_runtime_c__String__init(&msg->code)) {
    chaos_interfaces__action__NavigatePose_Result__fini(msg);
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__init(&msg->message)) {
    chaos_interfaces__action__NavigatePose_Result__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_Result__fini(chaos_interfaces__action__NavigatePose_Result * msg)
{
  if (!msg) {
    return;
  }
  // success
  // code
  rosidl_runtime_c__String__fini(&msg->code);
  // message
  rosidl_runtime_c__String__fini(&msg->message);
}

bool
chaos_interfaces__action__NavigatePose_Result__are_equal(const chaos_interfaces__action__NavigatePose_Result * lhs, const chaos_interfaces__action__NavigatePose_Result * rhs)
{
  if (!lhs || !rhs) {
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
chaos_interfaces__action__NavigatePose_Result__copy(
  const chaos_interfaces__action__NavigatePose_Result * input,
  chaos_interfaces__action__NavigatePose_Result * output)
{
  if (!input || !output) {
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

chaos_interfaces__action__NavigatePose_Result *
chaos_interfaces__action__NavigatePose_Result__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Result * msg = (chaos_interfaces__action__NavigatePose_Result *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_Result), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_Result));
  bool success = chaos_interfaces__action__NavigatePose_Result__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_Result__destroy(chaos_interfaces__action__NavigatePose_Result * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_Result__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_Result__Sequence__init(chaos_interfaces__action__NavigatePose_Result__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Result * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_Result)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_Result *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_Result), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_Result__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_Result__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_Result__Sequence__fini(chaos_interfaces__action__NavigatePose_Result__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_Result__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_Result__Sequence *
chaos_interfaces__action__NavigatePose_Result__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Result__Sequence * array = (chaos_interfaces__action__NavigatePose_Result__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_Result__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_Result__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_Result__Sequence__destroy(chaos_interfaces__action__NavigatePose_Result__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_Result__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_Result__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_Result__Sequence * lhs, const chaos_interfaces__action__NavigatePose_Result__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_Result__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_Result__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_Result__Sequence * input,
  chaos_interfaces__action__NavigatePose_Result__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_Result)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_Result);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_Result * data =
      (chaos_interfaces__action__NavigatePose_Result *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_Result__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_Result__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_Result__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `current_pose`
// already included above
// #include "geometry_msgs/msg/detail/pose_stamped__functions.h"

bool
chaos_interfaces__action__NavigatePose_Feedback__init(chaos_interfaces__action__NavigatePose_Feedback * msg)
{
  if (!msg) {
    return false;
  }
  // current_pose
  if (!geometry_msgs__msg__PoseStamped__init(&msg->current_pose)) {
    chaos_interfaces__action__NavigatePose_Feedback__fini(msg);
    return false;
  }
  // progress
  return true;
}

void
chaos_interfaces__action__NavigatePose_Feedback__fini(chaos_interfaces__action__NavigatePose_Feedback * msg)
{
  if (!msg) {
    return;
  }
  // current_pose
  geometry_msgs__msg__PoseStamped__fini(&msg->current_pose);
  // progress
}

bool
chaos_interfaces__action__NavigatePose_Feedback__are_equal(const chaos_interfaces__action__NavigatePose_Feedback * lhs, const chaos_interfaces__action__NavigatePose_Feedback * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // current_pose
  if (!geometry_msgs__msg__PoseStamped__are_equal(
      &(lhs->current_pose), &(rhs->current_pose)))
  {
    return false;
  }
  // progress
  if (lhs->progress != rhs->progress) {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_Feedback__copy(
  const chaos_interfaces__action__NavigatePose_Feedback * input,
  chaos_interfaces__action__NavigatePose_Feedback * output)
{
  if (!input || !output) {
    return false;
  }
  // current_pose
  if (!geometry_msgs__msg__PoseStamped__copy(
      &(input->current_pose), &(output->current_pose)))
  {
    return false;
  }
  // progress
  output->progress = input->progress;
  return true;
}

chaos_interfaces__action__NavigatePose_Feedback *
chaos_interfaces__action__NavigatePose_Feedback__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Feedback * msg = (chaos_interfaces__action__NavigatePose_Feedback *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_Feedback), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_Feedback));
  bool success = chaos_interfaces__action__NavigatePose_Feedback__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_Feedback__destroy(chaos_interfaces__action__NavigatePose_Feedback * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_Feedback__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_Feedback__Sequence__init(chaos_interfaces__action__NavigatePose_Feedback__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Feedback * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_Feedback)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_Feedback *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_Feedback), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_Feedback__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_Feedback__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_Feedback__Sequence__fini(chaos_interfaces__action__NavigatePose_Feedback__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_Feedback__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_Feedback__Sequence *
chaos_interfaces__action__NavigatePose_Feedback__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_Feedback__Sequence * array = (chaos_interfaces__action__NavigatePose_Feedback__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_Feedback__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_Feedback__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_Feedback__Sequence__destroy(chaos_interfaces__action__NavigatePose_Feedback__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_Feedback__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_Feedback__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_Feedback__Sequence * lhs, const chaos_interfaces__action__NavigatePose_Feedback__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_Feedback__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_Feedback__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_Feedback__Sequence * input,
  chaos_interfaces__action__NavigatePose_Feedback__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_Feedback)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_Feedback);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_Feedback * data =
      (chaos_interfaces__action__NavigatePose_Feedback *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_Feedback__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_Feedback__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_Feedback__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `goal_id`
#include "unique_identifier_msgs/msg/detail/uuid__functions.h"
// Member `goal`
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"

bool
chaos_interfaces__action__NavigatePose_SendGoal_Request__init(chaos_interfaces__action__NavigatePose_SendGoal_Request * msg)
{
  if (!msg) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__init(&msg->goal_id)) {
    chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(msg);
    return false;
  }
  // goal
  if (!chaos_interfaces__action__NavigatePose_Goal__init(&msg->goal)) {
    chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(chaos_interfaces__action__NavigatePose_SendGoal_Request * msg)
{
  if (!msg) {
    return;
  }
  // goal_id
  unique_identifier_msgs__msg__UUID__fini(&msg->goal_id);
  // goal
  chaos_interfaces__action__NavigatePose_Goal__fini(&msg->goal);
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Request__are_equal(const chaos_interfaces__action__NavigatePose_SendGoal_Request * lhs, const chaos_interfaces__action__NavigatePose_SendGoal_Request * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__are_equal(
      &(lhs->goal_id), &(rhs->goal_id)))
  {
    return false;
  }
  // goal
  if (!chaos_interfaces__action__NavigatePose_Goal__are_equal(
      &(lhs->goal), &(rhs->goal)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Request__copy(
  const chaos_interfaces__action__NavigatePose_SendGoal_Request * input,
  chaos_interfaces__action__NavigatePose_SendGoal_Request * output)
{
  if (!input || !output) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__copy(
      &(input->goal_id), &(output->goal_id)))
  {
    return false;
  }
  // goal
  if (!chaos_interfaces__action__NavigatePose_Goal__copy(
      &(input->goal), &(output->goal)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_SendGoal_Request *
chaos_interfaces__action__NavigatePose_SendGoal_Request__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Request * msg = (chaos_interfaces__action__NavigatePose_SendGoal_Request *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request));
  bool success = chaos_interfaces__action__NavigatePose_SendGoal_Request__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Request__destroy(chaos_interfaces__action__NavigatePose_SendGoal_Request * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__init(chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Request * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_SendGoal_Request *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_SendGoal_Request__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__fini(chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence *
chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * array = (chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__destroy(chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * lhs, const chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_SendGoal_Request__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * input,
  chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Request);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_SendGoal_Request * data =
      (chaos_interfaces__action__NavigatePose_SendGoal_Request *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_SendGoal_Request__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_SendGoal_Request__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_SendGoal_Request__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `stamp`
// already included above
// #include "builtin_interfaces/msg/detail/time__functions.h"

bool
chaos_interfaces__action__NavigatePose_SendGoal_Response__init(chaos_interfaces__action__NavigatePose_SendGoal_Response * msg)
{
  if (!msg) {
    return false;
  }
  // accepted
  // stamp
  if (!builtin_interfaces__msg__Time__init(&msg->stamp)) {
    chaos_interfaces__action__NavigatePose_SendGoal_Response__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Response__fini(chaos_interfaces__action__NavigatePose_SendGoal_Response * msg)
{
  if (!msg) {
    return;
  }
  // accepted
  // stamp
  builtin_interfaces__msg__Time__fini(&msg->stamp);
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Response__are_equal(const chaos_interfaces__action__NavigatePose_SendGoal_Response * lhs, const chaos_interfaces__action__NavigatePose_SendGoal_Response * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // accepted
  if (lhs->accepted != rhs->accepted) {
    return false;
  }
  // stamp
  if (!builtin_interfaces__msg__Time__are_equal(
      &(lhs->stamp), &(rhs->stamp)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Response__copy(
  const chaos_interfaces__action__NavigatePose_SendGoal_Response * input,
  chaos_interfaces__action__NavigatePose_SendGoal_Response * output)
{
  if (!input || !output) {
    return false;
  }
  // accepted
  output->accepted = input->accepted;
  // stamp
  if (!builtin_interfaces__msg__Time__copy(
      &(input->stamp), &(output->stamp)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_SendGoal_Response *
chaos_interfaces__action__NavigatePose_SendGoal_Response__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Response * msg = (chaos_interfaces__action__NavigatePose_SendGoal_Response *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response));
  bool success = chaos_interfaces__action__NavigatePose_SendGoal_Response__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Response__destroy(chaos_interfaces__action__NavigatePose_SendGoal_Response * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_SendGoal_Response__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__init(chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Response * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_SendGoal_Response *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_SendGoal_Response__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_SendGoal_Response__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__fini(chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_SendGoal_Response__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence *
chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * array = (chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__destroy(chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * lhs, const chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_SendGoal_Response__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * input,
  chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Response);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_SendGoal_Response * data =
      (chaos_interfaces__action__NavigatePose_SendGoal_Response *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_SendGoal_Response__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_SendGoal_Response__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_SendGoal_Response__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `info`
#include "service_msgs/msg/detail/service_event_info__functions.h"
// Member `request`
// Member `response`
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"

bool
chaos_interfaces__action__NavigatePose_SendGoal_Event__init(chaos_interfaces__action__NavigatePose_SendGoal_Event * msg)
{
  if (!msg) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__init(&msg->info)) {
    chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(msg);
    return false;
  }
  // request
  if (!chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__init(&msg->request, 0)) {
    chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(msg);
    return false;
  }
  // response
  if (!chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__init(&msg->response, 0)) {
    chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(chaos_interfaces__action__NavigatePose_SendGoal_Event * msg)
{
  if (!msg) {
    return;
  }
  // info
  service_msgs__msg__ServiceEventInfo__fini(&msg->info);
  // request
  chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__fini(&msg->request);
  // response
  chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__fini(&msg->response);
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Event__are_equal(const chaos_interfaces__action__NavigatePose_SendGoal_Event * lhs, const chaos_interfaces__action__NavigatePose_SendGoal_Event * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__are_equal(
      &(lhs->info), &(rhs->info)))
  {
    return false;
  }
  // request
  if (!chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__are_equal(
      &(lhs->request), &(rhs->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__are_equal(
      &(lhs->response), &(rhs->response)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Event__copy(
  const chaos_interfaces__action__NavigatePose_SendGoal_Event * input,
  chaos_interfaces__action__NavigatePose_SendGoal_Event * output)
{
  if (!input || !output) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__copy(
      &(input->info), &(output->info)))
  {
    return false;
  }
  // request
  if (!chaos_interfaces__action__NavigatePose_SendGoal_Request__Sequence__copy(
      &(input->request), &(output->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__action__NavigatePose_SendGoal_Response__Sequence__copy(
      &(input->response), &(output->response)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_SendGoal_Event *
chaos_interfaces__action__NavigatePose_SendGoal_Event__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Event * msg = (chaos_interfaces__action__NavigatePose_SendGoal_Event *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event));
  bool success = chaos_interfaces__action__NavigatePose_SendGoal_Event__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Event__destroy(chaos_interfaces__action__NavigatePose_SendGoal_Event * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__init(chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Event * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_SendGoal_Event *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_SendGoal_Event__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__fini(chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence *
chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * array = (chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__destroy(chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * lhs, const chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_SendGoal_Event__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * input,
  chaos_interfaces__action__NavigatePose_SendGoal_Event__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_SendGoal_Event);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_SendGoal_Event * data =
      (chaos_interfaces__action__NavigatePose_SendGoal_Event *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_SendGoal_Event__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_SendGoal_Event__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_SendGoal_Event__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `goal_id`
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__functions.h"

bool
chaos_interfaces__action__NavigatePose_GetResult_Request__init(chaos_interfaces__action__NavigatePose_GetResult_Request * msg)
{
  if (!msg) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__init(&msg->goal_id)) {
    chaos_interfaces__action__NavigatePose_GetResult_Request__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Request__fini(chaos_interfaces__action__NavigatePose_GetResult_Request * msg)
{
  if (!msg) {
    return;
  }
  // goal_id
  unique_identifier_msgs__msg__UUID__fini(&msg->goal_id);
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Request__are_equal(const chaos_interfaces__action__NavigatePose_GetResult_Request * lhs, const chaos_interfaces__action__NavigatePose_GetResult_Request * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__are_equal(
      &(lhs->goal_id), &(rhs->goal_id)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Request__copy(
  const chaos_interfaces__action__NavigatePose_GetResult_Request * input,
  chaos_interfaces__action__NavigatePose_GetResult_Request * output)
{
  if (!input || !output) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__copy(
      &(input->goal_id), &(output->goal_id)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_GetResult_Request *
chaos_interfaces__action__NavigatePose_GetResult_Request__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Request * msg = (chaos_interfaces__action__NavigatePose_GetResult_Request *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request));
  bool success = chaos_interfaces__action__NavigatePose_GetResult_Request__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Request__destroy(chaos_interfaces__action__NavigatePose_GetResult_Request * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_GetResult_Request__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__init(chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Request * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_GetResult_Request *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_GetResult_Request__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_GetResult_Request__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__fini(chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_GetResult_Request__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence *
chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * array = (chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__destroy(chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * lhs, const chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_GetResult_Request__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * input,
  chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_GetResult_Request);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_GetResult_Request * data =
      (chaos_interfaces__action__NavigatePose_GetResult_Request *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_GetResult_Request__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_GetResult_Request__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_GetResult_Request__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `result`
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"

bool
chaos_interfaces__action__NavigatePose_GetResult_Response__init(chaos_interfaces__action__NavigatePose_GetResult_Response * msg)
{
  if (!msg) {
    return false;
  }
  // status
  // result
  if (!chaos_interfaces__action__NavigatePose_Result__init(&msg->result)) {
    chaos_interfaces__action__NavigatePose_GetResult_Response__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Response__fini(chaos_interfaces__action__NavigatePose_GetResult_Response * msg)
{
  if (!msg) {
    return;
  }
  // status
  // result
  chaos_interfaces__action__NavigatePose_Result__fini(&msg->result);
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Response__are_equal(const chaos_interfaces__action__NavigatePose_GetResult_Response * lhs, const chaos_interfaces__action__NavigatePose_GetResult_Response * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // status
  if (lhs->status != rhs->status) {
    return false;
  }
  // result
  if (!chaos_interfaces__action__NavigatePose_Result__are_equal(
      &(lhs->result), &(rhs->result)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Response__copy(
  const chaos_interfaces__action__NavigatePose_GetResult_Response * input,
  chaos_interfaces__action__NavigatePose_GetResult_Response * output)
{
  if (!input || !output) {
    return false;
  }
  // status
  output->status = input->status;
  // result
  if (!chaos_interfaces__action__NavigatePose_Result__copy(
      &(input->result), &(output->result)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_GetResult_Response *
chaos_interfaces__action__NavigatePose_GetResult_Response__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Response * msg = (chaos_interfaces__action__NavigatePose_GetResult_Response *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response));
  bool success = chaos_interfaces__action__NavigatePose_GetResult_Response__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Response__destroy(chaos_interfaces__action__NavigatePose_GetResult_Response * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_GetResult_Response__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__init(chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Response * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_GetResult_Response *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_GetResult_Response__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_GetResult_Response__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__fini(chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_GetResult_Response__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence *
chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * array = (chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__destroy(chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * lhs, const chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_GetResult_Response__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * input,
  chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_GetResult_Response);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_GetResult_Response * data =
      (chaos_interfaces__action__NavigatePose_GetResult_Response *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_GetResult_Response__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_GetResult_Response__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_GetResult_Response__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `info`
// already included above
// #include "service_msgs/msg/detail/service_event_info__functions.h"
// Member `request`
// Member `response`
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"

bool
chaos_interfaces__action__NavigatePose_GetResult_Event__init(chaos_interfaces__action__NavigatePose_GetResult_Event * msg)
{
  if (!msg) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__init(&msg->info)) {
    chaos_interfaces__action__NavigatePose_GetResult_Event__fini(msg);
    return false;
  }
  // request
  if (!chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__init(&msg->request, 0)) {
    chaos_interfaces__action__NavigatePose_GetResult_Event__fini(msg);
    return false;
  }
  // response
  if (!chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__init(&msg->response, 0)) {
    chaos_interfaces__action__NavigatePose_GetResult_Event__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Event__fini(chaos_interfaces__action__NavigatePose_GetResult_Event * msg)
{
  if (!msg) {
    return;
  }
  // info
  service_msgs__msg__ServiceEventInfo__fini(&msg->info);
  // request
  chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__fini(&msg->request);
  // response
  chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__fini(&msg->response);
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Event__are_equal(const chaos_interfaces__action__NavigatePose_GetResult_Event * lhs, const chaos_interfaces__action__NavigatePose_GetResult_Event * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__are_equal(
      &(lhs->info), &(rhs->info)))
  {
    return false;
  }
  // request
  if (!chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__are_equal(
      &(lhs->request), &(rhs->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__are_equal(
      &(lhs->response), &(rhs->response)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Event__copy(
  const chaos_interfaces__action__NavigatePose_GetResult_Event * input,
  chaos_interfaces__action__NavigatePose_GetResult_Event * output)
{
  if (!input || !output) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__copy(
      &(input->info), &(output->info)))
  {
    return false;
  }
  // request
  if (!chaos_interfaces__action__NavigatePose_GetResult_Request__Sequence__copy(
      &(input->request), &(output->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__action__NavigatePose_GetResult_Response__Sequence__copy(
      &(input->response), &(output->response)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_GetResult_Event *
chaos_interfaces__action__NavigatePose_GetResult_Event__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Event * msg = (chaos_interfaces__action__NavigatePose_GetResult_Event *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event));
  bool success = chaos_interfaces__action__NavigatePose_GetResult_Event__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Event__destroy(chaos_interfaces__action__NavigatePose_GetResult_Event * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_GetResult_Event__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__init(chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Event * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_GetResult_Event *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_GetResult_Event__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_GetResult_Event__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__fini(chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_GetResult_Event__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence *
chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * array = (chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__destroy(chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * lhs, const chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_GetResult_Event__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * input,
  chaos_interfaces__action__NavigatePose_GetResult_Event__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_GetResult_Event);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_GetResult_Event * data =
      (chaos_interfaces__action__NavigatePose_GetResult_Event *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_GetResult_Event__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_GetResult_Event__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_GetResult_Event__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `goal_id`
// already included above
// #include "unique_identifier_msgs/msg/detail/uuid__functions.h"
// Member `feedback`
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"

bool
chaos_interfaces__action__NavigatePose_FeedbackMessage__init(chaos_interfaces__action__NavigatePose_FeedbackMessage * msg)
{
  if (!msg) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__init(&msg->goal_id)) {
    chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(msg);
    return false;
  }
  // feedback
  if (!chaos_interfaces__action__NavigatePose_Feedback__init(&msg->feedback)) {
    chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(chaos_interfaces__action__NavigatePose_FeedbackMessage * msg)
{
  if (!msg) {
    return;
  }
  // goal_id
  unique_identifier_msgs__msg__UUID__fini(&msg->goal_id);
  // feedback
  chaos_interfaces__action__NavigatePose_Feedback__fini(&msg->feedback);
}

bool
chaos_interfaces__action__NavigatePose_FeedbackMessage__are_equal(const chaos_interfaces__action__NavigatePose_FeedbackMessage * lhs, const chaos_interfaces__action__NavigatePose_FeedbackMessage * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__are_equal(
      &(lhs->goal_id), &(rhs->goal_id)))
  {
    return false;
  }
  // feedback
  if (!chaos_interfaces__action__NavigatePose_Feedback__are_equal(
      &(lhs->feedback), &(rhs->feedback)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_FeedbackMessage__copy(
  const chaos_interfaces__action__NavigatePose_FeedbackMessage * input,
  chaos_interfaces__action__NavigatePose_FeedbackMessage * output)
{
  if (!input || !output) {
    return false;
  }
  // goal_id
  if (!unique_identifier_msgs__msg__UUID__copy(
      &(input->goal_id), &(output->goal_id)))
  {
    return false;
  }
  // feedback
  if (!chaos_interfaces__action__NavigatePose_Feedback__copy(
      &(input->feedback), &(output->feedback)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__action__NavigatePose_FeedbackMessage *
chaos_interfaces__action__NavigatePose_FeedbackMessage__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_FeedbackMessage * msg = (chaos_interfaces__action__NavigatePose_FeedbackMessage *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage));
  bool success = chaos_interfaces__action__NavigatePose_FeedbackMessage__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__action__NavigatePose_FeedbackMessage__destroy(chaos_interfaces__action__NavigatePose_FeedbackMessage * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__init(chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_FeedbackMessage * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage)) {
      return false;
    }
    data = (chaos_interfaces__action__NavigatePose_FeedbackMessage *)allocator.zero_allocate(size, sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__action__NavigatePose_FeedbackMessage__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(&data[i - 1]);
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
chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__fini(chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * array)
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
      chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(&array->data[i]);
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

chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence *
chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * array = (chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence *)allocator.allocate(sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__destroy(chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__are_equal(const chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * lhs, const chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_FeedbackMessage__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence__copy(
  const chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * input,
  chaos_interfaces__action__NavigatePose_FeedbackMessage__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__action__NavigatePose_FeedbackMessage);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__action__NavigatePose_FeedbackMessage * data =
      (chaos_interfaces__action__NavigatePose_FeedbackMessage *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__action__NavigatePose_FeedbackMessage__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__action__NavigatePose_FeedbackMessage__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__action__NavigatePose_FeedbackMessage__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
