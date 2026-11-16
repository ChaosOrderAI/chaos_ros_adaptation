// Shared detection of the `applied` field of an action result.
//
// A software update reports whether it committed bytes; the other actions
// have no such field. The trait lets the gateway and the SDK treat the
// result of an action type without naming that type, so both agree on which
// results carry the field.
#pragma once

#include <type_traits>
#include <utility>

namespace chaos {

/// True when the result of an action carries an `applied` field.
template<typename T, typename = void>
struct ResultHasApplied : std::false_type {};

template<typename T>
struct ResultHasApplied<T, std::void_t<decltype(std::declval<T &>().applied)>>
: std::true_type {};

}  // namespace chaos
