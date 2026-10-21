// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from chaos_interfaces:msg/ControlTrace.idl
// generated code does not contain a copyright notice

#include "chaos_interfaces/msg/detail/control_trace__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__msg__ControlTrace__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0xe4, 0x05, 0xa3, 0xe5, 0x57, 0x19, 0x45, 0x6f,
      0x66, 0xd0, 0x81, 0x48, 0xb2, 0xd7, 0x10, 0xe7,
      0x58, 0x54, 0x1d, 0x65, 0x42, 0xa8, 0xab, 0xe3,
      0x2c, 0x99, 0xa8, 0xb5, 0x29, 0x9c, 0xa7, 0x90,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "geometry_msgs/msg/detail/vector3__functions.h"
#include "geometry_msgs/msg/detail/twist__functions.h"
#include "std_msgs/msg/detail/header__functions.h"
#include "builtin_interfaces/msg/detail/time__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
static const rosidl_type_hash_t geometry_msgs__msg__Twist__EXPECTED_HASH = {1, {
    0x9c, 0x45, 0xbf, 0x16, 0xfe, 0x09, 0x83, 0xd8,
    0x0e, 0x3c, 0xfe, 0x75, 0x0d, 0x68, 0x35, 0x84,
    0x3d, 0x26, 0x5a, 0x9a, 0x6c, 0x46, 0xbd, 0x2e,
    0x60, 0x9f, 0xcd, 0xdd, 0xe6, 0xfb, 0x8d, 0x2a,
  }};
static const rosidl_type_hash_t geometry_msgs__msg__Vector3__EXPECTED_HASH = {1, {
    0xcc, 0x12, 0xfe, 0x83, 0xe4, 0xc0, 0x27, 0x19,
    0xf1, 0xce, 0x80, 0x70, 0xbf, 0xd1, 0x4a, 0xec,
    0xd4, 0x0f, 0x75, 0xa9, 0x66, 0x96, 0xa6, 0x7a,
    0x2a, 0x1f, 0x37, 0xf7, 0xdb, 0xb0, 0x76, 0x5d,
  }};
static const rosidl_type_hash_t std_msgs__msg__Header__EXPECTED_HASH = {1, {
    0xf4, 0x9f, 0xb3, 0xae, 0x2c, 0xf0, 0x70, 0xf7,
    0x93, 0x64, 0x5f, 0xf7, 0x49, 0x68, 0x3a, 0xc6,
    0xb0, 0x62, 0x03, 0xe4, 0x1c, 0x89, 0x1e, 0x17,
    0x70, 0x1b, 0x1c, 0xb5, 0x97, 0xce, 0x6a, 0x01,
  }};
#endif

static char chaos_interfaces__msg__ControlTrace__TYPE_NAME[] = "chaos_interfaces/msg/ControlTrace";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";
static char geometry_msgs__msg__Twist__TYPE_NAME[] = "geometry_msgs/msg/Twist";
static char geometry_msgs__msg__Vector3__TYPE_NAME[] = "geometry_msgs/msg/Vector3";
static char std_msgs__msg__Header__TYPE_NAME[] = "std_msgs/msg/Header";

// Define type names, field names, and default values
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__header[] = "header";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__asset_id[] = "asset_id";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__sequence[] = "sequence";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__monotonic_ns[] = "monotonic_ns";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__request_id[] = "request_id";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__command[] = "command";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__stage[] = "stage";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__velocity_valid[] = "velocity_valid";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__velocity[] = "velocity";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__valid_for_s[] = "valid_for_s";
static char chaos_interfaces__msg__ControlTrace__FIELD_NAME__code[] = "code";

static rosidl_runtime_c__type_description__Field chaos_interfaces__msg__ControlTrace__FIELDS[] = {
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__header, 6, 6},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {std_msgs__msg__Header__TYPE_NAME, 19, 19},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__asset_id, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__sequence, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_UINT64,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__monotonic_ns, 12, 12},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_UINT64,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__request_id, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__command, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__stage, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__velocity_valid, 14, 14},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__velocity, 8, 8},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {geometry_msgs__msg__Twist__TYPE_NAME, 23, 23},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__valid_for_s, 11, 11},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_DOUBLE,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__ControlTrace__FIELD_NAME__code, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__msg__ControlTrace__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
  {
    {geometry_msgs__msg__Twist__TYPE_NAME, 23, 23},
    {NULL, 0, 0},
  },
  {
    {geometry_msgs__msg__Vector3__TYPE_NAME, 25, 25},
    {NULL, 0, 0},
  },
  {
    {std_msgs__msg__Header__TYPE_NAME, 19, 19},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__msg__ControlTrace__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__msg__ControlTrace__TYPE_NAME, 33, 33},
      {chaos_interfaces__msg__ControlTrace__FIELDS, 11, 11},
    },
    {chaos_interfaces__msg__ControlTrace__REFERENCED_TYPE_DESCRIPTIONS, 4, 4},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&geometry_msgs__msg__Twist__EXPECTED_HASH, geometry_msgs__msg__Twist__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[1].fields = geometry_msgs__msg__Twist__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&geometry_msgs__msg__Vector3__EXPECTED_HASH, geometry_msgs__msg__Vector3__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[2].fields = geometry_msgs__msg__Vector3__get_type_description(NULL)->type_description.fields;
    assert(0 == memcmp(&std_msgs__msg__Header__EXPECTED_HASH, std_msgs__msg__Header__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[3].fields = std_msgs__msg__Header__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "# Runtime evidence for offline training; never a hardware execution confirmation.\n"
  "# Event publication time and the configured robot base frame.\n"
  "std_msgs/Header header\n"
  "string asset_id\n"
  "# Strictly increasing within one runtime process, including failed publications.\n"
  "# A discontinuity or reset invalidates any previously held training action.\n"
  "uint64 sequence\n"
  "uint64 monotonic_ns\n"
  "string request_id\n"
  "# motion.velocity, motion.stop, navigation.pose, flight.takeoff, or flight.land.\n"
  "string command\n"
  "# issued, rejected, or invalidated.\n"
  "string stage\n"
  "# Only issued motion.velocity events may carry a usable velocity label.\n"
  "bool velocity_valid\n"
  "geometry_msgs/Twist velocity\n"
  "# Remaining seconds from header.stamp, bounded by command and lease expiration.\n"
  "# Zero supplies no valid action interval. Invalidation always ends the interval.\n"
  "float64 valid_for_s\n"
  "# Machine-readable code only. No lease token, owner, or exception message.\n"
  "string code";

static char msg_encoding[] = "msg";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__msg__ControlTrace__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__msg__ControlTrace__TYPE_NAME, 33, 33},
    {msg_encoding, 3, 3},
    {toplevel_type_raw_source, 928, 928},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__msg__ControlTrace__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[5];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 5, 5};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__msg__ControlTrace__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    sources[2] = *geometry_msgs__msg__Twist__get_individual_type_description_source(NULL);
    sources[3] = *geometry_msgs__msg__Vector3__get_individual_type_description_source(NULL);
    sources[4] = *std_msgs__msg__Header__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
