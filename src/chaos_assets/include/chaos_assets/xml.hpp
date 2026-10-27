#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chaos::assets {

// True when the text contains a DOCTYPE or ENTITY declaration.
//
// The scan also rejects the spaced form `<! DOCTYPE`, which the xacro bridge
// already rejects. A contiguous `<!doctype` remains rejected, so this does not
// accept a document the previous native check refused.
bool declares_doctype_or_entity(const std::string& xml);

// One attribute of an XML element.
struct XmlAttribute {
  std::string name;            // local name, without namespace prefix
  std::string namespace_uri;   // empty for an unprefixed attribute
  std::string value;
};

// A read-only XML element.
//
// The tree mirrors the subset of XML that URDF/Xacro documents use: element
// names (local name plus namespace URI), attributes in document order,
// character data and child elements. Comments, processing instructions and
// the XML declaration are not part of the tree; they survive only inside the
// serialized document.
struct XmlElement {
  std::string name;            // local name, without namespace prefix
  std::string namespace_uri;   // empty when the element has no namespace
  std::vector<XmlAttribute> attributes;  // document order
  std::string text;            // character data directly inside this element
  std::vector<std::unique_ptr<XmlElement>> children;
};

// An XML document parsed with libxml2 in a hardened configuration.
//
// The parser never resolves external entities, never fetches a network
// resource and rejects DOCTYPE/ENTITY declarations outright, so a hostile
// description file cannot expand into an unbounded document. Documents are
// bounded in size and nesting depth.
class XmlDocument {
 public:
  XmlDocument();
  ~XmlDocument();
  XmlDocument(XmlDocument&&) noexcept;
  XmlDocument& operator=(XmlDocument&&) noexcept;
  XmlDocument(const XmlDocument&) = delete;
  XmlDocument& operator=(const XmlDocument&) = delete;

  // Parse an XML document. Throws ConfigurationError for malformed input, for
  // DOCTYPE/ENTITY declarations and for documents that exceed the depth limit.
  static std::unique_ptr<XmlDocument> parse(const std::string& xml, const std::string& where);

  // The document element.
  const XmlElement& root() const { return *root_; }

  // Every element of the document in document order, the root included.
  std::vector<const XmlElement*> elements() const;

  // Replace an attribute value of an element obtained from this document.
  // Unknown attributes are created; existing ones keep their position.
  void set_attribute(const XmlElement& element, const std::string& name, const std::string& value);

  // Serialize the document, including any modification made through
  // set_attribute(). The output is deterministic for a given input.
  std::string serialize() const;

  // Maximum accepted nesting depth of a parsed document.
  static constexpr int kMaxDepth = 256;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
  std::unique_ptr<XmlElement> root_;
};

}  // namespace chaos::assets
