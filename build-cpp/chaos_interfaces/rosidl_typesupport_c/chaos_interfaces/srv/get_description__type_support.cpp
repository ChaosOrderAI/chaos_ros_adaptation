// generated from rosidl_typesupport_c/resource/idl__type_support.cpp.em
// with input from chaos_interfaces:srv/GetDescription.idl
// generated code does not contain a copyright notice

#include "cstddef"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "chaos_interfaces/srv/detail/get_description__struct.h"
#include "chaos_interfaces/srv/detail/get_description__type_support.h"
#include "chaos_interfaces/srv/detail/get_description__functions.h"
#include "rosidl_typesupport_c/identifier.h"
#include "rosidl_typesupport_c/message_type_support_dispatch.h"
#include "rosidl_typesupport_c/type_support_map.h"
#include "rosidl_typesupport_c/visibility_control.h"
#include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_c
{

typedef struct _GetDescription_Request_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _GetDescription_Request_type_support_ids_t;

static const _GetDescription_Request_type_support_ids_t _GetDescription_Request_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_c",  // ::rosidl_typesupport_fastrtps_c::typesupport_identifier,
    "rosidl_typesupport_introspection_c",  // ::rosidl_typesupport_introspection_c::typesupport_identifier,
  }
};

typedef struct _GetDescription_Request_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _GetDescription_Request_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _GetDescription_Request_type_support_symbol_names_t _GetDescription_Request_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, chaos_interfaces, srv, GetDescription_Request)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, GetDescription_Request)),
  }
};

typedef struct _GetDescription_Request_type_support_data_t
{
  void * data[2];
} _GetDescription_Request_type_support_data_t;

static _GetDescription_Request_type_support_data_t _GetDescription_Request_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _GetDescription_Request_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_GetDescription_Request_message_typesupport_ids.typesupport_identifier[0],
  &_GetDescription_Request_message_typesupport_symbol_names.symbol_name[0],
  &_GetDescription_Request_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t GetDescription_Request_message_type_support_handle = {
  rosidl_typesupport_c__typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_GetDescription_Request_message_typesupport_map),
  rosidl_typesupport_c__get_message_typesupport_handle_function,
  &chaos_interfaces__srv__GetDescription_Request__get_type_hash,
  &chaos_interfaces__srv__GetDescription_Request__get_type_description,
  &chaos_interfaces__srv__GetDescription_Request__get_type_description_sources,
};

}  // namespace rosidl_typesupport_c

}  // namespace srv

}  // namespace chaos_interfaces

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_c, chaos_interfaces, srv, GetDescription_Request)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_c::GetDescription_Request_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__struct.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__type_support.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__functions.h"
// already included above
// #include "rosidl_typesupport_c/identifier.h"
// already included above
// #include "rosidl_typesupport_c/message_type_support_dispatch.h"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_c/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_c
{

typedef struct _GetDescription_Response_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _GetDescription_Response_type_support_ids_t;

static const _GetDescription_Response_type_support_ids_t _GetDescription_Response_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_c",  // ::rosidl_typesupport_fastrtps_c::typesupport_identifier,
    "rosidl_typesupport_introspection_c",  // ::rosidl_typesupport_introspection_c::typesupport_identifier,
  }
};

typedef struct _GetDescription_Response_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _GetDescription_Response_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _GetDescription_Response_type_support_symbol_names_t _GetDescription_Response_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, chaos_interfaces, srv, GetDescription_Response)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, GetDescription_Response)),
  }
};

typedef struct _GetDescription_Response_type_support_data_t
{
  void * data[2];
} _GetDescription_Response_type_support_data_t;

static _GetDescription_Response_type_support_data_t _GetDescription_Response_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _GetDescription_Response_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_GetDescription_Response_message_typesupport_ids.typesupport_identifier[0],
  &_GetDescription_Response_message_typesupport_symbol_names.symbol_name[0],
  &_GetDescription_Response_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t GetDescription_Response_message_type_support_handle = {
  rosidl_typesupport_c__typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_GetDescription_Response_message_typesupport_map),
  rosidl_typesupport_c__get_message_typesupport_handle_function,
  &chaos_interfaces__srv__GetDescription_Response__get_type_hash,
  &chaos_interfaces__srv__GetDescription_Response__get_type_description,
  &chaos_interfaces__srv__GetDescription_Response__get_type_description_sources,
};

}  // namespace rosidl_typesupport_c

}  // namespace srv

}  // namespace chaos_interfaces

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_c, chaos_interfaces, srv, GetDescription_Response)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_c::GetDescription_Response_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__struct.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__type_support.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__functions.h"
// already included above
// #include "rosidl_typesupport_c/identifier.h"
// already included above
// #include "rosidl_typesupport_c/message_type_support_dispatch.h"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_c/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_c
{

typedef struct _GetDescription_Event_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _GetDescription_Event_type_support_ids_t;

static const _GetDescription_Event_type_support_ids_t _GetDescription_Event_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_c",  // ::rosidl_typesupport_fastrtps_c::typesupport_identifier,
    "rosidl_typesupport_introspection_c",  // ::rosidl_typesupport_introspection_c::typesupport_identifier,
  }
};

typedef struct _GetDescription_Event_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _GetDescription_Event_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _GetDescription_Event_type_support_symbol_names_t _GetDescription_Event_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, chaos_interfaces, srv, GetDescription_Event)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, GetDescription_Event)),
  }
};

typedef struct _GetDescription_Event_type_support_data_t
{
  void * data[2];
} _GetDescription_Event_type_support_data_t;

static _GetDescription_Event_type_support_data_t _GetDescription_Event_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _GetDescription_Event_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_GetDescription_Event_message_typesupport_ids.typesupport_identifier[0],
  &_GetDescription_Event_message_typesupport_symbol_names.symbol_name[0],
  &_GetDescription_Event_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t GetDescription_Event_message_type_support_handle = {
  rosidl_typesupport_c__typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_GetDescription_Event_message_typesupport_map),
  rosidl_typesupport_c__get_message_typesupport_handle_function,
  &chaos_interfaces__srv__GetDescription_Event__get_type_hash,
  &chaos_interfaces__srv__GetDescription_Event__get_type_description,
  &chaos_interfaces__srv__GetDescription_Event__get_type_description_sources,
};

}  // namespace rosidl_typesupport_c

}  // namespace srv

}  // namespace chaos_interfaces

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_c, chaos_interfaces, srv, GetDescription_Event)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_c::GetDescription_Event_message_type_support_handle;
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "cstddef"
#include "rosidl_runtime_c/service_type_support_struct.h"
// already included above
// #include "chaos_interfaces/srv/detail/get_description__type_support.h"
// already included above
// #include "rosidl_typesupport_c/identifier.h"
#include "rosidl_typesupport_c/service_type_support_dispatch.h"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"
#include "service_msgs/msg/service_event_info.h"
#include "builtin_interfaces/msg/time.h"

namespace chaos_interfaces
{

namespace srv
{

namespace rosidl_typesupport_c
{
typedef struct _GetDescription_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _GetDescription_type_support_ids_t;

static const _GetDescription_type_support_ids_t _GetDescription_service_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_c",  // ::rosidl_typesupport_fastrtps_c::typesupport_identifier,
    "rosidl_typesupport_introspection_c",  // ::rosidl_typesupport_introspection_c::typesupport_identifier,
  }
};

typedef struct _GetDescription_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _GetDescription_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _GetDescription_type_support_symbol_names_t _GetDescription_service_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_fastrtps_c, chaos_interfaces, srv, GetDescription)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_c, chaos_interfaces, srv, GetDescription)),
  }
};

typedef struct _GetDescription_type_support_data_t
{
  void * data[2];
} _GetDescription_type_support_data_t;

static _GetDescription_type_support_data_t _GetDescription_service_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _GetDescription_service_typesupport_map = {
  2,
  "chaos_interfaces",
  &_GetDescription_service_typesupport_ids.typesupport_identifier[0],
  &_GetDescription_service_typesupport_symbol_names.symbol_name[0],
  &_GetDescription_service_typesupport_data.data[0],
};

static const rosidl_service_type_support_t GetDescription_service_type_support_handle = {
  rosidl_typesupport_c__typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_GetDescription_service_typesupport_map),
  rosidl_typesupport_c__get_service_typesupport_handle_function,
  &GetDescription_Request_message_type_support_handle,
  &GetDescription_Response_message_type_support_handle,
  &GetDescription_Event_message_type_support_handle,
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_CREATE_EVENT_MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_c,
    chaos_interfaces,
    srv,
    GetDescription
  ),
  ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_DESTROY_EVENT_MESSAGE_SYMBOL_NAME(
    rosidl_typesupport_c,
    chaos_interfaces,
    srv,
    GetDescription
  ),
  &chaos_interfaces__srv__GetDescription__get_type_hash,
  &chaos_interfaces__srv__GetDescription__get_type_description,
  &chaos_interfaces__srv__GetDescription__get_type_description_sources,
};

}  // namespace rosidl_typesupport_c

}  // namespace srv

}  // namespace chaos_interfaces

#ifdef __cplusplus
extern "C"
{
#endif

const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_c, chaos_interfaces, srv, GetDescription)() {
  return &::chaos_interfaces::srv::rosidl_typesupport_c::GetDescription_service_type_support_handle;
}

#ifdef __cplusplus
}
#endif
