// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from chaos_interfaces:srv/Stop.idl
// generated code does not contain a copyright notice
#include "chaos_interfaces/srv/detail/stop__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"

// Include directives for member types
// Member `request_id`
// Member `lease_token`
#include "rosidl_runtime_c/string_functions.h"
// Member `expires_at`
#include "builtin_interfaces/msg/detail/time__functions.h"

bool
chaos_interfaces__srv__Stop_Request__init(chaos_interfaces__srv__Stop_Request * msg)
{
  if (!msg) {
    return false;
  }
  // request_id
  if (!rosidl_runtime_c__String__init(&msg->request_id)) {
    chaos_interfaces__srv__Stop_Request__fini(msg);
    return false;
  }
  // lease_token
  if (!rosidl_runtime_c__String__init(&msg->lease_token)) {
    chaos_interfaces__srv__Stop_Request__fini(msg);
    return false;
  }
  // expires_at
  if (!builtin_interfaces__msg__Time__init(&msg->expires_at)) {
    chaos_interfaces__srv__Stop_Request__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__srv__Stop_Request__fini(chaos_interfaces__srv__Stop_Request * msg)
{
  if (!msg) {
    return;
  }
  // request_id
  rosidl_runtime_c__String__fini(&msg->request_id);
  // lease_token
  rosidl_runtime_c__String__fini(&msg->lease_token);
  // expires_at
  builtin_interfaces__msg__Time__fini(&msg->expires_at);
}

bool
chaos_interfaces__srv__Stop_Request__are_equal(const chaos_interfaces__srv__Stop_Request * lhs, const chaos_interfaces__srv__Stop_Request * rhs)
{
  if (!lhs || !rhs) {
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
  // expires_at
  if (!builtin_interfaces__msg__Time__are_equal(
      &(lhs->expires_at), &(rhs->expires_at)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__srv__Stop_Request__copy(
  const chaos_interfaces__srv__Stop_Request * input,
  chaos_interfaces__srv__Stop_Request * output)
{
  if (!input || !output) {
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
  // expires_at
  if (!builtin_interfaces__msg__Time__copy(
      &(input->expires_at), &(output->expires_at)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__srv__Stop_Request *
chaos_interfaces__srv__Stop_Request__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Request * msg = (chaos_interfaces__srv__Stop_Request *)allocator.allocate(sizeof(chaos_interfaces__srv__Stop_Request), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__srv__Stop_Request));
  bool success = chaos_interfaces__srv__Stop_Request__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__srv__Stop_Request__destroy(chaos_interfaces__srv__Stop_Request * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__srv__Stop_Request__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__srv__Stop_Request__Sequence__init(chaos_interfaces__srv__Stop_Request__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Request * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__srv__Stop_Request)) {
      return false;
    }
    data = (chaos_interfaces__srv__Stop_Request *)allocator.zero_allocate(size, sizeof(chaos_interfaces__srv__Stop_Request), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__srv__Stop_Request__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__srv__Stop_Request__fini(&data[i - 1]);
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
chaos_interfaces__srv__Stop_Request__Sequence__fini(chaos_interfaces__srv__Stop_Request__Sequence * array)
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
      chaos_interfaces__srv__Stop_Request__fini(&array->data[i]);
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

chaos_interfaces__srv__Stop_Request__Sequence *
chaos_interfaces__srv__Stop_Request__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Request__Sequence * array = (chaos_interfaces__srv__Stop_Request__Sequence *)allocator.allocate(sizeof(chaos_interfaces__srv__Stop_Request__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__srv__Stop_Request__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__srv__Stop_Request__Sequence__destroy(chaos_interfaces__srv__Stop_Request__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__srv__Stop_Request__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__srv__Stop_Request__Sequence__are_equal(const chaos_interfaces__srv__Stop_Request__Sequence * lhs, const chaos_interfaces__srv__Stop_Request__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__srv__Stop_Request__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__srv__Stop_Request__Sequence__copy(
  const chaos_interfaces__srv__Stop_Request__Sequence * input,
  chaos_interfaces__srv__Stop_Request__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__srv__Stop_Request)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__srv__Stop_Request);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__srv__Stop_Request * data =
      (chaos_interfaces__srv__Stop_Request *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__srv__Stop_Request__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__srv__Stop_Request__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__srv__Stop_Request__copy(
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
chaos_interfaces__srv__Stop_Response__init(chaos_interfaces__srv__Stop_Response * msg)
{
  if (!msg) {
    return false;
  }
  // success
  // code
  if (!rosidl_runtime_c__String__init(&msg->code)) {
    chaos_interfaces__srv__Stop_Response__fini(msg);
    return false;
  }
  // message
  if (!rosidl_runtime_c__String__init(&msg->message)) {
    chaos_interfaces__srv__Stop_Response__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__srv__Stop_Response__fini(chaos_interfaces__srv__Stop_Response * msg)
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
chaos_interfaces__srv__Stop_Response__are_equal(const chaos_interfaces__srv__Stop_Response * lhs, const chaos_interfaces__srv__Stop_Response * rhs)
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
chaos_interfaces__srv__Stop_Response__copy(
  const chaos_interfaces__srv__Stop_Response * input,
  chaos_interfaces__srv__Stop_Response * output)
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

chaos_interfaces__srv__Stop_Response *
chaos_interfaces__srv__Stop_Response__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Response * msg = (chaos_interfaces__srv__Stop_Response *)allocator.allocate(sizeof(chaos_interfaces__srv__Stop_Response), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__srv__Stop_Response));
  bool success = chaos_interfaces__srv__Stop_Response__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__srv__Stop_Response__destroy(chaos_interfaces__srv__Stop_Response * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__srv__Stop_Response__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__srv__Stop_Response__Sequence__init(chaos_interfaces__srv__Stop_Response__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Response * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__srv__Stop_Response)) {
      return false;
    }
    data = (chaos_interfaces__srv__Stop_Response *)allocator.zero_allocate(size, sizeof(chaos_interfaces__srv__Stop_Response), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__srv__Stop_Response__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__srv__Stop_Response__fini(&data[i - 1]);
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
chaos_interfaces__srv__Stop_Response__Sequence__fini(chaos_interfaces__srv__Stop_Response__Sequence * array)
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
      chaos_interfaces__srv__Stop_Response__fini(&array->data[i]);
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

chaos_interfaces__srv__Stop_Response__Sequence *
chaos_interfaces__srv__Stop_Response__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Response__Sequence * array = (chaos_interfaces__srv__Stop_Response__Sequence *)allocator.allocate(sizeof(chaos_interfaces__srv__Stop_Response__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__srv__Stop_Response__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__srv__Stop_Response__Sequence__destroy(chaos_interfaces__srv__Stop_Response__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__srv__Stop_Response__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__srv__Stop_Response__Sequence__are_equal(const chaos_interfaces__srv__Stop_Response__Sequence * lhs, const chaos_interfaces__srv__Stop_Response__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__srv__Stop_Response__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__srv__Stop_Response__Sequence__copy(
  const chaos_interfaces__srv__Stop_Response__Sequence * input,
  chaos_interfaces__srv__Stop_Response__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__srv__Stop_Response)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__srv__Stop_Response);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__srv__Stop_Response * data =
      (chaos_interfaces__srv__Stop_Response *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__srv__Stop_Response__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__srv__Stop_Response__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__srv__Stop_Response__copy(
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
// #include "chaos_interfaces/srv/detail/stop__functions.h"

bool
chaos_interfaces__srv__Stop_Event__init(chaos_interfaces__srv__Stop_Event * msg)
{
  if (!msg) {
    return false;
  }
  // info
  if (!service_msgs__msg__ServiceEventInfo__init(&msg->info)) {
    chaos_interfaces__srv__Stop_Event__fini(msg);
    return false;
  }
  // request
  if (!chaos_interfaces__srv__Stop_Request__Sequence__init(&msg->request, 0)) {
    chaos_interfaces__srv__Stop_Event__fini(msg);
    return false;
  }
  // response
  if (!chaos_interfaces__srv__Stop_Response__Sequence__init(&msg->response, 0)) {
    chaos_interfaces__srv__Stop_Event__fini(msg);
    return false;
  }
  return true;
}

void
chaos_interfaces__srv__Stop_Event__fini(chaos_interfaces__srv__Stop_Event * msg)
{
  if (!msg) {
    return;
  }
  // info
  service_msgs__msg__ServiceEventInfo__fini(&msg->info);
  // request
  chaos_interfaces__srv__Stop_Request__Sequence__fini(&msg->request);
  // response
  chaos_interfaces__srv__Stop_Response__Sequence__fini(&msg->response);
}

bool
chaos_interfaces__srv__Stop_Event__are_equal(const chaos_interfaces__srv__Stop_Event * lhs, const chaos_interfaces__srv__Stop_Event * rhs)
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
  if (!chaos_interfaces__srv__Stop_Request__Sequence__are_equal(
      &(lhs->request), &(rhs->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__srv__Stop_Response__Sequence__are_equal(
      &(lhs->response), &(rhs->response)))
  {
    return false;
  }
  return true;
}

bool
chaos_interfaces__srv__Stop_Event__copy(
  const chaos_interfaces__srv__Stop_Event * input,
  chaos_interfaces__srv__Stop_Event * output)
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
  if (!chaos_interfaces__srv__Stop_Request__Sequence__copy(
      &(input->request), &(output->request)))
  {
    return false;
  }
  // response
  if (!chaos_interfaces__srv__Stop_Response__Sequence__copy(
      &(input->response), &(output->response)))
  {
    return false;
  }
  return true;
}

chaos_interfaces__srv__Stop_Event *
chaos_interfaces__srv__Stop_Event__create(void)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Event * msg = (chaos_interfaces__srv__Stop_Event *)allocator.allocate(sizeof(chaos_interfaces__srv__Stop_Event), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(chaos_interfaces__srv__Stop_Event));
  bool success = chaos_interfaces__srv__Stop_Event__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
chaos_interfaces__srv__Stop_Event__destroy(chaos_interfaces__srv__Stop_Event * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    chaos_interfaces__srv__Stop_Event__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
chaos_interfaces__srv__Stop_Event__Sequence__init(chaos_interfaces__srv__Stop_Event__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Event * data = NULL;

  if (size) {
    if (size > SIZE_MAX / sizeof(chaos_interfaces__srv__Stop_Event)) {
      return false;
    }
    data = (chaos_interfaces__srv__Stop_Event *)allocator.zero_allocate(size, sizeof(chaos_interfaces__srv__Stop_Event), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = chaos_interfaces__srv__Stop_Event__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        chaos_interfaces__srv__Stop_Event__fini(&data[i - 1]);
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
chaos_interfaces__srv__Stop_Event__Sequence__fini(chaos_interfaces__srv__Stop_Event__Sequence * array)
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
      chaos_interfaces__srv__Stop_Event__fini(&array->data[i]);
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

chaos_interfaces__srv__Stop_Event__Sequence *
chaos_interfaces__srv__Stop_Event__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  chaos_interfaces__srv__Stop_Event__Sequence * array = (chaos_interfaces__srv__Stop_Event__Sequence *)allocator.allocate(sizeof(chaos_interfaces__srv__Stop_Event__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = chaos_interfaces__srv__Stop_Event__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
chaos_interfaces__srv__Stop_Event__Sequence__destroy(chaos_interfaces__srv__Stop_Event__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    chaos_interfaces__srv__Stop_Event__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
chaos_interfaces__srv__Stop_Event__Sequence__are_equal(const chaos_interfaces__srv__Stop_Event__Sequence * lhs, const chaos_interfaces__srv__Stop_Event__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!chaos_interfaces__srv__Stop_Event__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
chaos_interfaces__srv__Stop_Event__Sequence__copy(
  const chaos_interfaces__srv__Stop_Event__Sequence * input,
  chaos_interfaces__srv__Stop_Event__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    if (input->size > SIZE_MAX / sizeof(chaos_interfaces__srv__Stop_Event)) {
      return false;
    }
    const size_t allocation_size =
      input->size * sizeof(chaos_interfaces__srv__Stop_Event);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    chaos_interfaces__srv__Stop_Event * data =
      (chaos_interfaces__srv__Stop_Event *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!chaos_interfaces__srv__Stop_Event__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          chaos_interfaces__srv__Stop_Event__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!chaos_interfaces__srv__Stop_Event__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
