#include "chaos_assets/xml.hpp"

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <cctype>
#include <map>
#include <string>

#include "chaos_assets/errors.hpp"

namespace chaos::assets {

namespace {

// The largest document the parser accepts.
constexpr size_t kMaxBytes = 4u * 1024u * 1024u;

// DOCTYPE and ENTITY declarations are rejected before libxml2 sees the
// document: an internal subset can define recursive entities, and an
// external subset can pull a network resource.
bool declares_doctype_or_entity(const std::string& xml) {
  std::string lowered;
  lowered.reserve(xml.size());
  for (char c : xml) {
    lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lowered.find("<!doctype") != std::string::npos ||
         lowered.find("<!entity") != std::string::npos;
}

using NodeMap = std::map<const XmlElement*, xmlNodePtr>;

std::unique_ptr<XmlElement> build(xmlNodePtr node, int depth, NodeMap& nodes) {
  if (depth > XmlDocument::kMaxDepth) {
    throw ConfigurationError("invalid URDF XML: document nesting is too deep");
  }
  auto element = std::make_unique<XmlElement>();
  element->name = reinterpret_cast<const char*>(node->name);
  if (node->ns != nullptr && node->ns->href != nullptr) {
    element->namespace_uri = reinterpret_cast<const char*>(node->ns->href);
  }
  for (xmlAttrPtr attribute = node->properties; attribute != nullptr;
       attribute = attribute->next) {
    XmlAttribute entry;
    entry.name = reinterpret_cast<const char*>(attribute->name);
    if (attribute->ns != nullptr && attribute->ns->href != nullptr) {
      entry.namespace_uri = reinterpret_cast<const char*>(attribute->ns->href);
    }
    // The value belongs to this attribute node. Looking it up by local name
    // would return the value of a different attribute that shares the name
    // in another namespace.
    xmlChar* value = xmlNodeListGetString(node->doc, attribute->children, 1);
    entry.value = value != nullptr ? reinterpret_cast<const char*>(value) : "";
    if (value != nullptr) xmlFree(value);
    element->attributes.push_back(std::move(entry));
  }
  std::string text;
  for (xmlNodePtr child = node->children; child != nullptr; child = child->next) {
    if (child->type == XML_ELEMENT_NODE) {
      element->children.push_back(build(child, depth + 1, nodes));
    } else if (child->type == XML_TEXT_NODE || child->type == XML_CDATA_SECTION_NODE) {
      if (child->content != nullptr) text += reinterpret_cast<const char*>(child->content);
    }
  }
  element->text = text;
  nodes.emplace(element.get(), node);
  return element;
}

void collect(const XmlElement& element, std::vector<const XmlElement*>& out) {
  out.push_back(&element);
  for (const auto& child : element.children) collect(*child, out);
}

}  // namespace

class XmlDocument::Impl {
 public:
  // The document belongs to the implementation object, so replacing or
  // destroying the implementation always releases it.
  ~Impl() {
    if (document != nullptr) {
      xmlFreeDoc(document);
      document = nullptr;
    }
  }
  Impl() = default;
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  xmlDocPtr document = nullptr;
  NodeMap nodes;
};

XmlDocument::XmlDocument() : impl_(std::make_unique<Impl>()) {}

XmlDocument::~XmlDocument() = default;

XmlDocument::XmlDocument(XmlDocument&&) noexcept = default;
XmlDocument& XmlDocument::operator=(XmlDocument&&) noexcept = default;

std::unique_ptr<XmlDocument> XmlDocument::parse(const std::string& xml, const std::string& where) {
  if (xml.size() > kMaxBytes) {
    throw ConfigurationError(where + ": XML document exceeds " + std::to_string(kMaxBytes) +
                             " bytes");
  }
  if (declares_doctype_or_entity(xml)) {
    throw ConfigurationError(where + ": DOCTYPE and ENTITY declarations are not allowed");
  }
  auto document = std::make_unique<XmlDocument>();
  // XML_PARSE_NONET forbids network access for any external reference.
  // XML_PARSE_NOERROR/XML_PARSE_NOWARNING keep libxml2 from writing to
  // stderr; the caller receives a structured error instead.
  const int options = XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING;
  xmlDocPtr parsed = xmlReadMemory(xml.data(), static_cast<int>(xml.size()), "description.xml",
                                   nullptr, options);
  if (parsed == nullptr) {
    throw ConfigurationError(where + ": invalid URDF XML");
  }
  if (parsed->intSubset != nullptr) {
    xmlFreeDoc(parsed);
    throw ConfigurationError(where + ": DOCTYPE and ENTITY declarations are not allowed");
  }
  xmlNodePtr root = xmlDocGetRootElement(parsed);
  if (root == nullptr) {
    xmlFreeDoc(parsed);
    throw ConfigurationError(where + ": invalid URDF XML: the document has no root element");
  }
  document->impl_->document = parsed;
  document->root_ = build(root, 1, document->impl_->nodes);
  return document;
}

std::vector<const XmlElement*> XmlDocument::elements() const {
  std::vector<const XmlElement*> result;
  collect(*root_, result);
  return result;
}

void XmlDocument::set_attribute(const XmlElement& element, const std::string& name,
                                const std::string& value) {
  const auto found = impl_->nodes.find(&element);
  if (found == impl_->nodes.end()) {
    throw ConfigurationError("invalid URDF XML: the element does not belong to this document");
  }
  xmlSetProp(found->second, reinterpret_cast<const xmlChar*>(name.c_str()),
             reinterpret_cast<const xmlChar*>(value.c_str()));
}

std::string XmlDocument::serialize() const {
  xmlChar* buffer = nullptr;
  int size = 0;
  // format = 0 keeps the original character data; the serializer never
  // reflows or re-indents the document.
  xmlDocDumpFormatMemory(impl_->document, &buffer, &size, 0);
  if (buffer == nullptr) {
    throw ConfigurationError("invalid URDF XML: the document could not be serialized");
  }
  std::string result(reinterpret_cast<const char*>(buffer), static_cast<size_t>(size));
  xmlFree(buffer);
  return result;
}

}  // namespace chaos::assets
