// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from chaos_interfaces:srv/ControlLease.idl
// generated code does not contain a copyright notice

#include "chaos_interfaces/srv/detail/control_lease__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease__get_type_hash(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xc7, 0xf4, 0x0a, 0x66, 0x05, 0xfd, 0xd6, 0x97,
      0xb4, 0xc7, 0xd6, 0xf2, 0xfa, 0xf7, 0x4c, 0x2b,
      0x23, 0xe3, 0x69, 0x66, 0x13, 0xa4, 0x8d, 0xcd,
      0x1a, 0xf6, 0x2f, 0x97, 0xa3, 0x71, 0xa0, 0xa9,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease_Request__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x51, 0xdd, 0xbb, 0xab, 0xb0, 0xfe, 0xc0, 0x7d,
      0xb2, 0xf7, 0xf9, 0xd8, 0x64, 0xd8, 0x29, 0x8e,
      0xa6, 0x47, 0x23, 0x4b, 0x13, 0x8e, 0x35, 0x42,
      0x27, 0xec, 0x19, 0xae, 0xd3, 0x71, 0x9f, 0xec,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease_Response__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xe2, 0x75, 0xbd, 0x5b, 0x73, 0xcc, 0x6d, 0x55,
      0x38, 0xd6, 0xae, 0xfb, 0xbb, 0xf0, 0x91, 0xcc,
      0xbd, 0x20, 0x6b, 0x7b, 0xae, 0xfb, 0x45, 0x7a,
      0x42, 0xb1, 0x58, 0x4f, 0xa1, 0x22, 0xbc, 0x00,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__ControlLease_Event__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x66, 0x30, 0x84, 0xd9, 0xd6, 0x2b, 0x66, 0xf7,
      0x12, 0xdf, 0x8d, 0xac, 0x5d, 0x68, 0x2b, 0xdc,
      0x67, 0xf0, 0xa3, 0xbe, 0xbe, 0xac, 0x1f, 0xb3,
      0xbf, 0xe6, 0xaa, 0xfc, 0x10, 0xb7, 0xf8, 0x2d,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "builtin_interfaces/msg/detail/time__functions.h"
#include "service_msgs/msg/detail/service_event_info__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
static const rosidl_type_hash_t service_msgs__msg__ServiceEventInfo__EXPECTED_HASH = {1, {
    0x41, 0xbc, 0xbb, 0xe0, 0x7a, 0x75, 0xc9, 0xb5,
    0x2b, 0xc9, 0x6b, 0xfd, 0x5c, 0x24, 0xd7, 0xf0,
    0xfc, 0x0a, 0x08, 0xc0, 0xcb, 0x79, 0x21, 0xb3,
    0x37, 0x3c, 0x57, 0x32, 0x34, 0x5a, 0x6f, 0x45,
  }};
#endif

static char chaos_interfaces__srv__ControlLease__TYPE_NAME[] = "chaos_interfaces/srv/ControlLease";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";
static char chaos_interfaces__srv__ControlLease_Event__TYPE_NAME[] = "chaos_interfaces/srv/ControlLease_Event";
static char chaos_interfaces__srv__ControlLease_Request__TYPE_NAME[] = "chaos_interfaces/srv/ControlLease_Request";
static char chaos_interfaces__srv__ControlLease_Response__TYPE_NAME[] = "chaos_interfaces/srv/ControlLease_Response";
static char service_msgs__msg__ServiceEventInfo__TYPE_NAME[] = "service_msgs/msg/ServiceEventInfo";

// Define type names, field names, and default values
static char chaos_interfaces__srv__ControlLease__FIELD_NAME__request_message[] = "request_message";
static char chaos_interfaces__srv__ControlLease__FIELD_NAME__response_message[] = "response_message";
static char chaos_interfaces__srv__ControlLease__FIELD_NAME__event_message[] = "event_message";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__ControlLease__FIELDS[] = {
  {
    {chaos_interfaces__srv__ControlLease__FIELD_NAME__request_message, 15, 15},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {chaos_interfaces__srv__ControlLease_Request__TYPE_NAME, 41, 41},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease__FIELD_NAME__response_message, 16, 16},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {chaos_interfaces__srv__ControlLease_Response__TYPE_NAME, 42, 42},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease__FIELD_NAME__event_message, 13, 13},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {chaos_interfaces__srv__ControlLease_Event__TYPE_NAME, 39, 39},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__srv__ControlLease__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Event__TYPE_NAME, 39, 39},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Request__TYPE_NAME, 41, 41},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Response__TYPE_NAME, 42, 42},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease__get_type_description(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__ControlLease__TYPE_NAME, 33, 33},
      {chaos_interfaces__srv__ControlLease__FIELDS, 3, 3},
    },
    {chaos_interfaces__srv__ControlLease__REFERENCED_TYPE_DESCRIPTIONS, 5, 5},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = chaos_interfaces__srv__ControlLease_Event__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = chaos_interfaces__srv__ControlLease_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[3].fields = chaos_interfaces__srv__ControlLease_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[4].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__operation[] = "operation";
static char chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__owner[] = "owner";
static char chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__token[] = "token";
static char chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__duration_s[] = "duration_s";
static char chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__expires_at[] = "expires_at";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__ControlLease_Request__FIELDS[] = {
  {
    {chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__operation, 9, 9},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__owner, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__token, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__duration_s, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Request__FIELD_NAME__expires_at, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__srv__ControlLease_Request__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease_Request__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__ControlLease_Request__TYPE_NAME, 41, 41},
      {chaos_interfaces__srv__ControlLease_Request__FIELDS, 5, 5},
    },
    {chaos_interfaces__srv__ControlLease_Request__REFERENCED_TYPE_DESCRIPTIONS, 1, 1},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__success[] = "success";
static char chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__code[] = "code";
static char chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__message[] = "message";
static char chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__token[] = "token";
static char chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__remaining_s[] = "remaining_s";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__ControlLease_Response__FIELDS[] = {
  {
    {chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__success, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__code, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__message, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__token, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Response__FIELD_NAME__remaining_s, 11, 11},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease_Response__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__ControlLease_Response__TYPE_NAME, 42, 42},
      {chaos_interfaces__srv__ControlLease_Response__FIELDS, 5, 5},
    },
    {NULL, 0, 0},
  };
  if (!constructed) {
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char chaos_interfaces__srv__ControlLease_Event__FIELD_NAME__info[] = "info";
static char chaos_interfaces__srv__ControlLease_Event__FIELD_NAME__request[] = "request";
static char chaos_interfaces__srv__ControlLease_Event__FIELD_NAME__response[] = "response";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__ControlLease_Event__FIELDS[] = {
  {
    {chaos_interfaces__srv__ControlLease_Event__FIELD_NAME__info, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Event__FIELD_NAME__request, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {chaos_interfaces__srv__ControlLease_Request__TYPE_NAME, 41, 41},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Event__FIELD_NAME__response, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {chaos_interfaces__srv__ControlLease_Response__TYPE_NAME, 42, 42},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__srv__ControlLease_Event__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Request__TYPE_NAME, 41, 41},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__ControlLease_Response__TYPE_NAME, 42, 42},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__ControlLease_Event__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__ControlLease_Event__TYPE_NAME, 39, 39},
      {chaos_interfaces__srv__ControlLease_Event__FIELDS, 3, 3},
    },
    {chaos_interfaces__srv__ControlLease_Event__REFERENCED_TYPE_DESCRIPTIONS, 4, 4},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = chaos_interfaces__srv__ControlLease_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = chaos_interfaces__srv__ControlLease_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[3].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "# acquire | renew | release; owner is required. Renew/release require token.\n"
  "string operation\n"
  "string owner\n"
  "string token\n"
  "float64 duration_s\n"
  "# Unix UTC deadline for accepting this request; zero means no transport deadline.\n"
  "builtin_interfaces/Time expires_at\n"
  "---\n"
  "bool success\n"
  "string code\n"
  "string message\n"
  "string token\n"
  "float64 remaining_s";

static char srv_encoding[] = "srv";
static char implicit_encoding[] = "implicit";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease__get_individual_type_description_source(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__ControlLease__TYPE_NAME, 33, 33},
    {srv_encoding, 3, 3},
    {toplevel_type_raw_source, 333, 333},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease_Request__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__ControlLease_Request__TYPE_NAME, 41, 41},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease_Response__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__ControlLease_Response__TYPE_NAME, 42, 42},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__ControlLease_Event__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__ControlLease_Event__TYPE_NAME, 39, 39},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease__get_type_description_sources(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[6];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 6, 6};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__ControlLease__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *chaos_interfaces__srv__ControlLease_Event__get_individual_type_description_source(NULL);
    sources[3] = *chaos_interfaces__srv__ControlLease_Request__get_individual_type_description_source(NULL);
    sources[4] = *chaos_interfaces__srv__ControlLease_Response__get_individual_type_description_source(NULL);
    sources[5] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease_Request__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[2];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 2, 2};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__ControlLease_Request__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease_Response__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[1];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 1, 1};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__ControlLease_Response__get_individual_type_description_source(NULL),
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__ControlLease_Event__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[5];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 5, 5};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__ControlLease_Event__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *chaos_interfaces__srv__ControlLease_Request__get_individual_type_description_source(NULL);
    sources[3] = *chaos_interfaces__srv__ControlLease_Response__get_individual_type_description_source(NULL);
    sources[4] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
