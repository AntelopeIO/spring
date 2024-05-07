#pragma once

#include <vector>
#include <tuple>

namespace fc {

/**
 * @class ordered_diff
 * @brief Provides ablity to generate and apply diff of containers of type T
 *
 * Example use:
 *    std::vector<char> source = { 'a', 'b', 'f', 'c', 'd' };
 *    std::vector<char> target = { 'b', 'f', 'c', 'd', 'e', 'h' };
 *    ordered_diff<char>::diff_result diff = ordered_diff<char>::diff(source, target);
 *    ordered_diff<char>::apply_diff(source, std::move(diff));
 *    assert(source == target);
 *
 * @param T type stored in Container, must provide ==
 */
template <typename T, template<typename Y, typename...> typename Container = std::vector>
requires std::equality_comparable<T> && std::random_access_iterator<typename Container<T>::iterator>
class ordered_diff {
public:
   struct diff_result {
      Container<size_t>                remove_indexes;
      Container<std::pair<size_t, T>>  insert_indexes;
   };

   /// Generate diff_result that when `apply_diff(source, diff_result)` to source will generate target
   static diff_result diff(const Container<T>& source, const Container<T>& target) {
      size_t s = 0;
      size_t t = 0;

      diff_result result;
      while (s < source.size() || t < target.size()) {
         if (s < source.size() && t < target.size()) {
            if (source[s] == target[t]) {
               // nothing to do, skip over
               ++s;
               ++t;
            } else { // not equal
               if (s == source.size() - 1 && t == target.size() - 1) {
                  // both at end, insert target and remove source
                  result.remove_indexes.push_back(s);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++s;
                  ++t;
               } else if (s + 1 < source.size() && t + 1 < target.size() && source[s + 1] == target[t + 1]) {
                  // misalignment, but next value equal, insert and remove
                  result.remove_indexes.push_back(s);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++s;
                  ++t;
               } else if (t + 1 < target.size() && source[s] == target[t + 1]) {
                  // source equals next target, insert current target
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++t;
               } else { // source[s + 1] == target[t]
                  // target matches next source, remove current source
                  result.remove_indexes.push_back(s);
                  ++s;
               }
            }
         } else if (s < source.size()) {
            // remove extra in source
            result.remove_indexes.push_back(s);
            ++s;
         } else if (t < target.size()) {
            // insert extra in target
            result.insert_indexes.emplace_back(t, target[t]);
            ++t;
         }
      }

      return result;
   }

   template <typename X>
   requires std::same_as<std::decay_t<X>, diff_result>
   static void apply_diff(Container<T>& source, X&& diff) {
      // Remove from the source based on diff.remove_indexes
      std::ptrdiff_t offset = 0;
      for (size_t index : diff.remove_indexes) {
         source.erase(source.begin() + index + offset);
         --offset;
      }

      // Insert into the source based on diff.insert_indexes
      for (auto& t : diff.insert_indexes) {
         size_t index = std::get<0>(t);
         auto&  value = std::get<1>(t);
         source.insert(source.begin() + index, std::move(value));
      }
   }

}; // class ordered_diff

} // namespace fc
