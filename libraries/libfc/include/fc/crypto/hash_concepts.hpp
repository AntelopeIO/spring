#pragma once

#include <tuple>
#include <type_traits>
#include <cstdint>

namespace fc {

template<typename Arg0>
concept FirstArgDecaysToCharPtr =
    std::is_same_v<std::decay_t<Arg0>, char*> ||
    std::is_same_v<std::decay_t<Arg0>, const char*>;

template<typename Arg1>
concept SecondArgIsConvertibleToUint32 =
    std::is_convertible_v<std::decay_t<Arg1>, uint32_t>;

/* Used on template<typename... T> hash_type hash(const T&... t); to prevent calls such as
 * std::vector<char> foo;
 * hash(foo.data(), foo.size())
 * calling the variadic hash instead of the existing hash(const char* d, uint32_t dlen)
 */
template<typename... T>
concept NotTwoArgsCharUint32 =
    sizeof...(T) != 2 ||
    !FirstArgDecaysToCharPtr<std::tuple_element_t<0, std::tuple<T...>>> ||
    !SecondArgIsConvertibleToUint32<std::tuple_element_t<1, std::tuple<T...>>>;

}