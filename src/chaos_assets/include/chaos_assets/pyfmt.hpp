#pragma once

#include <charconv>
#include <cmath>
#include <string>

namespace chaos::assets {

// Shortest round-trip float formatting matching CPython's repr()/json.dumps:
// scientific notation exactly when the decimal exponent is < -4 or >= 16,
// exponent sign always present and zero-padded to two digits, and a trailing
// ".0" for integral values in fixed notation.
inline std::string py_repr_float(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
  char buffer[64];
  auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  std::string shortest(buffer, result.ptr);
  const bool negative = !shortest.empty() && shortest[0] == '-';
  std::string body = negative ? shortest.substr(1) : shortest;

  std::string digits;
  long exponent = 0;
  const size_t epos = body.find_first_of("eE");
  if (epos != std::string::npos) {
    // Scientific form from to_chars: one digit before the point.
    exponent = std::stol(body.substr(epos + 1));
    std::string mantissa = body.substr(0, epos);
    digits = mantissa;
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
  } else {
    const size_t dot = body.find('.');
    std::string intpart = dot == std::string::npos ? body : body.substr(0, dot);
    const std::string fracpart = dot == std::string::npos ? "" : body.substr(dot + 1);
    if (intpart != "0") {
      exponent = static_cast<long>(intpart.size()) - 1;
      digits = intpart + fracpart;
    } else {
      const size_t first = fracpart.find_first_not_of('0');
      if (first == std::string::npos) {
        return negative ? "-0.0" : "0.0";
      }
      exponent = -static_cast<long>(first) - 1;
      digits = fracpart.substr(first);
    }
  }
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();

  std::string out = negative ? "-" : "";
  if (exponent < -4 || exponent >= 16) {
    out += digits[0];
    if (digits.size() > 1) out += "." + digits.substr(1);
    out += exponent < 0 ? "e-" : "e+";
    std::string exp_digits = std::to_string(exponent < 0 ? -exponent : exponent);
    if (exp_digits.size() < 2) exp_digits = "0" + exp_digits;
    out += exp_digits;
    return out;
  }
  if (exponent >= 0) {
    const size_t int_len = static_cast<size_t>(exponent) + 1;
    if (digits.size() <= int_len) {
      out += digits + std::string(int_len - digits.size(), '0') + ".0";
    } else {
      out += digits.substr(0, int_len) + "." + digits.substr(int_len);
    }
  } else {
    out += "0." + std::string(static_cast<size_t>(-exponent) - 1, '0') + digits;
  }
  return out;
}

inline std::string py_num(double value) { return py_repr_float(value); }

}  // namespace chaos::assets
