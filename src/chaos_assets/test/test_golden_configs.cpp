// Golden comparison against the configuration snapshots of the previous
// Python implementation.
//
// The snapshots in ``test/baseline`` were produced by the original
// ``asset_config`` command from the same configuration files. They are stored
// inside the package, so the regression runs in every checkout.
//
// The snapshots contain the absolute paths of the machine that created them,
// and the digest of an asset depends on those paths. A checkout at a
// different location resolves different paths, and an installed package
// resolves its model files through the package index, so the comparison is
// made on the normalized form of both documents:
//
//   1. every path inside the package is reduced to the part behind the
//      package directory, so the archive and a fresh resolution are
//      comparable in every workspace,
//   2. the archived snapshot is verified against its own recorded digest, so
//      the archive is proven to be the output of the Python implementation
//      and the native canonical encoding is proven to reproduce it,
//   3. the expected digest of the normalized snapshot is recomputed
//      independently and compared with the digest of the normalized native
//      resolution, so the digest function itself is covered as well.
//
// The test proves three things:
//
//   1. the canonical JSON encoding of the native engine reproduces the
//      digest of the Python implementation for every snapshot,
//   2. the resolved configuration is field by field identical, and
//   3. an asset with a robot description differs only in the byte format of
//      the normalized XML: the documents are semantically equal, the source
//      digest is identical, and swapping the native XML into the snapshot
//      reproduces the native digest exactly.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <openssl/evp.h>

#include "chaos_assets/inventory.hpp"
#include "chaos_assets/json.hpp"
#include "chaos_assets/xml.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Inventory;
using chaos::assets::Json;

namespace {

#ifndef CHAOS_ASSETS_SOURCE_DIR
#define CHAOS_ASSETS_SOURCE_DIR ""
#endif

// The package directory of this package.
//
// Every path inside the package contains this segment, and it is the only
// reliable part of a path that survives a move of the workspace.
constexpr const char* package_directory = "chaos_assets";

// The digest of a document, computed independently of the engine.
std::string sha256_hex(const std::string& content) {
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int length = 0;
  EVP_DigestInit_ex(context, EVP_sha256(), nullptr);
  EVP_DigestUpdate(context, content.data(), content.size());
  EVP_DigestFinal_ex(context, hash, &length);
  EVP_MD_CTX_free(context);
  static const char hex[] = "0123456789abcdef";
  std::string result;
  for (unsigned int index = 0; index < length; ++index) {
    result += hex[hash[index] >> 4];
    result += hex[hash[index] & 0x0f];
  }
  return result;
}

// The archived snapshots of the package.
//
// The directory is derived from the source directory of the build, so the
// test never depends on an absolute path of a particular machine.
std::string baseline_directory() {
  const char* override_path = std::getenv("CHAOS_ASSETS_BASELINE_DIR");
  if (override_path != nullptr && *override_path != '\0') {
    return override_path;
  }
  return std::string(CHAOS_ASSETS_SOURCE_DIR) + "/test/baseline";
}

// The original configuration directory of the source tree.
std::string config_directory() { return std::string(CHAOS_ASSETS_SOURCE_DIR) + "/config"; }

// The normalized form of a path.
//
// A path inside the package is reduced to the part behind the package
// directory, so the source tree of a checkout and the installed layout of the
// same package produce the same form. Every other path - a ROS namespace, an
// output directory of a recorder - is kept as it is.
std::string normalize_path(const std::string& path) {
  const std::string marker = std::string("/") + package_directory + "/";
  const size_t position = path.rfind(marker);
  if (position == std::string::npos) return path;
  return std::string(package_directory) + "/" + path.substr(position + marker.size());
}

// Normalize every path of a document in place.
void normalize_in_place(Json& value) {
  if (value.is_string()) {
    // The result is a separate string, because the source is a reference into
    // the value that is replaced.
    const std::string normalized = normalize_path(value.get_ref<const std::string&>());
    value = normalized;
    return;
  }
  if (value.is_array()) {
    for (Json& item : value) normalize_in_place(item);
    return;
  }
  if (value.is_object()) {
    for (auto it = value.begin(); it != value.end(); ++it) normalize_in_place(it.value());
  }
}

// The normalized copy of a document.
Json normalize(Json value) {
  normalize_in_place(value);
  return value;
}

Json read_json(const std::string& path) {
  std::ifstream stream(path);
  EXPECT_TRUE(stream.good()) << path;
  Json value;
  stream >> value;
  return value;
}

// Compare two XML documents by their structure instead of their bytes.
void expect_semantically_equal(const std::string& left, const std::string& right) {
  const auto left_document = chaos::assets::XmlDocument::parse(left, "baseline");
  const auto right_document = chaos::assets::XmlDocument::parse(right, "native");
  const std::vector<const chaos::assets::XmlElement*> left_elements = left_document->elements();
  const std::vector<const chaos::assets::XmlElement*> right_elements = right_document->elements();
  ASSERT_EQ(left_elements.size(), right_elements.size());
  for (size_t index = 0; index < left_elements.size(); ++index) {
    const auto& a = *left_elements[index];
    const auto& b = *right_elements[index];
    EXPECT_EQ(a.name, b.name) << "element " << index;
    EXPECT_EQ(a.namespace_uri, b.namespace_uri) << "element " << index;
    EXPECT_EQ(a.text, b.text) << "element " << index << " (" << a.name << ")";
    ASSERT_EQ(a.attributes.size(), b.attributes.size()) << "element " << index << " (" << a.name << ")";
    for (size_t attribute = 0; attribute < a.attributes.size(); ++attribute) {
      EXPECT_EQ(a.attributes[attribute].name, b.attributes[attribute].name);
      EXPECT_EQ(a.attributes[attribute].namespace_uri, b.attributes[attribute].namespace_uri);
      EXPECT_EQ(a.attributes[attribute].value, b.attributes[attribute].value);
    }
  }
}

struct Snapshot {
  std::string name;
  Json document;
};

std::vector<Snapshot> snapshots() {
  const char* const names[] = {"demo.yaml", "sensors.demo.yaml", "data_collection.demo.yaml",
                               "go2.demo.yaml", "description.demo.yaml"};
  std::vector<Snapshot> result;
  for (const char* name : names) {
    result.push_back({name, read_json(baseline_directory() + "/" + name + ".json")});
  }
  return result;
}

// Whether an asset carries a robot description.
bool has_description(const Json& asset) {
  return asset.contains("description") && !asset.at("description").is_null();
}

}  // namespace

TEST(GoldenConfigs, TheCanonicalEncodingReproducesThePythonDigest) {
  // The archive is verified against its own digest. The digest does not
  // depend on the location of the checkout, so this check is valid in every
  // workspace and proves that the native canonical encoding reproduces the
  // digest of the Python implementation.
  for (const Snapshot& snapshot : snapshots()) {
    for (const Json& asset : snapshot.document.at("assets")) {
      Json without_digest = asset;
      without_digest.erase("config_digest");
      EXPECT_EQ(chaos::assets::config_digest(without_digest),
                asset.at("config_digest").get_ref<const std::string&>())
          << snapshot.name << " " << asset.at("id").get_ref<const std::string&>();
    }
  }
}

TEST(GoldenConfigs, TheResolvedConfigurationMatchesTheSnapshot) {
  for (const Snapshot& snapshot : snapshots()) {
    const Inventory inventory =
        chaos::assets::load_inventory(config_directory() + "/" + snapshot.name);
    const Json resolved = normalize(inventory.as_dict());
    const Json expected = normalize(snapshot.document);
    ASSERT_EQ(resolved.at("assets").size(), expected.at("assets").size()) << snapshot.name;
    EXPECT_EQ(resolved.at("source"), expected.at("source")) << snapshot.name;
    EXPECT_EQ(resolved.at("hosts"), expected.at("hosts")) << snapshot.name;
    EXPECT_EQ(resolved.at("transports"), expected.at("transports")) << snapshot.name;
    for (size_t index = 0; index < resolved.at("assets").size(); ++index) {
      Json mine = resolved.at("assets").at(index);
      Json theirs = expected.at("assets").at(index);
      const std::string id = mine.at("id").get_ref<const std::string&>();
      ASSERT_EQ(mine.at("id"), theirs.at("id")) << snapshot.name;
      // The digest is compared separately, because it is computed over the
      // real paths of the respective workspace.
      mine.erase("config_digest");
      theirs.erase("config_digest");
      if (!has_description(mine)) {
        // Without a robot description the snapshot must be identical.
        EXPECT_EQ(mine, theirs) << snapshot.name << " " << id;
        continue;
      }
      // With a robot description the serialized XML may use another byte
      // format; every other field, and the digest of the source document,
      // must still be identical.
      const Json my_description = mine.at("description");
      const Json their_description = theirs.at("description");
      EXPECT_EQ(my_description.at("source_sha256"), their_description.at("source_sha256"))
          << snapshot.name << " " << id;
      EXPECT_EQ(my_description.at("links"), their_description.at("links")) << snapshot.name << " " << id;
      EXPECT_EQ(my_description.at("joints"), their_description.at("joints")) << snapshot.name << " " << id;
      EXPECT_EQ(my_description.at("root"), their_description.at("root")) << snapshot.name << " " << id;
      Json mine_without_xml = mine;
      Json theirs_without_xml = theirs;
      mine_without_xml.at("description").erase("xml");
      mine_without_xml.at("description").erase("sha256");
      theirs_without_xml.at("description").erase("xml");
      theirs_without_xml.at("description").erase("sha256");
      EXPECT_EQ(mine_without_xml, theirs_without_xml) << snapshot.name << " " << id;
    }
  }
}

TEST(GoldenConfigs, TheExpectedDigestIsRecomputedFromTheSnapshot) {
  // The expected digest is computed from the normalized snapshot itself, so
  // the native resolution is never compared against a value that was copied
  // from its own output.
  for (const Snapshot& snapshot : snapshots()) {
    const Inventory inventory =
        chaos::assets::load_inventory(config_directory() + "/" + snapshot.name);
    const Json resolved = normalize(inventory.as_dict());
    const Json expected = normalize(snapshot.document);
    for (size_t index = 0; index < resolved.at("assets").size(); ++index) {
      Json mine = resolved.at("assets").at(index);
      Json theirs = expected.at("assets").at(index);
      const std::string id = mine.at("id").get_ref<const std::string&>();
      if (has_description(mine)) continue;  // The XML byte format differs; see below.
      Json theirs_without_digest = theirs;
      theirs_without_digest.erase("config_digest");
      Json mine_without_digest = mine;
      mine_without_digest.erase("config_digest");
      EXPECT_EQ(chaos::assets::config_digest(theirs_without_digest),
                chaos::assets::config_digest(mine_without_digest))
          << snapshot.name << " " << id;
    }
  }
}

TEST(GoldenConfigs, TheDescriptionDiffersOnlyInTheXmlByteFormat) {
  for (const Snapshot& snapshot : snapshots()) {
    const Inventory inventory =
        chaos::assets::load_inventory(config_directory() + "/" + snapshot.name);
    const Json resolved = normalize(inventory.as_dict());
    const Json expected = normalize(snapshot.document);
    for (size_t index = 0; index < resolved.at("assets").size(); ++index) {
      Json mine = resolved.at("assets").at(index);
      Json theirs = expected.at("assets").at(index);
      const std::string id = mine.at("id").get_ref<const std::string&>();
      if (!has_description(mine)) continue;
      const std::string my_xml = mine.at("description").at("xml").get_ref<const std::string&>();
      const std::string their_xml =
          theirs.at("description").at("xml").get_ref<const std::string&>();
      // The documents describe the same robot.
      expect_semantically_equal(their_xml, my_xml);
      // Each recorded digest is the digest of its own document.
      EXPECT_EQ(sha256_hex(their_xml), theirs.at("description").at("sha256").get_ref<const std::string&>())
          << snapshot.name << " " << id;
      EXPECT_EQ(sha256_hex(my_xml), mine.at("description").at("sha256").get_ref<const std::string&>())
          << snapshot.name << " " << id;
      // Swapping the native XML and its digest into the snapshot must
      // reproduce the native asset exactly, and therefore its digest.
      Json theirs_with_my_xml = theirs;
      theirs_with_my_xml.at("description")["xml"] = my_xml;
      theirs_with_my_xml.at("description")["sha256"] = mine.at("description").at("sha256");
      theirs_with_my_xml.erase("config_digest");
      Json mine_without_digest = mine;
      mine_without_digest.erase("config_digest");
      EXPECT_EQ(theirs_with_my_xml, mine_without_digest) << snapshot.name << " " << id;
      EXPECT_EQ(chaos::assets::config_digest(theirs_with_my_xml),
                chaos::assets::config_digest(mine_without_digest))
          << snapshot.name << " " << id;
    }
  }
}

TEST(GoldenConfigs, TheDigestIsStableAcrossRuns) {
  for (const Snapshot& snapshot : snapshots()) {
    const Inventory first =
        chaos::assets::load_inventory(config_directory() + "/" + snapshot.name);
    const Inventory second =
        chaos::assets::load_inventory(config_directory() + "/" + snapshot.name);
    ASSERT_EQ(first.assets().size(), second.assets().size());
    for (size_t index = 0; index < first.assets().size(); ++index) {
      EXPECT_EQ(first.assets()[index].at("config_digest"), second.assets()[index].at("config_digest"))
          << snapshot.name;
    }
  }
}
