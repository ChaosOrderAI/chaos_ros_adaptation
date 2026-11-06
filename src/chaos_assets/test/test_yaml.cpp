// Contract tests for the YAML configuration layer.
//
// The loader keeps the external contract of the original pipeline: YAML 1.1
// implicit typing, quoted scalars stay strings, duplicate mapping keys are
// rejected and integers must fit into a signed 64 bit value.

#include <gtest/gtest.h>

#include <string>

#include "chaos_assets/yaml.hpp"

using chaos::assets::ConfigurationError;
using chaos::assets::Json;

namespace {

Json parse(const std::string& document) {
  return chaos::assets::load_yaml_string(document, "review");
}

}  // namespace

TEST(YamlScalars, BinaryAndHexadecimalIntegers) {
  EXPECT_EQ(parse("a: 0b10")["a"], 2);
  EXPECT_EQ(parse("a: -0b10")["a"], -2);
  EXPECT_EQ(parse("a: 0x10")["a"], 16);
  EXPECT_EQ(parse("a: -0x10")["a"], -16);
  // YAML 1.1 has no "0o" prefix: the value stays a string.
  EXPECT_EQ(parse("a: 0o17")["a"], "0o17");
  EXPECT_EQ(parse("a: 010")["a"], 8);
}

TEST(YamlScalars, SexagesimalNumbers) {
  EXPECT_DOUBLE_EQ(parse("a: 1:02")["a"].get<double>(), 62.0);
  EXPECT_DOUBLE_EQ(parse("a: 1:02.5")["a"].get<double>(), 62.5);
  EXPECT_DOUBLE_EQ(parse("a: 1:02:03.5")["a"].get<double>(), 3723.5);
  EXPECT_DOUBLE_EQ(parse("a: -1:02")["a"].get<double>(), -62.0);
}

TEST(YamlScalars, SexagesimalOverflowIsRejected) {
  EXPECT_THROW(parse("a: 99999999999999999999:00"), ConfigurationError);
}

TEST(YamlScalars, BooleansAreNotNumbers) {
  EXPECT_TRUE(parse("a: yes")["a"].is_boolean());
  EXPECT_TRUE(parse("a: True")["a"].is_boolean());
  EXPECT_FALSE(parse("a: off")["a"].get<bool>());
  EXPECT_FALSE(parse("a: 'yes'")["a"].is_boolean());
  EXPECT_EQ(parse("a: 'yes'")["a"], "yes");
}

TEST(YamlScalars, QuotedScalarsStayStrings) {
  EXPECT_EQ(parse("a: '0x10'")["a"], "0x10");
  EXPECT_EQ(parse("a: \"1.5\"")["a"], "1.5");
  EXPECT_EQ(parse("a: null")["a"], Json());
}

TEST(YamlScalars, ExplicitTagsAreEnforced) {
  EXPECT_THROW(parse("a: !!bool 123"), ConfigurationError);
  EXPECT_THROW(parse("a: !!int 1.5"), ConfigurationError);
  EXPECT_EQ(parse("a: !!str 12")["a"], "12");
  EXPECT_TRUE(parse("a: !!bool true")["a"].is_boolean());
  EXPECT_EQ(parse("a: !!int '12'")["a"], 12);
  EXPECT_TRUE(parse("a: !!float 1")["a"].is_number_float());
  EXPECT_DOUBLE_EQ(parse("a: !!float 1")["a"].get<double>(), 1.0);
}

TEST(YamlScalars, ImplicitFloatRequiresASignedExponent) {
  // The implicit resolver keeps these as strings, exactly like PyYAML.
  EXPECT_EQ(parse("a: 1e5")["a"], "1e5");
  EXPECT_EQ(parse("a: 1.0e5")["a"], "1.0e5");
  EXPECT_EQ(parse("a: .5e2")["a"], ".5e2");
  EXPECT_EQ(parse("a: 1e+5")["a"], "1e+5");
  // A decimal point plus a signed exponent resolves to a float.
  EXPECT_DOUBLE_EQ(parse("a: 1.0e+5")["a"].get<double>(), 100000.0);
  EXPECT_DOUBLE_EQ(parse("a: .5e+2")["a"].get<double>(), 50.0);
  EXPECT_DOUBLE_EQ(parse("a: 1.e+5")["a"].get<double>(), 100000.0);
  EXPECT_DOUBLE_EQ(parse("a: 1.0e-5")["a"].get<double>(), 1e-5);
}

TEST(YamlScalars, ExplicitFloatFollowsThePyYamlConstructor) {
  // The explicit tag is wider than the implicit resolver.
  EXPECT_DOUBLE_EQ(parse("a: !!float 1e5")["a"].get<double>(), 100000.0);
  EXPECT_DOUBLE_EQ(parse("a: !!float 1.0e5")["a"].get<double>(), 100000.0);
  EXPECT_TRUE(std::isinf(parse("a: !!float 1e500")["a"].get<double>()));
  EXPECT_DOUBLE_EQ(parse("a: !!float 010")["a"].get<double>(), 10.0);
  EXPECT_DOUBLE_EQ(parse("a: !!float --1")["a"].get<double>(), 1.0);
  EXPECT_DOUBLE_EQ(parse("a: !!float +-1")["a"].get<double>(), -1.0);
  EXPECT_DOUBLE_EQ(parse("a: !!float 1:2:3")["a"].get<double>(), 3723.0);
  EXPECT_DOUBLE_EQ(parse("a: !!float 1:30.5")["a"].get<double>(), 90.5);
  EXPECT_TRUE(std::isinf(parse("a: !!float inf")["a"].get<double>()));
  EXPECT_TRUE(std::isnan(parse("a: !!float nan")["a"].get<double>()));
  // Base prefixes and hexadecimal floats are rejected, and a trailing
  // suffix is an error rather than a prefix that happens to parse.
  EXPECT_THROW(parse("a: !!float 0b10"), ConfigurationError);
  EXPECT_THROW(parse("a: !!float 0x10"), ConfigurationError);
  EXPECT_THROW(parse("a: !!float 0x1.8p1"), ConfigurationError);
  EXPECT_THROW(parse("a: !!float 1e5abc"), ConfigurationError);
  EXPECT_THROW(parse("a: !!float 1e"), ConfigurationError);
}

TEST(YamlScalars, ASecondDocumentIsRejected) {
  EXPECT_THROW(parse("a: 1\n---\nb: 2\n"), ConfigurationError);
  EXPECT_THROW(parse("a: 1\n---\n"), ConfigurationError);
  // A document end marker does not start a second document.
  EXPECT_EQ(parse("a: 1\n...\n")["a"], 1);
  EXPECT_EQ(parse("---\na: 1\n")["a"], 1);
}

TEST(YamlScalars, DuplicateKeysAreRejected) {
  EXPECT_THROW(parse("a: 1\na: 2\n"), ConfigurationError);
}

TEST(YamlScalars, NonStringMappingKeysAreRejected) {
  EXPECT_THROW(parse("1: hello"), ConfigurationError);
  EXPECT_THROW(parse("true: hello"), ConfigurationError);
  EXPECT_NO_THROW(parse("'1': hello"));
}

TEST(YamlScalars, RecursiveAliasesAreRejected) {
  EXPECT_THROW(parse("a: &anchor\n  b: *anchor\n"), ConfigurationError);
}

TEST(YamlScalars, DeepNestingIsRejected) {
  std::string document;
  for (int index = 0; index < 400; ++index) document += "[";
  for (int index = 0; index < 400; ++index) document += "]";
  EXPECT_THROW(parse(document), ConfigurationError);
}

TEST(YamlScalars, IntegerRangeIsBounded) {
  EXPECT_THROW(parse("a: 9223372036854775808"), ConfigurationError);
  EXPECT_EQ(parse("a: 9223372036854775807")["a"].get<int64_t>(), INT64_MAX);
  EXPECT_EQ(parse("a: -9223372036854775808")["a"].get<int64_t>(), INT64_MIN);
}

TEST(YamlScalars, SpecialFloatsAreRecognised) {
  EXPECT_TRUE(parse("a: .inf")["a"].is_number_float());
  EXPECT_TRUE(std::isinf(parse("a: .inf")["a"].get<double>()));
  EXPECT_TRUE(std::isnan(parse("a: .nan")["a"].get<double>()));
}
