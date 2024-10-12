#include <boost/test/unit_test.hpp>

#include <fc/container/ordered_diff.hpp>
#include <fc/exception/exception.hpp>

#include <algorithm>
#include <random>

using namespace fc;

template <typename T, typename SizeType = size_t, template <typename Y, typename...> typename Container = std::vector>
struct checker {
   using container    = Container<T>;
   using ordered_diff = fc::ordered_diff<T, SizeType, Container>;
   using diff_result  = ordered_diff::diff_result;

   struct validate_result {
      container    restored_target;
      diff_result  diff;
   };

   // this prints out the `diff_result` in a form that can be used as an initializer for a `diff_result`
   static void Print(diff_result& dr) {
      auto print_ = []<typename V>(const V& v) {
          if constexpr (std::is_same_v<V, char>)
             std::cout << '\'' << v << '\'';
          else if constexpr (std::is_same_v<V, uint8_t>)
             std::cout << (int)v;
          else if constexpr (std::is_same_v<V, std::string>)
             std::cout << '\"' << v << '\"';
          else
             std::cout << v;
      };

      auto remove_out = [&](const auto& v) {
         std::cout << '{';
         for (size_t i = 0; i < v.size(); ++i) {
            print_(v[i]);
            std::cout << (i == v.size() - 1 ? "" : ", ");
         }
         std::cout << '}';
      };

      auto insert_out = [&](const auto& v) {
         std::cout << '{';
         for (size_t i = 0; i < v.size(); ++i) {
            std::cout << '{';
            print_(v[i].first);
            std::cout << ", ";
            print_(v[i].second);
            std::cout << '}' << (i == v.size() - 1 ? "" : ", ");
         }
         std::cout << '}';
      };

      std::cout << '{';
      remove_out(dr.remove_indexes);
      std::cout << ", ";
      insert_out(dr.insert_indexes);
      std::cout << '}' << '\n';
   }

   static validate_result validate(container source, const container& target) {
      auto diff = ordered_diff::diff(source, target);
      auto restored_target = ordered_diff::apply_diff(std::move(source), diff);
      return { std::move(restored_target), std::move(diff) };
   }
};

BOOST_AUTO_TEST_SUITE(ordered_diff_tests)

BOOST_AUTO_TEST_CASE(ordered_diff_test) try {
   using namespace std;

   { // Basic case
      using checker = checker<char>;

      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = {'a', 'c', 'e', 'f'};
      checker::diff_result expected_diff = {{1, 3}, {{3, 'f'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Basic case, deque
      using checker = checker<char, uint16_t, std::deque>;
      checker::container   source        = {'a', 'x', 'c', 'd', 'e'};
      checker::container   target        = {'z', 'c', 'y', 'f'};
      checker::diff_result expected_diff = {{0, 1, 3, 4}, {{0, 'z'}, {2, 'y'}, {3, 'f'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Empty vectors
      using checker = checker<char, uint8_t>;
      checker::container   source;
      checker::container   target;
      checker::diff_result expected_diff = {{}, {}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // All elements removed
      using checker = checker<char, unsigned int>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target;
      checker::diff_result expected_diff = {{0, 1, 2, 3, 4}, {}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // All elements removed, size 1
      using checker = checker<char, unsigned int>;
      checker::container   source        = {'a'};
      checker::container   target;
      checker::diff_result expected_diff = {{0}, {}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // All elements inserted
      using checker = checker<char>;
      checker::container   source;
      checker::container   target        = {'a', 'b', 'c', 'd', 'e'};
      checker::diff_result expected_diff = {{}, {{0, 'a'}, {1, 'b'}, {2, 'c'}, {3, 'd'}, {4, 'e'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // All elements inserted, size 1
      using checker = checker<char>;
      checker::container   source;
      checker::container   target        = {'a'};
      checker::diff_result expected_diff = {{}, {{0, 'a'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // No change
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = source;
      checker::diff_result expected_diff = {{}, {}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // No change, size 1
      using checker = checker<char>;
      checker::container   source        = {'a'};
      checker::container   target        = source;
      checker::diff_result expected_diff = {{}, {}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Mix of removals and inserts
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = {'a', 'c', 'e', 'f', 'g', 'h'};
      checker::diff_result expected_diff = {{1, 3}, {{3, 'f'}, {4, 'g'}, {5, 'h'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Mix of removals and inserts
      using checker = checker<int>;
      checker::container   source        = {1, 2, 3, 4, 5};
      checker::container   target        = {3, 4, 6, 2, 0};
      checker::diff_result expected_diff = {{0, 1, 4}, {{2, 6}, {3, 2}, {4, 0}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Complete change
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = {'f', 'g', 'h', 'i'};
      checker::diff_result expected_diff = {{0, 1, 2, 3, 4}, {{0, 'f'}, {1, 'g'}, {2, 'h'}, {3, 'i'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Complete change, size 1
      using checker = checker<char>;
      checker::container   source        = {'a'};
      checker::container   target        = {'f'};
      checker::diff_result expected_diff = {{0}, {{0, 'f'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Complete change equal sizes
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd'};
      checker::container   target        = {'f', 'g', 'h', 'i'};
      checker::diff_result expected_diff = {{0, 1, 2, 3}, {{0, 'f'}, {1, 'g'}, {2, 'h'}, {3, 'i'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Diff order
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = {'e', 'd', 'c', 'b', 'a'};
      checker::diff_result expected_diff = {{0, 1, 2, 4}, {{0, 'e'}, {2, 'c'}, {3, 'b'}, {4, 'a'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // Diff order, size 2
      using checker = checker<char>;
      checker::container   source        = {'a', 'b'};
      checker::container   target        = {'b', 'a'};
      checker::diff_result expected_diff = {{1}, {{0, 'b'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // shift left
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = {'b', 'c', 'd', 'e', 'f'};
      checker::diff_result expected_diff = {{0}, {{4, 'f'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // shift right
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e'};
      checker::container   target        = {'z', 'a', 'b', 'c', 'd'};
      checker::diff_result expected_diff = {{4}, {{0, 'z'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // non-unique
      using checker = checker<char>;
      checker::container   source        = {'a', 'b', 'c', 'd', 'e', 'c', 'a', 'q'};
      checker::container   target        = {'z', 'a', 'b', 'c', 'd', 'a'};
      checker::diff_result expected_diff = {{4, 5, 7}, {{0, 'z'}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   { // full
      vector<uint8_t> source(std::numeric_limits<uint8_t>::max()+1);
      std::iota(source.begin(), source.end(), 0);
      vector<uint8_t> target(source.size());
      std::reverse_copy(source.begin(), source.end(), target.begin());
      auto result = ordered_diff<uint8_t, uint8_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
      target.clear();
      result = ordered_diff<uint8_t, uint8_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
      source.clear();
      result = ordered_diff<uint8_t, uint8_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // full, random
      std::random_device rnd_device;
      std::mt19937 mersenne_engine {rnd_device()};
      std::uniform_int_distribution<uint8_t> dist {0, std::numeric_limits<uint8_t>::max()};
      auto gen = [&](){ return dist(mersenne_engine); };
      vector<uint8_t> source(std::numeric_limits<uint8_t>::max()+1);
      std::generate(source.begin(), source.end(), gen);
      vector<uint8_t> target(source.size());
      std::reverse_copy(source.begin(), source.end(), target.begin());
      auto result = ordered_diff<uint8_t, uint8_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
      target.clear();
      result = ordered_diff<uint8_t, uint8_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
      source.clear();
      result = ordered_diff<uint8_t, uint8_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // non-unique full
      vector<uint8_t> source(std::numeric_limits<uint8_t>::max()*2);
      std::iota(source.begin(), source.begin()+std::numeric_limits<uint8_t>::max(), 0);
      std::iota(source.begin()+std::numeric_limits<uint8_t>::max(), source.end(), 0);
      vector<uint8_t> target(source.size());
      std::reverse_copy(source.begin(), source.end(), target.begin());
      auto result = ordered_diff<uint8_t, uint16_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint16_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
      target.clear();
      result = ordered_diff<uint8_t, uint16_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint16_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
      source.clear();
      result = ordered_diff<uint8_t, uint16_t>::diff(source, target);
      source = ordered_diff<uint8_t, uint16_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(ordered_diff_string_test) try {
   using namespace std;
   {
      using checker = checker<string>;
      checker::container   source        = {"hello", "how", "are", "you", "today"};
      checker::container   target        = {"hi", "are", "you", "here"};
      checker::diff_result expected_diff = {{0, 1, 4}, {{0, "hi"}, {3, "here"}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   {
      using checker = checker<string>;
      checker::container   source        = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      checker::container   target        = {"prod2", "prod1", "prod3", "prod4", "prod5"};
      checker::diff_result expected_diff = {{1}, {{0, "prod2"}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   {
      using checker = checker<string>;
      checker::container   source        = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      checker::container   target        = {"prod5", "prod1", "prod2", "prod3", "prod4"};
      checker::diff_result expected_diff = {{4}, {{0, "prod5"}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }
   {
      using checker = checker<string>;
      checker::container   source        = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      checker::container   target        = {"prod5", "prod1", "prod2", "prod3", "prod4"};
      checker::diff_result expected_diff = {{4}, {{0, "prod5"}}};
      auto vr = checker::validate(source, target);

      BOOST_TEST(vr.restored_target == target);

      bool diffs_match = vr.diff == expected_diff;
      BOOST_TEST(diffs_match);
   }

} FC_LOG_AND_RETHROW();

class count_moves {
   std::string s;
public:
   inline static size_t num_moves = 0;
   count_moves(const count_moves& m) : s(m.s) {};
   count_moves(count_moves&& m) noexcept : s(std::move(m.s)) { ++num_moves; };
   count_moves& operator=(const count_moves& rhs) = default;
   explicit count_moves(std::string s) : s(s) {};
   auto operator<=>(const count_moves&) const = default;
   bool operator==(const count_moves&) const = default;
};

BOOST_AUTO_TEST_CASE(ordered_diff_moveable_test) try {
   using namespace std;
   {
      vector<count_moves> source = {count_moves{"hello"}, count_moves{"there"}};
      vector<count_moves> target = {count_moves{"hi"}, count_moves{"there"}};
      auto result = ordered_diff<count_moves>::diff(source, target);
      source = ordered_diff<count_moves>::apply_diff(std::move(source), std::move(result));
      BOOST_TEST(source == target);
      BOOST_TEST(count_moves::num_moves == 1u);
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
