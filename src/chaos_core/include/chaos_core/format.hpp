// Formatting helpers for diagnostic strings. The health snapshot values are
// consumed by existing dashboards and tests, so finite values in the
// non-scientific range [1e-4, 1e16) render exactly like Python's repr():
// shortest roundtrip digits in fixed notation, with ".0" appended to
// integral values. Outside that range Python falls back to scientific
// notation with a signed, at-least-two-digit exponent; this helper matches
// that form as well.
#pragma once

#include <charconv>
#include <cmath>
#include <string>

namespace chaos {

namespace detail {
inline std::string normalizeExponent(std::string text) {
  const std::size_t e = text.find('e');
  if (e == std::string::npos) {
    return text;
  }
  std::string mantissa = text.substr(0, e);
  std::string exponent = text.substr(e + 1);
  char sign = '+';
  if (!exponent.empty() && (exponent[0] == '+' || exponent[0] == '-')) {
    sign = exponent[0];
    exponent.erase(exponent.begin());
  }
  while (exponent.size() < 2) {
    exponent.insert(exponent.begin(), '0');
  }
  return mantissa + "e" + sign + exponent;
}
}  // namespace detail

// Python str(float) for finite values, as described above.
inline std::string pythonFloatStr(double value) {
  if (std::isnan(value)) {
    return "nan";
  }
  if (std::isinf(value)) {
    return value < 0 ? "-inf" : "inf";
  }
  char buffer[64];
  if (value != 0.0 &&
      (std::fabs(value) < 1e-4 || std::fabs(value) >= 1e16)) {
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    return detail::normalizeExponent(std::string(buffer, result.ptr));
  }
  const auto result =
    std::to_chars(buffer, buffer + sizeof(buffer), value,
                  std::chars_format::fixed);
  std::string text(buffer, result.ptr);
  if (text.find('.') == std::string::npos) {
    text += ".0";
  }
  return text;
}

}  // namespace chaos
