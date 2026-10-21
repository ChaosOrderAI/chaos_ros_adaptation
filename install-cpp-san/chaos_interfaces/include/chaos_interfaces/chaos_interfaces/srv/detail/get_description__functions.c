// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:srv/GetDescription.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/srv/detail/get_description__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"

bool
chaos_interfaces__srv__GetDescription_Request__init(chaos_interfaces__srv__GetDescription_Request * msg)
{
  if (!msg) {
    return false;
  }
  // structure_needs_at_least_one_member
  return true;
}

void
chaos_interfaces__srv__GetDescription_Request__fini(chaos_interfaces__srv__GetDescription_Request * msg)
{
  if (!msg) {
    return;
  }
  // structure_needs_at_least_one_member
}

bool
chaos_interfaces__srv__GetDescription_Request__are_equal(const chaos_interfaces__srv__GetDescription_Request * lhs, const chaos_interfaces__srv__GetDescription_Request * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // structure_needs_at_least_one_member
  if (lhs->structure_needs_at_least_one_member != rhs->structure_needs_at_least_one_member) {
    return false;
  }
  return true;
}

bool
chaos_interfaces__srv__GetDescription_Request__copy(
  const chaos_interfaces__srv__GetDescription_Request * input,
  chaos_interfaces__srv__GetDescription_Request * output)
{
  if (!input || !output) {
    return false;
  }
  // structure_needs_at_least_one_member
  output->structure_needs_at_least_one_member = input->structure_needs_at_least_one_member;
  return true;
}

chaos_interfaces__srv__GetDescription_Request *
chaos_interfaces__srv__GetDescription_Request__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Request * msg = (chaos_interfaces__srv__GetDescription_Request *)allocator.allocate(sizeof(chaos_interfaces__srv__GetDescription_Request), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__srv__GetDescription_Request));
  bool success = chaos_interfaces__srv__GetDescription_Request__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__srv__GetDescription_Request__destroy(chaos_interfaces__srv__GetDescription_Request * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__srv__GetDescription_Request__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__srv__GetDescription_Request__Sequence__init(chaos_interfaces__srv__GetDescription_Request__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Request * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__srv__GetDescription_Request)) {
      return false;
    }
    data = (chaos_interfaces__srv__GetDescription_Request *)allocator.zero_allocate(size, sizeof(chaos_interfaces__srv__GetDescription_Request), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__srv__GetDescription_Request__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__srv__GetDescription_Request__fini(&data[i - 1]);
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
chaos_interfaces__srv__GetDescription_Request__Sequence__fini(chaos_interfaces__srv__GetDescription_Request__Sequence * array)
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
      chaos_interfaces__srv__GetDescription_Request__fini(&array->data[i]);
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

chaos_interfaces__srv__GetDescription_Request__Sequence *
chaos_interfaces__srv__GetDescription_Request__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Request__Sequence * array = (chaos_interfaces__srv__GetDescription_Request__Sequence *)allocator.allocate(sizeof(chaos_interfaces__srv__GetDescription_Request__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__srv__GetDescription_Request__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__srv__GetDescription_Request__Sequence__destroy(chaos_interfaces__srv__GetDescription_Request__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__srv__GetDescription_Request__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__srv__GetDescription_Request__Sequence__are_equal(const chaos_interfaces__srv__GetDescription_Request__Sequence * lhs, const chaos_interfaces__srv__GetDescription_Request__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__srv__GetDescription_Request__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__srv__GetDescription_Request__Sequence__copy(
  const chaos_interfaces__srv__GetDescription_Request__Sequence * input,
  chaos_interfaces__srv__GetDescription_Request__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__srv__GetDescription_Request)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__srv__GetDescription_Request);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__srv__GetDescription_Request * data =
      (chaos_interfaces__srv__GetDescription_Request *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__srv__GetDescription_Request__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__srv__GetDescription_Request__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__srv__GetDescription_Request__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}


// Include directives for member types
// Member `asset_id`
// Member `model`
// Member `model_version`
// Member `capabilities`
// Member `base_frame`
// Member `odom_frame`
// Member `map_frame`
#include "rosidl_runtime_c/string_functions.h"

bool
chaos_interfaces__srv__GetDescription_Response__init(chaos_interfaces__srv__GetDescription_Response * msg)
{
  if (!msg) {
    return false;
  }
  // asset_id
  if (!rosidl_runtime_c__String__init(&msg->asset_id)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // model
  if (!rosidl_runtime_c__String__init(&msg->model)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // model_version
  if (!rosidl_runtime_c__String__init(&msg->model_version)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // capabilities
  if (!rosidl_runtime_c__String__Sequence__init(&msg->capabilities, 0)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // base_frame
  if (!rosidl_runtime_c__String__init(&msg->base_frame)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // odom_frame
  if (!rosidl_runtime_c__String__init(&msg->odom_frame)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // map_frame
  if (!rosidl_runtime_c__String__init(&msg->map_frame)) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
    return false;
  }
  // linear_limit_mps
  // angular_limit_rps
  // command_timeout_s
  // lease_max_s
  return true;
}

void
chaos_interfaces__srv__GetDescription_Response__fini(chaos_interfaces__srv__GetDescription_Response * msg)
{
  if (!msg) {
    return;
  }
  // asset_id
  rosidl_runtime_c__String__fini(&msg->asset_id);
  // model
  rosidl_runtime_c__String__fini(&msg->model);
  // model_version
  rosidl_runtime_c__String__fini(&msg->model_version);
  // capabilities
  rosidl_runtime_c__String__Sequence__fini(&msg->capabilities);
  // base_frame
  rosidl_runtime_c__String__fini(&msg->base_frame);
  // odom_frame
  rosidl_runtime_c__String__fini(&msg->odom_frame);
  // map_frame
  rosidl_runtime_c__String__fini(&msg->map_frame);
  // linear_limit_mps
  // angular_limit_rps
  // command_timeout_s
  // lease_max_s
}

bool
chaos_interfaces__srv__GetDescription_Response__are_equal(const chaos_interfaces__srv__GetDescription_Response * lhs, const chaos_interfaces__srv__GetDescription_Response * rhs)
{
  if (!lhs || !rhs) {
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
  // model_version
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->model_version), &(rhs->model_version)))
  {
    return false;
  }
  // capabilities
  if (!rosidl_runtime_c__String__Sequence__are_equal(
      &(lhs->capabilities), &(rhs->capabilities)))
  {
    return false;
  }
  // base_frame
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->base_frame), &(rhs->base_frame)))
  {
    return false;
  }
  // odom_frame
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->odom_frame), &(rhs->odom_frame)))
  {
    return false;
  }
  // map_frame
  if (!rosidl_runtime_c__String__are_equal(
      &(lhs->map_frame), &(rhs->map_frame)))
  {
    return false;
  }
  // linear_limit_mps
  if (lhs->linear_limit_mps != rhs->linear_limit_mps) {
    return false;
  }
  // angular_limit_rps
  if (lhs->angular_limit_rps != rhs->angular_limit_rps) {
    return false;
  }
  // command_timeout_s
  if (lhs->command_timeout_s != rhs->command_timeout_s) {
    return false;
  }
  // lease_max_s
  if (lhs->lease_max_s != rhs->lease_max_s) {
    return false;
  }
  return true;
}

bool
chaos_interfaces__srv__GetDescription_Response__copy(
  const chaos_interfaces__srv__GetDescription_Response * input,
  chaos_interfaces__srv__GetDescription_Response * output)
{
  if (!input || !output) {
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
  // model_version
  if (!rosidl_runtime_c__String__copy(
      &(input->model_version), &(output->model_version)))
  {
    return false;
  }
  // capabilities
  if (!rosidl_runtime_c__String__Sequence__copy(
      &(input->capabilities), &(output->capabilities)))
  {
    return false;
  }
  // base_frame
  if (!rosidl_runtime_c__String__copy(
      &(input->base_frame), &(output->base_frame)))
  {
    return false;
  }
  // odom_frame
  if (!rosidl_runtime_c__String__copy(
      &(input->odom_frame), &(output->odom_frame)))
  {
    return false;
  }
  // map_frame
  if (!rosidl_runtime_c__String__copy(
      &(input->map_frame), &(output->map_frame)))
  {
    return false;
  }
  // linear_limit_mps
  output->linear_limit_mps = input->linear_limit_mps;
  // angular_limit_rps
  output->angular_limit_rps = input->angular_limit_rps;
  // command_timeout_s
  output->command_timeout_s = input->command_timeout_s;
  // lease_max_s
  output->lease_max_s = input->lease_max_s;
  return true;
}

chaos_interfaces__srv__GetDescription_Response *
chaos_interfaces__srv__GetDescription_Response__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Response * msg = (chaos_interfaces__srv__GetDescription_Response *)allocator.allocate(sizeof(chaos_interfaces__srv__GetDescription_Response), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__srv__GetDescription_Response));
  bool success = chaos_interfaces__srv__GetDescription_Response__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__srv__GetDescription_Response__destroy(chaos_interfaces__srv__GetDescription_Response * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__srv__GetDescription_Response__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__srv__GetDescription_Response__Sequence__init(chaos_interfaces__srv__GetDescription_Response__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Response * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__srv__GetDescription_Response)) {
      return false;
    }
    data = (chaos_interfaces__srv__GetDescription_Response *)allocator.zero_allocate(size, sizeof(chaos_interfaces__srv__GetDescription_Response), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__srv__GetDescription_Response__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__srv__GetDescription_Response__fini(&data[i - 1]);
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
chaos_interfaces__srv__GetDescription_Response__Sequence__fini(chaos_interfaces__srv__GetDescription_Response__Sequence * array)
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
      chaos_interfaces__srv__GetDescription_Response__fini(&array->data[i]);
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

chaos_interfaces__srv__GetDescription_Response__Sequence *
chaos_interfaces__srv__GetDescription_Response__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Response__Sequence * array = (chaos_interfaces__srv__GetDescription_Response__Sequence *)allocator.allocate(sizeof(chaos_interfaces__srv__GetDescription_Response__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__srv__GetDescription_Response__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__srv__GetDescription_Response__Sequence__destroy(chaos_interfaces__srv__GetDescription_Response__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__srv__GetDescription_Response__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__srv__GetDescription_Response__Sequence__are_equal(const chaos_interfaces__srv__GetDescription_Response__Sequence * lhs, const chaos_interfaces__srv__GetDescription_Response__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__srv__GetDescription_Response__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__srv__GetDescription_Response__Sequence__copy(
  const chaos_interfaces__srv__GetDescription_Response__Sequence * input,
  chaos_interfaces__srv__GetDescription_Response__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__srv__GetDescription_Response)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__srv__GetDescription_Response);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__srv__GetDescription_Response * data =
      (chaos_interfaces__srv__GetDescription_Response *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__srv__GetDescription_Response__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__srv__GetDescription_Response__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__srv__GetDescription_Response__copy(
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
// #include "chaos_interfaces/srv/detail/get_description__functions.h"

bool
chaos_interfaces__srv__GetDescription_Event__init(chaos_interfaces__srv__GetDescription_Event * msg)
{
  if (!msg) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__init(&msg->info)) {
    chaos_interfaces__srv__GetDescription_Event__fini(msg);
    return false;
  }
  // request
  if (!chaos_interfaces__srv__GetDescription_Request__Sequence__init(&msg->request, 0)) {
    chaos_interfaces__srv__GetDescription_Event__fini(msg);
    return false;
  }
  // response
  if (!chaos_interfaces__srv__GetDescription_Response__Sequence__init(&msg->response, 0)) {
    chaos_interfaces__srv__GetDescription_Event__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__srv__GetDescription_Event__fini(chaos_interfaces__srv__GetDescription_Event * msg)
{
  if (!msg) {
    return;
  }
  // info
  service_msgs__msg__ServiceEventInfo__fini(&msg->info);
  // request
  chaos_interfaces__srv__GetDescription_Request__Sequence__fini(&msg->request);
  // response
  chaos_interfaces__srv__GetDescription_Response__Sequence__fini(&msg->response);
}

bool
chaos_interfaces__srv__GetDescription_Event__are_equal(const chaos_interfaces__srv__GetDescription_Event * lhs, const chaos_interfaces__srv__GetDescription_Event * rhs)
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
  if (!chaos_interfaces__srv__GetDescription_Request__Sequence__are_equal(
      &(lhs->request), &(rhs->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__srv__GetDescription_Response__Sequence__are_equal(
      &(lhs->response), &(rhs->response)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__srv__GetDescription_Event__copy(
  const chaos_interfaces__srv__GetDescription_Event * input,
  chaos_interfaces__srv__GetDescription_Event * output)
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
  if (!chaos_interfaces__srv__GetDescription_Request__Sequence__copy(
      &(input->request), &(output->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__srv__GetDescription_Response__Sequence__copy(
      &(input->response), &(output->response)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__srv__GetDescription_Event *
chaos_interfaces__srv__GetDescription_Event__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Event * msg = (chaos_interfaces__srv__GetDescription_Event *)allocator.allocate(sizeof(chaos_interfaces__srv__GetDescription_Event), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__srv__GetDescription_Event));
  bool success = chaos_interfaces__srv__GetDescription_Event__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__srv__GetDescription_Event__destroy(chaos_interfaces__srv__GetDescription_Event * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__srv__GetDescription_Event__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__srv__GetDescription_Event__Sequence__init(chaos_interfaces__srv__GetDescription_Event__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Event * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__srv__GetDescription_Event)) {
      return false;
    }
    data = (chaos_interfaces__srv__GetDescription_Event *)allocator.zero_allocate(size, sizeof(chaos_interfaces__srv__GetDescription_Event), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__srv__GetDescription_Event__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__srv__GetDescription_Event__fini(&data[i - 1]);
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
chaos_interfaces__srv__GetDescription_Event__Sequence__fini(chaos_interfaces__srv__GetDescription_Event__Sequence * array)
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
      chaos_interfaces__srv__GetDescription_Event__fini(&array->data[i]);
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

chaos_interfaces__srv__GetDescription_Event__Sequence *
chaos_interfaces__srv__GetDescription_Event__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__GetDescription_Event__Sequence * array = (chaos_interfaces__srv__GetDescription_Event__Sequence *)allocator.allocate(sizeof(chaos_interfaces__srv__GetDescription_Event__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__srv__GetDescription_Event__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__srv__GetDescription_Event__Sequence__destroy(chaos_interfaces__srv__GetDescription_Event__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__srv__GetDescription_Event__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__srv__GetDescription_Event__Sequence__are_equal(const chaos_interfaces__srv__GetDescription_Event__Sequence * lhs, const chaos_interfaces__srv__GetDescription_Event__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__srv__GetDescription_Event__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__srv__GetDescription_Event__Sequence__copy(
  const chaos_interfaces__srv__GetDescription_Event__Sequence * input,
  chaos_interfaces__srv__GetDescription_Event__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__srv__GetDescription_Event)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__srv__GetDescription_Event);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__srv__GetDescription_Event * data =
      (chaos_interfaces__srv__GetDescription_Event *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__srv__GetDescription_Event__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__srv__GetDescription_Event__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__srv__GetDescription_Event__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
