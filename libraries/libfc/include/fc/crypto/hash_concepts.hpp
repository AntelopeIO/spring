#pragma once

#include <concepts>

template <typename T>
concept ContiguousCharSource = requires(const T& obj) {
   { obj.data() } -> std::convertible_to<const char*>;
   { obj.size() } -> std::unsigned_integral;
};