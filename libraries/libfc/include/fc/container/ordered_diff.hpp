#pragma once

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <ranges>
#include <type_traits>
#include <vector>
#include <utility>

namespace fc {

/**
 * @class ordered_diff
 * @brief Provides ability to generate and apply diff of containers of type T
 *
 * Minimizes the number of inserts to transform source to target.
 *
 * Example use:
 *    std::vector<char> source = { 'a', 'b', 'f', 'c', 'd' };
 *    std::vector<char> target = { 'b', 'f', 'c', 'd', 'e', 'h' };
 *    ordered_diff<char>::diff_result diff = ordered_diff<char>::diff(source, target);
 *    ordered_diff<char>::apply_diff(source, std::move(diff));
 *    assert(source == target);
 *
 * @param T type stored in Container, must provide ==
 * @param SizeType numeric type used for index into diff_result, for non-unique Containers a larger type may be required
 * @param Container container type for ordered diff and for diff_result
 */
template <typename T, typename SizeType = size_t, template<typename Y, typename...> typename Container = std::vector>
requires std::equality_comparable<T> && std::random_access_iterator<typename Container<T>::iterator>
class ordered_diff {
public:

   /// All indexes are in reference to the target. Apply in order: remove, insert, move.
   /// remove_indexes are in reverse order so they can be applied directly, see apply_diff.
   struct diff_result {
      using value_type = T;
      Container<SizeType>                       remove_indexes;
      Container<std::pair<SizeType, T>>         insert_indexes;
      Container<std::pair<SizeType, SizeType>>  move_indexes;
   };

   /// Generate diff_result that when `apply_diff(orig, diff_result)` will modify source to be equal to target.
   static diff_result diff(const Container<T>& orig, const Container<T>& target) {
      diff_result result;

      auto sort_input = [](auto&& r) -> Container<std::pair<T, size_t>> {
         Container<std::pair<T, size_t>> sorted;
         if constexpr (requires(Container<SizeType>& t) { t.reserve(0u); }) {
            sorted.reserve(std::ranges::size(r));
         }

         size_t index = 0;
         for (const auto& entry : r) {
            sorted.emplace_back(entry, index);
            ++index;
         }

         std::ranges::stable_sort(sorted, [](auto&& lhs, auto&& rhs) {
            return lhs.first < rhs.first;
         });

         return sorted;
      };

      auto orig_sorted = sort_input(orig);
      auto targ_sorted = sort_input(target);

      size_t orig_index = 0;
      size_t cur_index  = 0;

      while ((orig_index < orig_sorted.size()) || (cur_index < targ_sorted.size())) {
         std::weak_ordering cmp = std::weak_ordering::equivalent;

         if (orig_sorted.size() <= orig_index) {
            cmp = std::weak_ordering::greater;
         } else if (targ_sorted.size() <= cur_index) {
            cmp = std::weak_ordering::less;
         } else {
            cmp = (orig_sorted[orig_index].first <=> targ_sorted[cur_index].first);
         }

         if (cmp == std::weak_ordering::less) {
            result.remove_indexes.emplace_back(orig_sorted[orig_index].second);
            ++orig_index;
         } else if (cmp == std::weak_ordering::greater) {
            result.insert_indexes.emplace_back(targ_sorted[cur_index].second, std::move(targ_sorted[cur_index].first));
            ++cur_index;
         } else {
            // cmp == std::weak_ordering::equivalent
            assert(orig_sorted[orig_index].first == targ_sorted[cur_index].first);
            if (orig_sorted[orig_index].second != targ_sorted[cur_index].second) {
               result.move_indexes.emplace_back(orig_sorted[orig_index].second, targ_sorted[cur_index].second);
            }
            ++orig_index;
            ++cur_index;
         }
      }

      std::ranges::sort(result.remove_indexes, [](auto&& lhs, auto&& rhs) { return lhs > rhs; }); // descending
      std::ranges::sort(result.insert_indexes, [](auto&& lhs, auto&& rhs) { return lhs.first < rhs.first; });
      std::ranges::sort(result.move_indexes,   [](auto&& lhs, auto&& rhs) { return lhs.first < rhs.first; });

      // update move indexes
      if (!result.move_indexes.empty()) {
         // remove decreases `from` index
         auto m = result.move_indexes.begin();
         for (auto ritr = result.remove_indexes.rbegin(); ritr != result.remove_indexes.rend(); ++ritr) {
            m = std::lower_bound(m, result.move_indexes.end(), *ritr, [&](const auto& p, const auto& v) { return p.first < v; });
            for (auto i = m; i != result.move_indexes.end(); ++i) {
               --i->first;
            }
         }
         // insert increases `from` index
         m = result.move_indexes.begin();
         for (const auto& i : result.insert_indexes) {
            m = std::lower_bound(m, result.move_indexes.end(), i.first, [](const auto& p, const auto& v) { return p.first < v; });
            for (auto i = m; i != result.move_indexes.end(); ++i) {
               ++i->first;
            }
         }
         // remove any moves that are not needed, from == to
         result.move_indexes.erase(std::remove_if(result.move_indexes.begin(), result.move_indexes.end(),
                                                  [](const auto& p) { return p.first == p.second; }),
                                   result.move_indexes.end());
      }

      return result;
   }

   /// @param diff the diff_result created from diff(orig, target), apply_diff(std::move(orig), diff_result) => target
   /// @param container the orig of diff(orig, target) to modify using the diff_result to produce the target
   /// @return the modified container now equal to target
   template <typename X>
   requires std::same_as<std::decay_t<X>, diff_result>
   static Container<T> apply_diff(Container<T>&& container, X&& diff) {
      // Remove from the container based on diff.remove_indexes
      for (auto index : diff.remove_indexes) {
         container.erase(container.begin() + index);
      }

      // Insert into the container based on diff.insert_indexes
      for (auto&& [index, value] : diff.insert_indexes) {
         container.insert(container.begin() + index, std::move(value));
      }

      // Move in container based on diff.move_indexes
      Container<T> items;
      if constexpr (requires(Container<T>& t) { t.reserve(0u); }) {
         items.reserve(diff.move_indexes.size());
      }
      for (const auto& [from, _] : diff.move_indexes) {
         items.push_back(std::move(container[from]));
      }
      size_t i = 0;
      for (const auto& [_, to] : diff.move_indexes) {
         container[to] = std::move(items[i]);
         ++i;
      }
      return container;
   }

}; // class ordered_diff

} // namespace fc
