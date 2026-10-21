// Native ROS message reading layer: introspection driven access to a
// decoded message object.
#include "chaos_data/ros_message.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>

#include <rcpputils/shared_library.hpp>
#include <rmw/rmw.h>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rclcpp/typesupport_helpers.hpp>
#include <rosidl_runtime_cpp/message_initialization.hpp>
#include <rosidl_typesupport_introspection_cpp/field_types.hpp>
#include <rosidl_typesupport_introspection_cpp/message_introspection.hpp>

namespace chaos::data {

namespace introspection = rosidl_typesupport_introspection_cpp;

using introspection::MessageMember;
using introspection::MessageMembers;

namespace {

// A single message payload is bounded: a hostile bag must not make the
// process allocate without limit.
constexpr std::size_t kMaxPayloadBytes = 64u * 1024u * 1024u;

// The metadata and the type support libraries of one message type. The
// libraries must stay loaded while the metadata is used, so they are owned
// here and shared with every decoded message.
struct TypeEntry {
  std::shared_ptr<rcpputils::SharedLibrary> introspection_library;
  std::shared_ptr<rcpputils::SharedLibrary> rmw_library;
  const MessageMembers * members = nullptr;
  const rosidl_message_type_support_t * rmw_type_support = nullptr;
};

class TypeRegistry {
public:
  static TypeRegistry & instance()
  {
    static TypeRegistry registry;
    return registry;
  }

  std::shared_ptr<const TypeEntry> entry(const std::string & type_name) const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = cache_.find(type_name);
    if (found != cache_.end()) {
      return found->second;
    }
    if (cache_.size() >= kMaxCachedTypes) {
      // The cache is bounded: an unbounded type cache would keep every
      // typesupport library of a hostile bag in memory.
      return nullptr;
    }
    auto entry = std::make_shared<TypeEntry>();
    try {
      entry->introspection_library = rclcpp::get_typesupport_library(
        type_name, "rosidl_typesupport_introspection_cpp");
      const auto * handle = rclcpp::get_message_typesupport_handle(
        type_name, "rosidl_typesupport_introspection_cpp", *entry->introspection_library);
      entry->members = static_cast<const MessageMembers *>(handle->data);

      // The rmw typesupport is the dispatcher of the rosidl_typesupport_cpp
      // library. The dispatcher resolves the type by name for the RMW
      // implementation that is active in this process, so no middleware
      // specific name is hard coded and another RMW (for example Cyclone)
      // works without a change.
      entry->rmw_library = rclcpp::get_typesupport_library(
        type_name, "rosidl_typesupport_cpp");
      entry->rmw_type_support = rclcpp::get_message_typesupport_handle(
        type_name, "rosidl_typesupport_cpp", *entry->rmw_library);
    } catch (const std::exception &) {
      entry->members = nullptr;
      entry->rmw_type_support = nullptr;
    }
    cache_.emplace(type_name, entry);
    return entry;
  }

private:
  static constexpr std::size_t kMaxCachedTypes = 256;
  mutable std::mutex mutex_;
  mutable std::map<std::string, std::shared_ptr<const TypeEntry>> cache_;
};

const MessageMembers * nestedMembers(const MessageMember & member)
{
  if (member.members_ == nullptr) {
    return nullptr;
  }
  return static_cast<const MessageMembers *>(member.members_->data);
}

const MessageMember * findMember(const MessageMembers * members,
                                 const std::string & name)
{
  if (members == nullptr) {
    return nullptr;
  }
  for (uint32_t index = 0; index < members->member_count_; ++index) {
    const MessageMember & member = members->members_[index];
    if (name == member.name_) {
      return &member;
    }
  }
  return nullptr;
}

bool isTimestamp(const MessageMembers * members)
{
  if (members == nullptr) {
    return false;
  }
  bool has_sec = false;
  bool has_nanosec = false;
  for (uint32_t index = 0; index < members->member_count_; ++index) {
    const std::string name = members->members_[index].name_;
    has_sec = has_sec || name == "sec";
    has_nanosec = has_nanosec || name == "nanosec";
  }
  return has_sec && has_nanosec;
}

// The address of a member inside its parent object.
//
// A unary member is stored inline, so the address is the byte offset of the
// member inside the parent. An array or a sequence is a container (a
// std::vector), so the address of the member is the address of the
// container: the element accessors of the introspection take the address of
// the field, not the address of the parent object.
const void * memberField(const MessageMember & member, const void * object)
{
  if (object == nullptr) {
    return nullptr;
  }
  return static_cast<const std::uint8_t *>(object) + member.offset_;
}

// The number of elements of an array or a sequence.
//
// The introspection provides the current size of a sequence through
// size_function, so the bound is the real number of elements and not the
// declared maximum.
std::size_t memberSize(const MessageMember & member, const void * field)
{
  if (member.size_function != nullptr) {
    return member.size_function(field);
  }
  if (member.is_upper_bound_) {
    // An unbounded sequence without a size function cannot be measured, so
    // no element is addressable.
    return 0;
  }
  return member.array_size_;
}

// The address of one element of an array or a sequence.
const void * memberElement(const MessageMember & member, const void * field,
                           std::size_t index)
{
  if (member.get_const_function == nullptr) {
    return nullptr;
  }
  return member.get_const_function(field, index);
}

// A path component that is a decimal index. A component that is not a
// number never selects an element, so a name like "2d" is not an index.
bool parseIndex(const std::string & token, std::size_t & out)
{
  if (token.empty() || token.size() > 19) {
    return false;
  }
  std::size_t value = 0;
  for (const char character : token) {
    if (character < '0' || character > '9') {
      return false;
    }
    value = value * 10 + static_cast<std::size_t>(character - '0');
  }
  out = value;
  return true;
}

// Reads one scalar value. The address is the address of the value itself,
// which is the element address for an indexed element.
bool readValue(const MessageMember & member, const void * value,
               DynamicValue & out)
{
  if (value == nullptr) {
    return false;
  }
  switch (member.type_id_) {
    case introspection::ROS_TYPE_BOOLEAN: {
      out.kind = DynamicValue::Kind::kBool;
      out.boolean = *static_cast<const bool *>(value);
      return true;
    }
    case introspection::ROS_TYPE_INT8: {
      out.kind = DynamicValue::Kind::kInt64;
      out.integer = *static_cast<const std::int8_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_UINT8:
    case introspection::ROS_TYPE_OCTET: {
      out.kind = DynamicValue::Kind::kUint64;
      out.uinteger = *static_cast<const std::uint8_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_INT16: {
      out.kind = DynamicValue::Kind::kInt64;
      out.integer = *static_cast<const std::int16_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_UINT16: {
      out.kind = DynamicValue::Kind::kUint64;
      out.uinteger = *static_cast<const std::uint16_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_INT32: {
      out.kind = DynamicValue::Kind::kInt64;
      out.integer = *static_cast<const std::int32_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_UINT32: {
      out.kind = DynamicValue::Kind::kUint64;
      out.uinteger = *static_cast<const std::uint32_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_INT64: {
      out.kind = DynamicValue::Kind::kInt64;
      out.integer = *static_cast<const std::int64_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_UINT64: {
      out.kind = DynamicValue::Kind::kUint64;
      out.uinteger = *static_cast<const std::uint64_t *>(value);
      return true;
    }
    case introspection::ROS_TYPE_FLOAT: {
      out.kind = DynamicValue::Kind::kDouble;
      out.number = *static_cast<const float *>(value);
      return true;
    }
    case introspection::ROS_TYPE_DOUBLE: {
      out.kind = DynamicValue::Kind::kDouble;
      out.number = *static_cast<const double *>(value);
      return true;
    }
    case introspection::ROS_TYPE_STRING: {
      out.kind = DynamicValue::Kind::kString;
      out.text = *static_cast<const std::string *>(value);
      return true;
    }
    case introspection::ROS_TYPE_MESSAGE: {
      const MessageMembers * nested = nestedMembers(member);
      if (!isTimestamp(nested)) {
        // A structured value is not exposed; the caller must ask for a
        // scalar field inside it.
        return false;
      }
      const MessageMember * sec_member = findMember(nested, "sec");
      const MessageMember * nanosec_member = findMember(nested, "nanosec");
      if (sec_member == nullptr || nanosec_member == nullptr) {
        return false;
      }
      const void * sec = memberField(*sec_member, value);
      const void * nanosec = memberField(*nanosec_member, value);
      if (sec == nullptr || nanosec == nullptr) {
        return false;
      }
      out.kind = DynamicValue::Kind::kTimestamp;
      out.seconds = *static_cast<const std::int32_t *>(sec);
      out.nanoseconds = *static_cast<const std::uint32_t *>(nanosec);
      return true;
    }
    default:
      // A whole array or sequence is not expanded: the data plane keeps the
      // native payload instead of one value per element.
      return false;
  }
}

// Walks the path through the introspection metadata.
//
// The path is a sequence of member names; a numeric component selects one
// element of an array or a sequence. The bound of the element is the size
// that the introspection reports for the container, so an index outside the
// container is refused instead of reading memory that does not belong to the
// message.
bool readPath(const MessageMembers * members, const void * object,
              const std::vector<std::string> & parts, std::size_t index,
              DynamicValue & out)
{
  if (members == nullptr || object == nullptr || index >= parts.size()) {
    return false;
  }
  const MessageMember * member = findMember(members, parts[index]);
  if (member == nullptr) {
    return false;
  }
  const void * field = memberField(*member, object);
  if (field == nullptr) {
    return false;
  }
  if (!member->is_array_) {
    // A unary member: the value is stored inline.
    if (index + 1 == parts.size()) {
      return readValue(*member, field, out);
    }
    if (member->type_id_ != introspection::ROS_TYPE_MESSAGE) {
      return false;
    }
    const MessageMembers * nested = nestedMembers(*member);
    if (nested == nullptr) {
      return false;
    }
    return readPath(nested, field, parts, index + 1, out);
  }
  // An array or a sequence: the next component has to be the index of one
  // element. The container itself is never returned as a value.
  if (index + 2 > parts.size()) {
    return false;
  }
  std::size_t element_index = 0;
  if (!parseIndex(parts[index + 1], element_index)) {
    return false;
  }
  if (element_index >= memberSize(*member, field)) {
    return false;
  }
  if (member->type_id_ == introspection::ROS_TYPE_BOOLEAN) {
    // A sequence of booleans is a std::vector<bool>, whose elements are
    // proxy references: the address of an element cannot be read. The
    // introspection provides a function that copies one element out of the
    // sequence, and that is the only correct way to read it.
    if (member->fetch_function == nullptr) {
      return false;
    }
    bool fetched = false;
    member->fetch_function(field, element_index, &fetched);
    out.kind = DynamicValue::Kind::kBool;
    out.boolean = fetched;
    return true;
  }
  const void * element = memberElement(*member, field, element_index);
  if (element == nullptr) {
    return false;
  }
  if (index + 2 == parts.size()) {
    return readValue(*member, element, out);
  }
  const MessageMembers * nested = nestedMembers(*member);
  if (nested == nullptr) {
    return false;
  }
  return readPath(nested, element, parts, index + 2, out);
}

std::vector<std::string> splitPath(const std::string & path)
{
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= path.size()) {
    const std::size_t dot = path.find('.', start);
    if (dot == std::string::npos) {
      parts.push_back(path.substr(start));
      break;
    }
    parts.push_back(path.substr(start, dot - start));
    start = dot + 1;
  }
  return parts;
}

}  // namespace

struct DynamicMessage::Impl {
  std::shared_ptr<const TypeEntry> type;
  // The decoded message object. It is created by the introspection
  // initializer and destroyed by the introspection finalizer.
  std::vector<std::uint8_t> storage;
  // The initializer ran, so the finalizer has to run as well, even when the
  // deserialization failed afterwards.
  bool initialized = false;
  bool decoded = false;

  ~Impl()
  {
    if (initialized && type != nullptr && type->members != nullptr &&
        type->members->fini_function != nullptr && !storage.empty()) {
      type->members->fini_function(storage.data());
    }
  }
};

bool DynamicValue::asBoolean() const
{
  if (kind == Kind::kBool) {
    return boolean;
  }
  if (kind == Kind::kInt64) {
    return integer != 0;
  }
  if (kind == Kind::kUint64) {
    return uinteger != 0;
  }
  return false;
}

bool DynamicValue::asNumber(double & out) const
{
  switch (kind) {
    case Kind::kDouble:
      out = number;
      return true;
    case Kind::kInt64:
      out = static_cast<double>(integer);
      return true;
    case Kind::kUint64:
      out = static_cast<double>(uinteger);
      return true;
    case Kind::kBool:
      out = boolean ? 1.0 : 0.0;
      return true;
    default:
      return false;
  }
}

bool DynamicValue::asTimestamp(std::int64_t & out_seconds,
                               std::uint32_t & out_nanoseconds) const
{
  if (kind != Kind::kTimestamp) {
    return false;
  }
  out_seconds = seconds;
  out_nanoseconds = nanoseconds;
  return true;
}

DynamicMessage::DynamicMessage(
  std::string type_name,
  std::shared_ptr<const std::vector<std::uint8_t>> payload)
: type_name_(std::move(type_name)), payload_(std::move(payload)),
  impl_(std::make_unique<Impl>())
{
  impl_->type = TypeRegistry::instance().entry(type_name_);
  if (impl_->type == nullptr || impl_->type->members == nullptr ||
      impl_->type->rmw_type_support == nullptr) {
    decode_error_ = "unknown message type: " + type_name_;
    return;
  }
  if (!payload_ || payload_->empty()) {
    decode_error_ = "empty payload";
    return;
  }
  if (payload_->size() > kMaxPayloadBytes) {
    decode_error_ = "payload exceeds the 64 MiB limit";
    return;
  }
  const MessageMembers * members = impl_->type->members;
  impl_->storage.assign(members->size_of_, 0);
  if (members->init_function != nullptr) {
    members->init_function(impl_->storage.data(),
                            rosidl_runtime_cpp::MessageInitialization::ALL);
  }
  impl_->initialized = true;
  try {
    rclcpp::SerializedMessage serialized(payload_->size());
    auto & raw = serialized.get_rcl_serialized_message();
    raw.buffer_length = payload_->size();
    std::memcpy(raw.buffer, payload_->data(), payload_->size());
    rclcpp::SerializationBase deserializer(impl_->type->rmw_type_support);
    deserializer.deserialize_message(&serialized, impl_->storage.data());
  } catch (const std::exception & error) {
    decode_error_ = error.what();
    return;
  }
  impl_->decoded = true;
}

DynamicMessage::~DynamicMessage() = default;

bool DynamicMessage::valid() const
{
  return impl_ && impl_->decoded;
}

bool DynamicMessage::typeSupported(const std::string & type_name)
{
  try {
    const auto entry = TypeRegistry::instance().entry(type_name);
    return entry != nullptr && entry->members != nullptr &&
           entry->rmw_type_support != nullptr;
  } catch (const std::exception &) {
    return false;
  }
}

bool DynamicMessage::read(const std::string & path, DynamicValue & out) const
{
  if (!valid()) {
    return false;
  }
  const auto parts = splitPath(path);
  if (parts.empty()) {
    return false;
  }
  return readPath(impl_->type->members, impl_->storage.data(), parts, 0, out);
}

bool DynamicMessage::hasHeader() const
{
  if (!valid()) {
    return false;
  }
  return findMember(impl_->type->members, "header") != nullptr;
}

bool DynamicMessage::headerStamp(std::int64_t & seconds,
                                 std::uint32_t & nanoseconds) const
{
  DynamicValue value;
  if (!read("header.stamp", value)) {
    return false;
  }
  return value.asTimestamp(seconds, nanoseconds);
}

bool DynamicMessage::headerFrame(std::string & out) const
{
  DynamicValue value;
  if (!read("header.frame_id", value)) {
    return false;
  }
  if (value.kind != DynamicValue::Kind::kString) {
    return false;
  }
  out = value.text;
  return true;
}

}  // namespace chaos::data
