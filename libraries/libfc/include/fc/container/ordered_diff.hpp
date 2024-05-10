#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
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
   struct diff_result {
      Container<SizeType>                remove_indexes;
      Container<std::pair<SizeType, T>>  insert_indexes;
   };

   /// Generate diff_result that when `apply_diff(source, diff_result)` will modify source to be equal to target.
   static diff_result diff(const Container<T>& source, const Container<T>& target) {
      diff_result result;

      // longest common subsequence table using single row to minimize memory usage
      // See https://www.geeksforgeeks.org/minimum-number-deletions-insertions-transform-one-string-another/
      Container<size_t> lcs_row(target.size() + 1, 0);

      // Compute LCS
      for (size_t s = 1; s <= source.size(); ++s) {
         size_t prev = 0;
         for (size_t t = 1; t <= target.size(); ++t) {
            size_t curr = lcs_row[t];
            if (source[s - 1] == target[t - 1]) {
               lcs_row[t] = prev + 1;
            } else {
               lcs_row[t] = std::max(lcs_row[t], lcs_row[t - 1]);
            }
            prev = curr;
         }
      }

      // Use LCS to generate diff
      size_t s = source.size();
      size_t t = target.size();
      while (s > 0 && t > 0) {
         if (source[s - 1] == target[t - 1]) {
            --s;
            --t;
         } else if (lcs_row[t] > lcs_row[t - 1]) {
            assert(s - 1 <= std::numeric_limits<SizeType>::max());
            result.remove_indexes.push_back(s - 1);
            --s;
         } else {
            assert(t - 1 <= std::numeric_limits<SizeType>::max());
            result.insert_indexes.emplace_back(t - 1, target[t - 1]);
            --t;
         }
      }
      // handle remaining elements
      while (s > 0) {
         assert(s - 1 <= std::numeric_limits<SizeType>::max());
         result.remove_indexes.push_back(s - 1);
         --s;
      }
      while (t > 0) {
         assert(t - 1 <= std::numeric_limits<SizeType>::max());
         result.insert_indexes.emplace_back(t - 1, target[t - 1]);
         --t;
      }

      std::reverse(result.remove_indexes.begin(), result.remove_indexes.end());
      std::reverse(result.insert_indexes.begin(), result.insert_indexes.end());

      return result;
   }

   /// @param diff the diff_result created from diff(source, target), apply_diff(std::move(source), diff_result) => target
   /// @param container the source of diff(source, target) to modify using the diff_result to produce original target
   /// @return the modified container now equal to original target
   template <typename X>
   requires std::same_as<std::decay_t<X>, diff_result>
   static Container<T> apply_diff(Container<T>&& container, X&& diff) {
      // Remove from the source based on diff.remove_indexes
      std::ptrdiff_t offset = 0;
      for (SizeType index : diff.remove_indexes) {
         container.erase(container.begin() + index + offset);
         --offset;
      }

      // Insert into the source based on diff.insert_indexes
      for (auto& [index, value] : diff.insert_indexes) {
         container.insert(container.begin() + index, std::move(value));
      }
      return container;
   }

}; // class ordered_diff

} // namespace fc
