// generated from rosidl_generator_c/resource/idl__description.c.em
// with input from chaos_interfaces:msg/CommandResult.idl
// generated code does not contain a copyright notice

#include "chaos_interfaces/msg/detail/command_result__functions.h"

ROSIDL_GENERATOR_C_PUBLIC_chaos_interfaces
const rosidl_type_hash_t *
chaos_interfaces__msg__CommandResult__get_type_hash(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_type_hash_t hash = {1, {
      0x7c, 0x9f, 0xba, 0xc4, 0xcd, 0x7f, 0x7d, 0xfa,
      0x5e, 0xf1, 0xa2, 0x6b, 0xea, 0x5d, 0xbf, 0x6c,
      0x69, 0xe1, 0xea, 0x4c, 0x50, 0x96, 0x8e, 0xa6,
      0xa2, 0x50, 0x9c, 0x5f, 0x10, 0x66, 0xd5, 0xc8,
    }};
  return &hash;
}

#include <assert.h>
#include <string.h>

// Include directives for referenced types
#include "builtin_interfaces/msg/detail/time__functions.h"

// Hashes for external referenced types
#ifndef NDEBUG
static const rosidl_type_hash_t builtin_interfaces__msg__Time__EXPECTED_HASH = {1, {
    0xb1, 0x06, 0x23, 0x5e, 0x25, 0xa4, 0xc5, 0xed,
    0x35, 0x09, 0x8a, 0xa0, 0xa6, 0x1a, 0x3e, 0xe9,
    0xc9, 0xb1, 0x8d, 0x19, 0x7f, 0x39, 0x8b, 0x0e,
    0x42, 0x06, 0xce, 0xa9, 0xac, 0xf9, 0xc1, 0x97,
  }};
#endif

static char chaos_interfaces__msg__CommandResult__TYPE_NAME[] = "chaos_interfaces/msg/CommandResult";
static char builtin_interfaces__msg__Time__TYPE_NAME[] = "builtin_interfaces/msg/Time";

// Define type names, field names, and default values
static char chaos_interfaces__msg__CommandResult__FIELD_NAME__stamp[] = "stamp";
static char chaos_interfaces__msg__CommandResult__FIELD_NAME__request_id[] = "request_id";
static char chaos_interfaces__msg__CommandResult__FIELD_NAME__success[] = "success";
static char chaos_interfaces__msg__CommandResult__FIELD_NAME__code[] = "code";
static char chaos_interfaces__msg__CommandResult__FIELD_NAME__message[] = "message";

static rosidl_runtime_c__type_description__Field chaos_interfaces__msg__CommandResult__FIELDS[] = {
  {
    {chaos_interfaces__msg__CommandResult__FIELD_NAME__stamp, 5, 5},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_NESTED_TYPE,
      0,
      0,
      {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__CommandResult__FIELD_NAME__request_id, 10, 10},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__CommandResult__FIELD_NAME__success, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_BOOLEAN,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__CommandResult__FIELD_NAME__code, 4, 4},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
  {
    {chaos_interfaces__msg__CommandResult__FIELD_NAME__message, 7, 7},
    {
      rosidl_runtime_c__type_description__FieldType__FIELD_TYPE_STRING,
      0,
      0,
      {NULL, 0, 0},
    },
    {NULL, 0, 0},
  },
};

static rosidl_runtime_c__type_description__IndividualTypeDescription chaos_interfaces__msg__CommandResult__REFERENCED_TYPE_DESCRIPTIONS[] = {
  {
    {builtin_interfaces__msg__Time__TYPE_NAME, 27, 27},
    {NULL, 0, 0},
  },
};

const rosidl_runtime_c__type_description__TypeDescription *
chaos_interfaces__msg__CommandResult__get_type_description(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static bool constructed = false;
  static const rosidl_runtime_c__type_description__TypeDescription description = {
    {
      {chaos_interfaces__msg__CommandResult__TYPE_NAME, 34, 34},
      {chaos_interfaces__msg__CommandResult__FIELDS, 5, 5},
    },
    {chaos_interfaces__msg__CommandResult__REFERENCED_TYPE_DESCRIPTIONS, 1, 1},
  };
  if (!constructed) {
    assert(0 == memcmp(&builtin_interfaces__msg__Time__EXPECTED_HASH, builtin_interfaces__msg__Time__get_type_hash(NULL), sizeof(rosidl_type_hash_t)));
    description.referenced_type_descriptions.data[0].fields = builtin_interfaces__msg__Time__get_type_description(NULL)->type_description.fields;
    constructed = true;
  }
  return &description;
}

static char toplevel_type_raw_source[] =
  "builtin_interfaces/Time stamp\n"
  "string request_id\n"
  "bool success\n"
  "string code\n"
  "string message";

static char msg_encoding[] = "msg";

// Define all individual source functions

const rosidl_runtime_c__type_description__TypeSource *
chaos_interfaces__msg__CommandResult__get_individual_type_description_source(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static const rosidl_runtime_c__type_description__TypeSource source = {
    {chaos_interfaces__msg__CommandResult__TYPE_NAME, 34, 34},
    {msg_encoding, 3, 3},
    {toplevel_type_raw_source, 88, 88},
  };
  return &source;
}

const rosidl_runtime_c__type_description__TypeSource__Sequence *
chaos_interfaces__msg__CommandResult__get_type_description_sources(
  const rosidl_message_type_support_t * type_support)
{
  (void)type_support;
  static rosidl_runtime_c__type_description__TypeSource sources[2];
  static const rosidl_runtime_c__type_description__TypeSource__Sequence source_sequence = {sources, 2, 2};
  static bool constructed = false;
  if (!constructed) {
    sources[0] = *chaos_interfaces__msg__CommandResult__get_individual_type_description_source(NULL),
    sources[1] = *builtin_interfaces__msg__Time__get_individual_type_description_source(NULL);
    constructed = true;
  }
  return &source_sequence;
}
