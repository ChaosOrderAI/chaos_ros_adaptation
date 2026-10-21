// generated from rosidl_generator_c/resource/idl__functions.h.em
// with input from chaos_interfaces:srv/ControlLease.idl
// generated code does not contain a copyright notice

// IWYU pragma: private, include "chaos_interfaces/srv/control_lease.h"


#ifndef CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__FUNCTIONS_H_
#define CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__FUNCTIONS_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdlib.h>

#include "rosidl_runtime_c/action_type_support_struct.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_runtime_c/type_description/type_description__struct.h"
#include "rosidl_runtime_c/type_description/type_source__struct.h"
#include "rosidl_runtime_c/type_hash.h"
#include "rosidl_runtime_c/visibility_control.h"
#include "chaos_interfaces/msg/rosidl_generator_c__visibility_control.h"

#include "chaos_interfaces/srv/detail/control_lease__struct.h"

/// Retrieve pointer to the hash of the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease__get_type_hash(
  const rosidl_service_type_support_t * type_support);

/// Retrieve pointer to the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease__get_type_description(
  const rosidl_service_type_support_t * type_support);

/// Retrieve pointer to the single raw source text that defined this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease__get_individual_type_description_source(
  const rosidl_service_type_support_t * type_support);

/// Retrieve pointer to the recursive raw sources that defined the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease__get_type_description_sources(
  const rosidl_service_type_support_t * type_support);

/// Initialize srv/ControlLease message.
/**
 * If the init function is called twice for the same message without
 * calling fini inbetween previously allocated memory will be leaked.
 * \param[in,out] msg The previously allocated message pointer.
 * Fields without a default value will not be initialized by this function.
 * You might want to call memset(msg, 0, sizeof(
 * chaos_interfaces__srv__ControlLease_Request
 * )) before or use
 * chaos_interfaces__srv__ControlLease_Request__create()
 * to allocate and initialize the message.
 * \return true if initialization was successful, otherwise false
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Request__init(chaos_interfaces__srv__ControlLease_Request * msg);

/// Finalize srv/ControlLease message.
/**
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Request__fini(chaos_interfaces__srv__ControlLease_Request * msg);

/// Create srv/ControlLease message.
/**
 * It allocates the memory for the message, sets the memory to zero, and
 * calls
 * chaos_interfaces__srv__ControlLease_Request__init().
 * \return The pointer to the initialized message if successful,
 * otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
chaos_interfaces__srv__ControlLease_Request *
chaos_interfaces__srv__ControlLease_Request__create(void);

/// Destroy srv/ControlLease message.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Request__fini()
 * and frees the memory of the message.
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Request__destroy(chaos_interfaces__srv__ControlLease_Request * msg);

/// Check for srv/ControlLease message equality.
/**
 * \param[in] lhs The message on the left hand size of the equality operator.
 * \param[in] rhs The message on the right hand size of the equality operator.
 * \return true if messages are equal, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Request__are_equal(const chaos_interfaces__srv__ControlLease_Request * lhs, const chaos_interfaces__srv__ControlLease_Request * rhs);

/// Copy a srv/ControlLease message.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source message pointer.
 * \param[out] output The target message pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer is null
 *   or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Request__copy(
  const chaos_interfaces__srv__ControlLease_Request * input,
  chaos_interfaces__srv__ControlLease_Request * output);

/// Retrieve pointer to the hash of the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease_Request__get_type_hash(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease_Request__get_type_description(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the single raw source text that defined this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease_Request__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the recursive raw sources that defined the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease_Request__get_type_description_sources(
  const rosidl_message_type_support_t * type_support);

/// Initialize array of srv/ControlLease messages.
/**
 * It allocates the memory for the number of elements and calls
 * chaos_interfaces__srv__ControlLease_Request__init()
 * for each element of the array.
 * \param[in,out] array The allocated array pointer.
 * \param[in] size The size / capacity of the array.
 * \return true if initialization was successful, otherwise false
 * If the array pointer is valid and the size is zero it is guaranteed
 # to return true.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Request__Sequence__init(chaos_interfaces__srv__ControlLease_Request__Sequence * array, size_t size);

/// Finalize array of srv/ControlLease messages.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Request__fini()
 * for each element of the array and frees the memory for the number of
 * elements.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Request__Sequence__fini(chaos_interfaces__srv__ControlLease_Request__Sequence * array);

/// Create array of srv/ControlLease messages.
/**
 * It allocates the memory for the array and calls
 * chaos_interfaces__srv__ControlLease_Request__Sequence__init().
 * \param[in] size The size / capacity of the array.
 * \return The pointer to the initialized array if successful, otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
chaos_interfaces__srv__ControlLease_Request__Sequence *
chaos_interfaces__srv__ControlLease_Request__Sequence__create(size_t size);

/// Destroy array of srv/ControlLease messages.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Request__Sequence__fini()
 * on the array,
 * and frees the memory of the array.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Request__Sequence__destroy(chaos_interfaces__srv__ControlLease_Request__Sequence * array);

/// Check for srv/ControlLease message array equality.
/**
 * \param[in] lhs The message array on the left hand size of the equality operator.
 * \param[in] rhs The message array on the right hand size of the equality operator.
 * \return true if message arrays are equal in size and content, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Request__Sequence__are_equal(const chaos_interfaces__srv__ControlLease_Request__Sequence * lhs, const chaos_interfaces__srv__ControlLease_Request__Sequence * rhs);

/// Copy an array of srv/ControlLease messages.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source array pointer.
 * \param[out] output The target array pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer
 *   is null or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Request__Sequence__copy(
  const chaos_interfaces__srv__ControlLease_Request__Sequence * input,
  chaos_interfaces__srv__ControlLease_Request__Sequence * output);

/// Initialize srv/ControlLease message.
/**
 * If the init function is called twice for the same message without
 * calling fini inbetween previously allocated memory will be leaked.
 * \param[in,out] msg The previously allocated message pointer.
 * Fields without a default value will not be initialized by this function.
 * You might want to call memset(msg, 0, sizeof(
 * chaos_interfaces__srv__ControlLease_Response
 * )) before or use
 * chaos_interfaces__srv__ControlLease_Response__create()
 * to allocate and initialize the message.
 * \return true if initialization was successful, otherwise false
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Response__init(chaos_interfaces__srv__ControlLease_Response * msg);

/// Finalize srv/ControlLease message.
/**
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Response__fini(chaos_interfaces__srv__ControlLease_Response * msg);

/// Create srv/ControlLease message.
/**
 * It allocates the memory for the message, sets the memory to zero, and
 * calls
 * chaos_interfaces__srv__ControlLease_Response__init().
 * \return The pointer to the initialized message if successful,
 * otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
chaos_interfaces__srv__ControlLease_Response *
chaos_interfaces__srv__ControlLease_Response__create(void);

/// Destroy srv/ControlLease message.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Response__fini()
 * and frees the memory of the message.
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Response__destroy(chaos_interfaces__srv__ControlLease_Response * msg);

/// Check for srv/ControlLease message equality.
/**
 * \param[in] lhs The message on the left hand size of the equality operator.
 * \param[in] rhs The message on the right hand size of the equality operator.
 * \return true if messages are equal, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Response__are_equal(const chaos_interfaces__srv__ControlLease_Response * lhs, const chaos_interfaces__srv__ControlLease_Response * rhs);

/// Copy a srv/ControlLease message.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source message pointer.
 * \param[out] output The target message pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer is null
 *   or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Response__copy(
  const chaos_interfaces__srv__ControlLease_Response * input,
  chaos_interfaces__srv__ControlLease_Response * output);

/// Retrieve pointer to the hash of the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease_Response__get_type_hash(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease_Response__get_type_description(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the single raw source text that defined this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease_Response__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the recursive raw sources that defined the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease_Response__get_type_description_sources(
  const rosidl_message_type_support_t * type_support);

/// Initialize array of srv/ControlLease messages.
/**
 * It allocates the memory for the number of elements and calls
 * chaos_interfaces__srv__ControlLease_Response__init()
 * for each element of the array.
 * \param[in,out] array The allocated array pointer.
 * \param[in] size The size / capacity of the array.
 * \return true if initialization was successful, otherwise false
 * If the array pointer is valid and the size is zero it is guaranteed
 # to return true.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Response__Sequence__init(chaos_interfaces__srv__ControlLease_Response__Sequence * array, size_t size);

/// Finalize array of srv/ControlLease messages.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Response__fini()
 * for each element of the array and frees the memory for the number of
 * elements.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Response__Sequence__fini(chaos_interfaces__srv__ControlLease_Response__Sequence * array);

/// Create array of srv/ControlLease messages.
/**
 * It allocates the memory for the array and calls
 * chaos_interfaces__srv__ControlLease_Response__Sequence__init().
 * \param[in] size The size / capacity of the array.
 * \return The pointer to the initialized array if successful, otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
chaos_interfaces__srv__ControlLease_Response__Sequence *
chaos_interfaces__srv__ControlLease_Response__Sequence__create(size_t size);

/// Destroy array of srv/ControlLease messages.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Response__Sequence__fini()
 * on the array,
 * and frees the memory of the array.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Response__Sequence__destroy(chaos_interfaces__srv__ControlLease_Response__Sequence * array);

/// Check for srv/ControlLease message array equality.
/**
 * \param[in] lhs The message array on the left hand size of the equality operator.
 * \param[in] rhs The message array on the right hand size of the equality operator.
 * \return true if message arrays are equal in size and content, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Response__Sequence__are_equal(const chaos_interfaces__srv__ControlLease_Response__Sequence * lhs, const chaos_interfaces__srv__ControlLease_Response__Sequence * rhs);

/// Copy an array of srv/ControlLease messages.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source array pointer.
 * \param[out] output The target array pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer
 *   is null or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Response__Sequence__copy(
  const chaos_interfaces__srv__ControlLease_Response__Sequence * input,
  chaos_interfaces__srv__ControlLease_Response__Sequence * output);

/// Initialize srv/ControlLease message.
/**
 * If the init function is called twice for the same message without
 * calling fini inbetween previously allocated memory will be leaked.
 * \param[in,out] msg The previously allocated message pointer.
 * Fields without a default value will not be initialized by this function.
 * You might want to call memset(msg, 0, sizeof(
 * chaos_interfaces__srv__ControlLease_Event
 * )) before or use
 * chaos_interfaces__srv__ControlLease_Event__create()
 * to allocate and initialize the message.
 * \return true if initialization was successful, otherwise false
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Event__init(chaos_interfaces__srv__ControlLease_Event * msg);

/// Finalize srv/ControlLease message.
/**
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Event__fini(chaos_interfaces__srv__ControlLease_Event * msg);

/// Create srv/ControlLease message.
/**
 * It allocates the memory for the message, sets the memory to zero, and
 * calls
 * chaos_interfaces__srv__ControlLease_Event__init().
 * \return The pointer to the initialized message if successful,
 * otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
chaos_interfaces__srv__ControlLease_Event *
chaos_interfaces__srv__ControlLease_Event__create(void);

/// Destroy srv/ControlLease message.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Event__fini()
 * and frees the memory of the message.
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Event__destroy(chaos_interfaces__srv__ControlLease_Event * msg);

/// Check for srv/ControlLease message equality.
/**
 * \param[in] lhs The message on the left hand size of the equality operator.
 * \param[in] rhs The message on the right hand size of the equality operator.
 * \return true if messages are equal, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Event__are_equal(const chaos_interfaces__srv__ControlLease_Event * lhs, const chaos_interfaces__srv__ControlLease_Event * rhs);

/// Copy a srv/ControlLease message.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source message pointer.
 * \param[out] output The target message pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer is null
 *   or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Event__copy(
  const chaos_interfaces__srv__ControlLease_Event * input,
  chaos_interfaces__srv__ControlLease_Event * output);

/// Retrieve pointer to the hash of the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease_Event__get_type_hash(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease_Event__get_type_description(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the single raw source text that defined this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease_Event__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support);

/// Retrieve pointer to the recursive raw sources that defined the description of this type.
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease_Event__get_type_description_sources(
  const rosidl_message_type_support_t * type_support);

/// Initialize array of srv/ControlLease messages.
/**
 * It allocates the memory for the number of elements and calls
 * chaos_interfaces__srv__ControlLease_Event__init()
 * for each element of the array.
 * \param[in,out] array The allocated array pointer.
 * \param[in] size The size / capacity of the array.
 * \return true if initialization was successful, otherwise false
 * If the array pointer is valid and the size is zero it is guaranteed
 # to return true.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Event__Sequence__init(chaos_interfaces__srv__ControlLease_Event__Sequence * array, size_t size);

/// Finalize array of srv/ControlLease messages.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Event__fini()
 * for each element of the array and frees the memory for the number of
 * elements.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Event__Sequence__fini(chaos_interfaces__srv__ControlLease_Event__Sequence * array);

/// Create array of srv/ControlLease messages.
/**
 * It allocates the memory for the array and calls
 * chaos_interfaces__srv__ControlLease_Event__Sequence__init().
 * \param[in] size The size / capacity of the array.
 * \return The pointer to the initialized array if successful, otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
chaos_interfaces__srv__ControlLease_Event__Sequence *
chaos_interfaces__srv__ControlLease_Event__Sequence__create(size_t size);

/// Destroy array of srv/ControlLease messages.
/**
 * It calls
 * chaos_interfaces__srv__ControlLease_Event__Sequence__fini()
 * on the array,
 * and frees the memory of the array.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
void
chaos_interfaces__srv__ControlLease_Event__Sequence__destroy(chaos_interfaces__srv__ControlLease_Event__Sequence * array);

/// Check for srv/ControlLease message array equality.
/**
 * \param[in] lhs The message array on the left hand size of the equality operator.
 * \param[in] rhs The message array on the right hand size of the equality operator.
 * \return true if message arrays are equal in size and content, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Event__Sequence__are_equal(const chaos_interfaces__srv__ControlLease_Event__Sequence * lhs, const chaos_interfaces__srv__ControlLease_Event__Sequence * rhs);

/// Copy an array of srv/ControlLease messages.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source array pointer.
 * \param[out] output The target array pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer
 *   is null or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
bool
chaos_interfaces__srv__ControlLease_Event__Sequence__copy(
  const chaos_interfaces__srv__ControlLease_Event__Sequence * input,
  chaos_interfaces__srv__ControlLease_Event__Sequence * output);
#ifdef __cplusplus
}
#endif

#endif  // CHAOS_INTERFACES__SRV__DETAIL__CONTROL_LEASE__FUNCTIONS_H_
