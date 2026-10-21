// Native ROS message reading layer.
//
// The data plane must never expand a message into a JSON number array: an
// image or a point cloud would allocate one JSON value per byte. This layer
// therefore uses the ROS deserialization that the middleware already
// provides:
//
//  * known types are deserialized with rclcpp::Serialization<T>, so the
//    payload keeps its native byte vector and no copy of the pixel data is
//    made;
//  * other types are deserialized with the rmw typesupport of the type into
//    a local message object that the introspection metadata created. Only the
//    fields that are explicitly requested are read, through the
//    introspection accessors.
//
// The type support libraries stay loaded for as long as the decoded object
// is used; the ownership is expressed with shared pointers.
//
// A source timestamp is never invented: an unknown type, a corrupt payload
// or a missing field is reported as such.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>

namespace chaos::data {

// A single decoded value.
//
// A field that the message declares as "bool" is a C++ bool in the generated
// message struct and is reported as ROS_TYPE_BOOLEAN. A field that the
// message declares as "uint8" is an unsigned integer, even when it is used
// as a flag, so the two are not interchangeable.
class DynamicValue {
public:
  enum class Kind {
    kNone,
    kBool,
    kInt64,
    kUint64,
    kDouble,
    kString,
    kTimestamp,
  };

  Kind kind = Kind::kNone;
  bool boolean = false;
  std::int64_t integer = 0;
  std::uint64_t uinteger = 0;
  double number = 0.0;
  std::string text;
  std::int64_t seconds = 0;
  std::uint32_t nanoseconds = 0;

  bool isNone() const { return kind == Kind::kNone; }
  bool asBoolean() const;
  bool asNumber(double & out) const;
  bool asTimestamp(std::int64_t & seconds, std::uint32_t & nanoseconds) const;
};

// A typed view over one serialized message.
//
// The payload must stay alive as long as the view is used; the view keeps a
// reference to it.
class DynamicMessage {
public:
  DynamicMessage(std::string type_name,
                 std::shared_ptr<const std::vector<std::uint8_t>> payload);
  ~DynamicMessage();

  DynamicMessage(const DynamicMessage &) = delete;
  DynamicMessage & operator=(const DynamicMessage &) = delete;

  // True when the type is known to this environment and the payload decoded.
  bool valid() const;
  const std::string & typeName() const { return type_name_; }
  // Why the message could not be decoded (empty when it could).
  const std::string & decodeError() const { return decode_error_; }

  // True when this environment knows the type and can decode it. The check
  // does not decode a payload; it only resolves the type support.
  static bool typeSupported(const std::string & type_name);

  // Reads a field by a dotted path.
  //
  // A path is a sequence of member names; a numeric component selects one
  // element of an array or a sequence ("fields.0.name"). The index is
  // checked against the size that the introspection reports for the
  // container, so an index outside the container is refused instead of
  // reading memory that does not belong to the message.
  //
  // A container itself is never expanded into a value: an image payload
  // stays the native bytes of the message and no JSON number array is
  // produced.
  //
  // Returns false when the path does not exist, when the index is outside
  // the container or when the value cannot be represented.
  bool read(const std::string & path, DynamicValue & out) const;

  // True when the message declares a "header" member.
  bool hasHeader() const;
  // Reads header.stamp; false when absent, unreadable or invalid.
  bool headerStamp(std::int64_t & seconds, std::uint32_t & nanoseconds) const;
  // Reads header.frame_id; false when absent or unreadable.
  bool headerFrame(std::string & out) const;

private:
  std::string type_name_;
  std::string decode_error_;
  std::shared_ptr<const std::vector<std::uint8_t>> payload_;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace chaos::data

// Typed deserialization with the native ROS serializer. The message keeps
// its own byte vectors, so an image payload is never expanded into numbers.
//
// The definition is in the header: a consumer that uses a message type
// instantiates it for that type, so no explicit instantiation is needed.
namespace chaos::data {

template<typename T>
bool deserializeTyped(const std::uint8_t * data, std::size_t size, T & out,
                      std::string & error)
{
  if (data == nullptr || size == 0) {
    error = "empty payload";
    return false;
  }
  if (size > 64u * 1024u * 1024u) {
    error = "payload exceeds the 64 MiB limit";
    return false;
  }
  try {
    rclcpp::SerializedMessage serialized(size);
    auto & raw = serialized.get_rcl_serialized_message();
    raw.buffer_length = size;
    std::memcpy(raw.buffer, data, size);
    rclcpp::Serialization<T> serialization;
    serialization.deserialize_message(&serialized, &out);
  } catch (const std::exception & caught) {
    error = caught.what();
    return false;
  }
  return true;
}

}  // namespace chaos::data
