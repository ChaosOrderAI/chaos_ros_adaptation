// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from chaos_interfaces:srv/Stop.idl
// generated code does not contain a copyright notice

#include "chaos_interfaces/srv/detail/stop__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__Stop__get_type_hash(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x8a, 0xa8, 0x1a, 0x45, 0x7d, 0x50, 0x48, 0xf3,
      0x54, 0xa8, 0x61, 0x7b, 0x61, 0x2e, 0x14, 0x22,
      0x2a, 0xc7, 0xb9, 0x25, 0xb9, 0xb3, 0x3e, 0x86,
      0x93, 0xfe, 0x8a, 0xd0, 0x2e, 0x73, 0x88, 0x82,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__Stop_Request__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xfa, 0x3f, 0x16, 0x3c, 0xc6, 0xb7, 0x0e, 0x92,
      0x86, 0x5b, 0x76, 0xde, 0x84, 0xf9, 0x1b, 0x56,
      0x20, 0xc4, 0xff, 0x96, 0x6b, 0x78, 0xe2, 0xa7,
      0xc7, 0xb5, 0x37, 0x69, 0xb3, 0xb4, 0xf5, 0xf4,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__Stop_Response__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x6d, 0xb1, 0xfd, 0x0a, 0x83, 0x49, 0x44, 0x94,
      0x0a, 0xf2, 0x0a, 0xc0, 0xb8, 0x17, 0x35, 0xf0,
      0x2a, 0xb3, 0xc4, 0xb1, 0xb9, 0x03, 0x14, 0x4c,
      0x5d, 0xaf, 0x10, 0xac, 0xcb, 0x30, 0x24, 0x55,
    }};
  return &hash;
}

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__srv__Stop_Event__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xca, 0xac, 0x6d, 0x5f, 0x21, 0x1b, 0xd2, 0x27,
      0x0b, 0xeb, 0x85, 0x22, 0x89, 0xec, 0xa8, 0x0f,
      0xa9, 0x07, 0x0a, 0xa6, 0x8c, 0xaf, 0x10, 0xf0,
      0x01, 0x8d, 0x61, 0x69, 0x55, 0xda, 0x6d, 0x4e,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "service_msgs/msg/detail/service_event_info__functions.h"
#include "builtin_interfaces/msg/detail/time__functions.h"

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

static char chaos_interfaces__srv__Stop__TYPE_NAME[] = "chaos_interfaces/srv/Stop";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";
static char chaos_interfaces__srv__Stop_Event__TYPE_NAME[] = "chaos_interfaces/srv/Stop_Event";
static char chaos_interfaces__srv__Stop_Request__TYPE_NAME[] = "chaos_interfaces/srv/Stop_Request";
static char chaos_interfaces__srv__Stop_Response__TYPE_NAME[] = "chaos_interfaces/srv/Stop_Response";
static char service_msgs__msg__ServiceEventInfo__TYPE_NAME[] = "service_msgs/msg/ServiceEventInfo";

// Define type names, field names, and default values
static char chaos_interfaces__srv__Stop__FIELD_NAME__request_message[] = "request_message";
static char chaos_interfaces__srv__Stop__FIELD_NAME__response_message[] = "response_message";
static char chaos_interfaces__srv__Stop__FIELD_NAME__event_message[] = "event_message";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__Stop__FIELDS[] = {
  {
    {chaos_interfaces__srv__Stop__FIELD_NAME__request_message, 15, 15},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {chaos_interfaces__srv__Stop_Request__TYPE_NAME, 33, 33},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop__FIELD_NAME__response_message, 16, 16},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {chaos_interfaces__srv__Stop_Response__TYPE_NAME, 34, 34},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop__FIELD_NAME__event_message, 13, 13},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {chaos_interfaces__srv__Stop_Event__TYPE_NAME, 31, 31},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__srv__Stop__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Event__TYPE_NAME, 31, 31},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Request__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Response__TYPE_NAME, 34, 34},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__Stop__get_type_description(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__Stop__TYPE_NAME, 25, 25},
      {chaos_interfaces__srv__Stop__FIELDS, 3, 3},
    },
    {chaos_interfaces__srv__Stop__REFERENCED_TYPE_DESCRIPTIONS, 5, 5},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = chaos_interfaces__srv__Stop_Event__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = chaos_interfaces__srv__Stop_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[3].fields = chaos_interfaces__srv__Stop_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[4].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char chaos_interfaces__srv__Stop_Request__FIELD_NAME__request_id[] = "request_id";
static char chaos_interfaces__srv__Stop_Request__FIELD_NAME__lease_token[] = "lease_token";
static char chaos_interfaces__srv__Stop_Request__FIELD_NAME__expires_at[] = "expires_at";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__Stop_Request__FIELDS[] = {
  {
    {chaos_interfaces__srv__Stop_Request__FIELD_NAME__request_id, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Request__FIELD_NAME__lease_token, 11, 11},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Request__FIELD_NAME__expires_at, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__srv__Stop_Request__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__Stop_Request__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__Stop_Request__TYPE_NAME, 33, 33},
      {chaos_interfaces__srv__Stop_Request__FIELDS, 3, 3},
    },
    {chaos_interfaces__srv__Stop_Request__REFERENCED_TYPE_DESCRIPTIONS, 1, 1},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char chaos_interfaces__srv__Stop_Response__FIELD_NAME__success[] = "success";
static char chaos_interfaces__srv__Stop_Response__FIELD_NAME__code[] = "code";
static char chaos_interfaces__srv__Stop_Response__FIELD_NAME__message[] = "message";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__Stop_Response__FIELDS[] = {
  {
    {chaos_interfaces__srv__Stop_Response__FIELD_NAME__success, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Response__FIELD_NAME__code, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Response__FIELD_NAME__message, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__Stop_Response__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__Stop_Response__TYPE_NAME, 34, 34},
      {chaos_interfaces__srv__Stop_Response__FIELDS, 3, 3},
    },
    {NULL, 0, 0},
  };
  if (!constructed) {
    constructed = true;
  }
  return &description;
}
// Define type names, field names, and default values
static char chaos_interfaces__srv__Stop_Event__FIELD_NAME__info[] = "info";
static char chaos_interfaces__srv__Stop_Event__FIELD_NAME__request[] = "request";
static char chaos_interfaces__srv__Stop_Event__FIELD_NAME__response[] = "response";

static rosidl_runtime_c__type_description__Field chaos_interfaces__srv__Stop_Event__FIELDS[] = {
  {
    {chaos_interfaces__srv__Stop_Event__FIELD_NAME__info, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Event__FIELD_NAME__request, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {chaos_interfaces__srv__Stop_Request__TYPE_NAME, 33, 33},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Event__FIELD_NAME__response, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE_BOUNDED_SEQUENCE,
      1,
      0,
      {chaos_interfaces__srv__Stop_Response__TYPE_NAME, 34, 34},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__srv__Stop_Event__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Request__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__srv__Stop_Response__TYPE_NAME, 34, 34},
    {NULL, 0, 0},
  },
  {
    {service_msgs__msg__ServiceEventInfo__TYPE_NAME, 33, 33},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__srv__Stop_Event__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__srv__Stop_Event__TYPE_NAME, 31, 31},
      {chaos_interfaces__srv__Stop_Event__FIELDS, 3, 3},
    },
    {chaos_interfaces__srv__Stop_Event__REFERENCED_TYPE_DESCRIPTIONS, 4, 4},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[1].fields = chaos_interfaces__srv__Stop_Request__get_type_description(NULL)->type_description.fields;
    description.referenced_type_descriptions.data[2].fields = chaos_interfaces__srv__Stop_Response__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&service_msgs__msg__ServiceEventInfo__EXPECTED_HASH, service_msgs__msg__ServiceEventInfo__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[3].fields = service_msgs__msg__ServiceEventInfo__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "string request_id\n"
  "string lease_token\n"
  "# Unix UTC deadline for accepting this request; zero means no transport deadline.\n"
  "builtin_interfaces/Time expires_at\n"
  "---\n"
  "bool success\n"
  "string code\n"
  "string message";

static char srv_encoding[] = "srv";
static char implicit_encoding[] = "implicit";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__Stop__get_individual_type_description_source(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__Stop__TYPE_NAME, 25, 25},
    {srv_encoding, 3, 3},
    {toplevel_type_raw_source, 198, 198},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__Stop_Request__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__Stop_Request__TYPE_NAME, 33, 33},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__Stop_Response__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__Stop_Response__TYPE_NAME, 34, 34},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__srv__Stop_Event__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__srv__Stop_Event__TYPE_NAME, 31, 31},
    {implicit_encoding, 8, 8},
    {NULL, 0, 0},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__Stop__get_type_description_sources(
  const rosidl_service_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[6];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 6, 6};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__Stop__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *chaos_interfaces__srv__Stop_Event__get_individual_type_description_source(NULL);
    sources[3] = *chaos_interfaces__srv__Stop_Request__get_individual_type_description_source(NULL);
    sources[4] = *chaos_interfaces__srv__Stop_Response__get_individual_type_description_source(NULL);
    sources[5] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__Stop_Request__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[2];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 2, 2};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__Stop_Request__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__Stop_Response__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[1];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 1, 1};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__Stop_Response__get_individual_type_description_source(NULL),
    constructed = true;
  }
  return &source_sequence;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__srv__Stop_Event__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[5];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 5, 5};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__srv__Stop_Event__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *chaos_interfaces__srv__Stop_Request__get_individual_type_description_source(NULL);
    sources[3] = *chaos_interfaces__srv__Stop_Response__get_individual_type_description_source(NULL);
    sources[4] = *service_msgs__msg__ServiceEventInfo__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
