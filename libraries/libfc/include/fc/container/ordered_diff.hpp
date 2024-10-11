#pragma once

#include <fc/exception/exception.hpp>
#include <fc/utility.hpp>

#include <vector>
#include <utility>

namespace fc {

/**
 * @class ordered_diff
 * @brief Provides ability to generate and apply diff of containers of type T
 *
 * NOTE: Part of Spring Consensus. Used for finalizer and proposer policies.
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
requires std::equality_comparable<T>
         && std::random_access_iterator<typename Container<T>::iterator>
         && std::is_unsigned_v<SizeType>
         && std::is_unsigned_v<typename Container<T>::size_type>
         && (std::numeric_limits<typename Container<T>::size_type>::max() >= std::numeric_limits<SizeType>::max())
class ordered_diff {
public:
   using size_type = SizeType;
   using container_size_type = typename Container<T>::size_type;

   struct diff_result {
      Container<SizeType>                remove_indexes;
      Container<std::pair<SizeType, T>>  insert_indexes;

      bool operator==(const diff_result&) const = default;
   };

   /// Generate diff_result that when `apply_diff(source, diff_result)` will modify source to be equal to target.
   static diff_result diff(const Container<T>& source, const Container<T>& target) {
      size_t s = 0;
      size_t t = 0;

      FC_ASSERT(source.empty() || (source.size() - 1) <= std::numeric_limits<SizeType>::max());
      FC_ASSERT(target.empty() || (target.size() - 1) <= std::numeric_limits<SizeType>::max());

      diff_result result;
      while (s < source.size() || t < target.size()) {
         assert(s <= source.size());
         assert(t <= target.size());
         if (s < source.size() && t < target.size()) {
            if (source[s] == target[t]) {
               // nothing to do, skip over
               ++s;
               ++t;
            } else { // not equal
               if (s == source.size() - 1 && t == target.size() - 1) {
                  // both at end, insert target and remove source
                  assert(s <= std::numeric_limits<SizeType>::max());
                  assert(t <= std::numeric_limits<SizeType>::max());
                  result.remove_indexes.push_back(s);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++s;
                  ++t;
               } else if (s + 1 < source.size() && t + 1 < target.size() && source[s + 1] == target[t + 1]) {
                  // misalignment, but next value equal, insert and remove
                  assert(s <= std::numeric_limits<SizeType>::max());
                  assert(t <= std::numeric_limits<SizeType>::max());
                  result.remove_indexes.push_back(s);
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++s;
                  ++t;
               } else if (t + 1 < target.size() && source[s] == target[t + 1]) {
                  // source equals next target, insert current target
                  assert(t <= std::numeric_limits<SizeType>::max());
                  result.insert_indexes.emplace_back(t, target[t]);
                  ++t;
               } else {
                  // not misalignment by one and source not equal to next target, so remove from source
                  // may be inserted later by other conditions if needed
                  assert(s <= std::numeric_limits<SizeType>::max());
                  result.remove_indexes.push_back(s);
                  ++s;
               }
            }
         } else if (s < source.size()) {
            // remove extra in source
            assert(s <= std::numeric_limits<SizeType>::max());
            result.remove_indexes.push_back(s);
            ++s;
         } else if (t < target.size()) {
            // insert extra in target
            assert(t <= std::numeric_limits<SizeType>::max());
            result.insert_indexes.emplace_back(t, target[t]);
            ++t;
         }
      }

      return result;
   }

   /// @param diff_in (input) the diff_result created from diff(source, target), apply_diff(std::move(source), diff_result) => target
   /// @param container the source of diff(source, target) to modify using the diff_result to produce original target
   /// @return the modified container now equal to original target
   template <typename X>
   requires std::same_as<std::decay_t<X>, diff_result>
   static Container<T> apply_diff(Container<T>&& container, X&& diff_in) {
      diff_result diff = std::forward<X>(diff_in); // do not use `X` as diff's type, we want either a copy or a move.

      // Remove from the source based on diff.remove_indexes
      for (container_size_type i = 0; i < diff.remove_indexes.size(); ++i) {
         FC_ASSERT(i == 0 || diff.remove_indexes[i] > diff.remove_indexes[i-1],
                   "diff.remove_indexes not strictly monotonically increasing: current index ${c}, previous index ${p}",
                   ("c", diff.remove_indexes[i])("p", diff.remove_indexes[i-1]));

         assert(diff.remove_indexes[i] >= i);
         auto updated_index = diff.remove_indexes[i] - i;
         FC_ASSERT(updated_index < container.size(), "diff.remove_indexes index ${idx} - i ${i} not in range ${s}",
                   ("idx", diff.remove_indexes[i])("i", i)("s", container.size()));
         container.erase(container.begin() + updated_index);
      }

      // Insert into the source based on diff.insert_indexes
      for (container_size_type i = 0; i < diff.insert_indexes.size(); ++i) {
         FC_ASSERT(i == 0 || diff.insert_indexes[i].first > diff.insert_indexes[i-1].first,
                   "diff.insert_indexes not strictly monotonically increasing: current index ${c}, previous index ${p}",
                   ("c", diff.insert_indexes[i].first)("p", diff.insert_indexes[i-1].first));
         auto& [index, value] = diff.insert_indexes[i];
         FC_ASSERT(index <= container.size(), "diff.insert_indexes index ${idx} not in range ${s}",
                   ("idx", index)("s", container.size()));
         container.insert(container.begin() + index, std::move(value));
      }
      return container;
   }

}; // class ordered_diff

} // namespace fc
