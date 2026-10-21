// generated from rosidl_typesupport_cpp/resource/idl__type_support.cpp.em
// with input from chaos_interfaces:action/NavigatePose.idl
// generated code does not contain a copyright notice

#include "cstddef"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "chaos_interfaces/action/detail/navigate_pose__functions.h"
#include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
#include "rosidl_typesupport_cpp/identifier.hpp"
#include "rosidl_typesupport_cpp/message_type_support.hpp"
#include "rosidl_typesupport_c/type_support_map.h"
#include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
#include "rosidl_typesupport_cpp/visibility_control.h"
#include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_Goal_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_Goal_type_support_ids_t;

static const _NavigatePose_Goal_type_support_ids_t _NavigatePose_Goal_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_Goal_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_Goal_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_Goal_type_support_symbol_names_t _NavigatePose_Goal_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_Goal)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_Goal)),
  }
};

typedef struct _NavigatePose_Goal_type_support_data_t
{
  void * data[2];
} _NavigatePose_Goal_type_support_data_t;

static _NavigatePose_Goal_type_support_data_t _NavigatePose_Goal_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_Goal_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_Goal_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_Goal_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_Goal_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_Goal_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_Goal_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_Goal__get_type_hash,
  &chaos_interfaces__action__NavigatePose_Goal__get_type_description,
  &chaos_interfaces__action__NavigatePose_Goal__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_Goal>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_Goal_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_Goal)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_Goal>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_Result_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_Result_type_support_ids_t;

static const _NavigatePose_Result_type_support_ids_t _NavigatePose_Result_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_Result_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_Result_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_Result_type_support_symbol_names_t _NavigatePose_Result_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_Result)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_Result)),
  }
};

typedef struct _NavigatePose_Result_type_support_data_t
{
  void * data[2];
} _NavigatePose_Result_type_support_data_t;

static _NavigatePose_Result_type_support_data_t _NavigatePose_Result_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_Result_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_Result_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_Result_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_Result_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_Result_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_Result_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_Result__get_type_hash,
  &chaos_interfaces__action__NavigatePose_Result__get_type_description,
  &chaos_interfaces__action__NavigatePose_Result__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_Result>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_Result_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_Result)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_Result>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_Feedback_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_Feedback_type_support_ids_t;

static const _NavigatePose_Feedback_type_support_ids_t _NavigatePose_Feedback_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_Feedback_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_Feedback_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_Feedback_type_support_symbol_names_t _NavigatePose_Feedback_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_Feedback)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_Feedback)),
  }
};

typedef struct _NavigatePose_Feedback_type_support_data_t
{
  void * data[2];
} _NavigatePose_Feedback_type_support_data_t;

static _NavigatePose_Feedback_type_support_data_t _NavigatePose_Feedback_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_Feedback_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_Feedback_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_Feedback_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_Feedback_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_Feedback_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_Feedback_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_Feedback__get_type_hash,
  &chaos_interfaces__action__NavigatePose_Feedback__get_type_description,
  &chaos_interfaces__action__NavigatePose_Feedback__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_Feedback>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_Feedback_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_Feedback)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_Feedback>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_SendGoal_Request_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_SendGoal_Request_type_support_ids_t;

static const _NavigatePose_SendGoal_Request_type_support_ids_t _NavigatePose_SendGoal_Request_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_SendGoal_Request_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_SendGoal_Request_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_SendGoal_Request_type_support_symbol_names_t _NavigatePose_SendGoal_Request_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Request)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Request)),
  }
};

typedef struct _NavigatePose_SendGoal_Request_type_support_data_t
{
  void * data[2];
} _NavigatePose_SendGoal_Request_type_support_data_t;

static _NavigatePose_SendGoal_Request_type_support_data_t _NavigatePose_SendGoal_Request_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_SendGoal_Request_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_SendGoal_Request_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_SendGoal_Request_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_SendGoal_Request_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_SendGoal_Request_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_SendGoal_Request_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_SendGoal_Request__get_type_hash,
  &chaos_interfaces__action__NavigatePose_SendGoal_Request__get_type_description,
  &chaos_interfaces__action__NavigatePose_SendGoal_Request__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Request>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_SendGoal_Request_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Request)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Request>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_SendGoal_Response_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_SendGoal_Response_type_support_ids_t;

static const _NavigatePose_SendGoal_Response_type_support_ids_t _NavigatePose_SendGoal_Response_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_SendGoal_Response_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_SendGoal_Response_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_SendGoal_Response_type_support_symbol_names_t _NavigatePose_SendGoal_Response_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Response)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Response)),
  }
};

typedef struct _NavigatePose_SendGoal_Response_type_support_data_t
{
  void * data[2];
} _NavigatePose_SendGoal_Response_type_support_data_t;

static _NavigatePose_SendGoal_Response_type_support_data_t _NavigatePose_SendGoal_Response_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_SendGoal_Response_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_SendGoal_Response_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_SendGoal_Response_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_SendGoal_Response_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_SendGoal_Response_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_SendGoal_Response_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_SendGoal_Response__get_type_hash,
  &chaos_interfaces__action__NavigatePose_SendGoal_Response__get_type_description,
  &chaos_interfaces__action__NavigatePose_SendGoal_Response__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Response>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_SendGoal_Response_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Response)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Response>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_SendGoal_Event_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_SendGoal_Event_type_support_ids_t;

static const _NavigatePose_SendGoal_Event_type_support_ids_t _NavigatePose_SendGoal_Event_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_SendGoal_Event_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_SendGoal_Event_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_SendGoal_Event_type_support_symbol_names_t _NavigatePose_SendGoal_Event_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Event)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Event)),
  }
};

typedef struct _NavigatePose_SendGoal_Event_type_support_data_t
{
  void * data[2];
} _NavigatePose_SendGoal_Event_type_support_data_t;

static _NavigatePose_SendGoal_Event_type_support_data_t _NavigatePose_SendGoal_Event_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_SendGoal_Event_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_SendGoal_Event_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_SendGoal_Event_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_SendGoal_Event_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_SendGoal_Event_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_SendGoal_Event_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_SendGoal_Event__get_type_hash,
  &chaos_interfaces__action__NavigatePose_SendGoal_Event__get_type_description,
  &chaos_interfaces__action__NavigatePose_SendGoal_Event__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Event>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_SendGoal_Event_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_SendGoal_Event)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Event>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_typesupport_cpp/service_type_support.hpp"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
#include "rosidl_typesupport_cpp/service_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_SendGoal_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_SendGoal_type_support_ids_t;

static const _NavigatePose_SendGoal_type_support_ids_t _NavigatePose_SendGoal_service_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_SendGoal_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_SendGoal_type_support_symbol_names_t;
#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_SendGoal_type_support_symbol_names_t _NavigatePose_SendGoal_service_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_SendGoal)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_SendGoal)),
  }
};

typedef struct _NavigatePose_SendGoal_type_support_data_t
{
  void * data[2];
} _NavigatePose_SendGoal_type_support_data_t;

static _NavigatePose_SendGoal_type_support_data_t _NavigatePose_SendGoal_service_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_SendGoal_service_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_SendGoal_service_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_SendGoal_service_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_SendGoal_service_typesupport_data.data[0],
};

static const rosidl_service_type_support_t NavigatePose_SendGoal_service_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_SendGoal_service_typesupport_map),
  ::rosidl_typesupport_cpp::get_service_typesupport_handle_function,
  ::rosidl_typesupport_cpp::get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Request>(),
  ::rosidl_typesupport_cpp::get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Response>(),
  ::rosidl_typesupport_cpp::get_message_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal_Event>(),
  &::rosidl_typesupport_cpp::service_create_event_message<chaos_interfaces::action::NavigatePose_SendGoal>,
  &::rosidl_typesupport_cpp::service_destroy_event_message<chaos_interfaces::action::NavigatePose_SendGoal>,
  &chaos_interfaces__action__NavigatePose_SendGoal__get_type_hash,
  &chaos_interfaces__action__NavigatePose_SendGoal__get_type_description,
  &chaos_interfaces__action__NavigatePose_SendGoal__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_service_type_support_t *
get_service_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_SendGoal_service_type_support_handle;
}

}  // namespace rosidl_typesupport_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_SendGoal)() {
  return ::rosidl_typesupport_cpp::get_service_type_support_handle<chaos_interfaces::action::NavigatePose_SendGoal>();
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_GetResult_Request_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_GetResult_Request_type_support_ids_t;

static const _NavigatePose_GetResult_Request_type_support_ids_t _NavigatePose_GetResult_Request_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_GetResult_Request_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_GetResult_Request_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_GetResult_Request_type_support_symbol_names_t _NavigatePose_GetResult_Request_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_GetResult_Request)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_GetResult_Request)),
  }
};

typedef struct _NavigatePose_GetResult_Request_type_support_data_t
{
  void * data[2];
} _NavigatePose_GetResult_Request_type_support_data_t;

static _NavigatePose_GetResult_Request_type_support_data_t _NavigatePose_GetResult_Request_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_GetResult_Request_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_GetResult_Request_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_GetResult_Request_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_GetResult_Request_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_GetResult_Request_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_GetResult_Request_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_GetResult_Request__get_type_hash,
  &chaos_interfaces__action__NavigatePose_GetResult_Request__get_type_description,
  &chaos_interfaces__action__NavigatePose_GetResult_Request__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Request>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_GetResult_Request_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_GetResult_Request)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Request>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_GetResult_Response_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_GetResult_Response_type_support_ids_t;

static const _NavigatePose_GetResult_Response_type_support_ids_t _NavigatePose_GetResult_Response_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_GetResult_Response_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_GetResult_Response_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_GetResult_Response_type_support_symbol_names_t _NavigatePose_GetResult_Response_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_GetResult_Response)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_GetResult_Response)),
  }
};

typedef struct _NavigatePose_GetResult_Response_type_support_data_t
{
  void * data[2];
} _NavigatePose_GetResult_Response_type_support_data_t;

static _NavigatePose_GetResult_Response_type_support_data_t _NavigatePose_GetResult_Response_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_GetResult_Response_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_GetResult_Response_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_GetResult_Response_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_GetResult_Response_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_GetResult_Response_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_GetResult_Response_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_GetResult_Response__get_type_hash,
  &chaos_interfaces__action__NavigatePose_GetResult_Response__get_type_description,
  &chaos_interfaces__action__NavigatePose_GetResult_Response__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Response>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_GetResult_Response_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_GetResult_Response)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Response>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_GetResult_Event_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_GetResult_Event_type_support_ids_t;

static const _NavigatePose_GetResult_Event_type_support_ids_t _NavigatePose_GetResult_Event_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_GetResult_Event_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_GetResult_Event_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_GetResult_Event_type_support_symbol_names_t _NavigatePose_GetResult_Event_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_GetResult_Event)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_GetResult_Event)),
  }
};

typedef struct _NavigatePose_GetResult_Event_type_support_data_t
{
  void * data[2];
} _NavigatePose_GetResult_Event_type_support_data_t;

static _NavigatePose_GetResult_Event_type_support_data_t _NavigatePose_GetResult_Event_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_GetResult_Event_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_GetResult_Event_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_GetResult_Event_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_GetResult_Event_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_GetResult_Event_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_GetResult_Event_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_GetResult_Event__get_type_hash,
  &chaos_interfaces__action__NavigatePose_GetResult_Event__get_type_description,
  &chaos_interfaces__action__NavigatePose_GetResult_Event__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Event>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_GetResult_Event_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_GetResult_Event)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Event>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/service_type_support_struct.h"
// already included above
// #include "rosidl_typesupport_cpp/service_type_support.hpp"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/service_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_GetResult_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_GetResult_type_support_ids_t;

static const _NavigatePose_GetResult_type_support_ids_t _NavigatePose_GetResult_service_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_GetResult_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_GetResult_type_support_symbol_names_t;
#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_GetResult_type_support_symbol_names_t _NavigatePose_GetResult_service_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_GetResult)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_GetResult)),
  }
};

typedef struct _NavigatePose_GetResult_type_support_data_t
{
  void * data[2];
} _NavigatePose_GetResult_type_support_data_t;

static _NavigatePose_GetResult_type_support_data_t _NavigatePose_GetResult_service_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_GetResult_service_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_GetResult_service_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_GetResult_service_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_GetResult_service_typesupport_data.data[0],
};

static const rosidl_service_type_support_t NavigatePose_GetResult_service_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_GetResult_service_typesupport_map),
  ::rosidl_typesupport_cpp::get_service_typesupport_handle_function,
  ::rosidl_typesupport_cpp::get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Request>(),
  ::rosidl_typesupport_cpp::get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Response>(),
  ::rosidl_typesupport_cpp::get_message_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult_Event>(),
  &::rosidl_typesupport_cpp::service_create_event_message<chaos_interfaces::action::NavigatePose_GetResult>,
  &::rosidl_typesupport_cpp::service_destroy_event_message<chaos_interfaces::action::NavigatePose_GetResult>,
  &chaos_interfaces__action__NavigatePose_GetResult__get_type_hash,
  &chaos_interfaces__action__NavigatePose_GetResult__get_type_description,
  &chaos_interfaces__action__NavigatePose_GetResult__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_service_type_support_t *
get_service_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_GetResult_service_type_support_handle;
}

}  // namespace rosidl_typesupport_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_service_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__SERVICE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_GetResult)() {
  return ::rosidl_typesupport_cpp::get_service_type_support_handle<chaos_interfaces::action::NavigatePose_GetResult>();
}

#ifdef __cplusplus
}
#endif

// already included above
// #include "cstddef"
// already included above
// #include "rosidl_runtime_c/message_type_support_struct.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__functions.h"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/identifier.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_c/type_support_map.h"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support_dispatch.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
// already included above
// #include "rosidl_typesupport_interface/macros.h"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

typedef struct _NavigatePose_FeedbackMessage_type_support_ids_t
{
  const char * typesupport_identifier[2];
} _NavigatePose_FeedbackMessage_type_support_ids_t;

static const _NavigatePose_FeedbackMessage_type_support_ids_t _NavigatePose_FeedbackMessage_message_typesupport_ids = {
  {
    "rosidl_typesupport_fastrtps_cpp",  // ::rosidl_typesupport_fastrtps_cpp::typesupport_identifier,
    "rosidl_typesupport_introspection_cpp",  // ::rosidl_typesupport_introspection_cpp::typesupport_identifier,
  }
};

typedef struct _NavigatePose_FeedbackMessage_type_support_symbol_names_t
{
  const char * symbol_name[2];
} _NavigatePose_FeedbackMessage_type_support_symbol_names_t;

#define STRINGIFY_(s) #s
#define STRINGIFY(s) STRINGIFY_(s)

static const _NavigatePose_FeedbackMessage_type_support_symbol_names_t _NavigatePose_FeedbackMessage_message_typesupport_symbol_names = {
  {
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_fastrtps_cpp, chaos_interfaces, action, NavigatePose_FeedbackMessage)),
    STRINGIFY(ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_introspection_cpp, chaos_interfaces, action, NavigatePose_FeedbackMessage)),
  }
};

typedef struct _NavigatePose_FeedbackMessage_type_support_data_t
{
  void * data[2];
} _NavigatePose_FeedbackMessage_type_support_data_t;

static _NavigatePose_FeedbackMessage_type_support_data_t _NavigatePose_FeedbackMessage_message_typesupport_data = {
  {
    0,  // will store the shared library later
    0,  // will store the shared library later
  }
};

static const type_support_map_t _NavigatePose_FeedbackMessage_message_typesupport_map = {
  2,
  "chaos_interfaces",
  &_NavigatePose_FeedbackMessage_message_typesupport_ids.typesupport_identifier[0],
  &_NavigatePose_FeedbackMessage_message_typesupport_symbol_names.symbol_name[0],
  &_NavigatePose_FeedbackMessage_message_typesupport_data.data[0],
};

static const rosidl_message_type_support_t NavigatePose_FeedbackMessage_message_type_support_handle = {
  ::rosidl_typesupport_cpp::typesupport_identifier,
  reinterpret_cast<const type_support_map_t *>(&_NavigatePose_FeedbackMessage_message_typesupport_map),
  ::rosidl_typesupport_cpp::get_message_typesupport_handle_function,
  &chaos_interfaces__action__NavigatePose_FeedbackMessage__get_type_hash,
  &chaos_interfaces__action__NavigatePose_FeedbackMessage__get_type_description,
  &chaos_interfaces__action__NavigatePose_FeedbackMessage__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
get_message_type_support_handle<chaos_interfaces::action::NavigatePose_FeedbackMessage>()
{
  return &::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_FeedbackMessage_message_type_support_handle;
}

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_message_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__MESSAGE_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose_FeedbackMessage)() {
  return get_message_type_support_handle<chaos_interfaces::action::NavigatePose_FeedbackMessage>();
}

#ifdef __cplusplus
}
#endif
}  // namespace rosidl_typesupport_cpp

#include "action_msgs/msg/goal_status_array.hpp"
#include "action_msgs/srv/cancel_goal.hpp"
// already included above
// #include "chaos_interfaces/action/detail/navigate_pose__struct.hpp"
// already included above
// #include "rosidl_typesupport_cpp/visibility_control.h"
#include "rosidl_runtime_c/action_type_support_struct.h"
#include "rosidl_typesupport_cpp/action_type_support.hpp"
// already included above
// #include "rosidl_typesupport_cpp/message_type_support.hpp"
// already included above
// #include "rosidl_typesupport_cpp/service_type_support.hpp"

namespace chaos_interfaces
{

namespace action
{

namespace rosidl_typesupport_cpp
{

static rosidl_action_type_support_t NavigatePose_action_type_support_handle = {
  NULL, NULL, NULL, NULL, NULL,
  &chaos_interfaces__action__NavigatePose__get_type_hash,
  &chaos_interfaces__action__NavigatePose__get_type_description,
  &chaos_interfaces__action__NavigatePose__get_type_description_sources,
};

}  // namespace rosidl_typesupport_cpp

}  // namespace action

}  // namespace chaos_interfaces

namespace rosidl_typesupport_cpp
{

template<>
ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_action_type_support_t *
get_action_type_support_handle<chaos_interfaces::action::NavigatePose>()
{
  using ::chaos_interfaces::action::rosidl_typesupport_cpp::NavigatePose_action_type_support_handle;
  // Thread-safe by always writing the same values to the static struct
  NavigatePose_action_type_support_handle.goal_service_type_support = get_service_type_support_handle<::chaos_interfaces::action::NavigatePose::Impl::SendGoalService>();
  NavigatePose_action_type_support_handle.result_service_type_support = get_service_type_support_handle<::chaos_interfaces::action::NavigatePose::Impl::GetResultService>();
  NavigatePose_action_type_support_handle.cancel_service_type_support = get_service_type_support_handle<::chaos_interfaces::action::NavigatePose::Impl::CancelGoalService>();
  NavigatePose_action_type_support_handle.feedback_message_type_support = get_message_type_support_handle<::chaos_interfaces::action::NavigatePose::Impl::FeedbackMessage>();
  NavigatePose_action_type_support_handle.status_message_type_support = get_message_type_support_handle<::chaos_interfaces::action::NavigatePose::Impl::GoalStatusMessage>();
  return &NavigatePose_action_type_support_handle;
}

}  // namespace rosidl_typesupport_cpp

#ifdef __cplusplus
extern "C"
{
#endif

ROSIDL_TYPESUPPORT_CPP_PUBLIC
const rosidl_action_type_support_t *
ROSIDL_TYPESUPPORT_INTERFACE__ACTION_SYMBOL_NAME(rosidl_typesupport_cpp, chaos_interfaces, action, NavigatePose)() {
  return ::rosidl_typesupport_cpp::get_action_type_support_handle<chaos_interfaces::action::NavigatePose>();
}

#ifdef __cplusplus
}
#endif
