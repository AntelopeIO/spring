#include <boost/test/unit_test.hpp>

#include <fc/container/ordered_diff.hpp>
#include <fc/exception/exception.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(ordered_diff_tests)

void print_diff(const auto& diff) {
   // std::cout << "Remove: [";
   // for (const auto& e : diff.remove_indexes) {
   //    std::cout << e << ", ";
   // }
   // std::cout << "]\n";
   // std::cout << "Add: [";
   // for (const auto& e : diff.insert_indexes) {
   //    std::cout << e.first << "|" << e.second << ", ";
   // }
   // std::cout << "]\n";
}

// verify only the inserts are in the diff insert_indexes
void verify_inserted(const auto& diff, const auto& inserts) {
   BOOST_TEST_REQUIRE(diff.insert_indexes.size() == inserts.size());
   for (size_t i = 0; i < inserts.size(); ++i) {
      BOOST_TEST(diff.insert_indexes[i].second == inserts[i]);
   }
}

BOOST_AUTO_TEST_CASE(ordered_diff_test) try {
   using namespace std;

   { // Basic case
      vector<char>       source = {'a', 'b', 'c', 'd', 'e'};
      vector<char>       target = {'a', 'c', 'e', 'f'};
      auto               result = ordered_diff<char>::diff(source, target);
      print_diff(result);
      verify_inserted(result, std::vector{'f'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Basic case, deque
      using ordered_deque_char_diff = ordered_diff<char, uint16_t, std::deque>;
      deque<char>       source = {'a', 'x', 'c', 'd', 'e'};
      deque<char>       target = {'z', 'c', 'y', 'f'};
      auto               result = ordered_deque_char_diff::diff(source, target);
      print_diff(result);
      verify_inserted(result, std::vector{'z', 'y', 'f'});
      source = ordered_deque_char_diff::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Empty vectors
      vector<char> source;
      vector<char> target;
      ordered_diff<char, uint8_t>::diff_result result = ordered_diff<char, uint8_t>::diff(source, target);
      print_diff(result);
      verify_inserted(result, target);
      source = ordered_diff<char, uint8_t>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // All elements removed
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target;
      auto result = ordered_diff<char, int>::diff(source, target);
      verify_inserted(result, target);
      source = ordered_diff<char, int>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // All elements inserted
      vector<char> source;
      vector<char> target = {'a', 'b', 'c', 'd', 'e'};
      auto result = ordered_diff<char>::diff(source, target);
      print_diff(result);
      verify_inserted(result, target);
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // No change
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = source;
      auto result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector<char>{});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Mix of removals and inserts
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'a', 'c', 'e', 'f', 'g', 'h'};
      ordered_diff<char>::diff_result result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'f', 'g', 'h'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Mix of removals and inserts
      vector<int> source = {1, 2, 3, 4, 5};
      vector<int> target = {3, 4, 6, 2, 0};
      auto result = ordered_diff<int>::diff(source, target);
      // 2 insert because order changed between 3,4 and 2
      verify_inserted(result, std::vector{6, 2, 0});
      source = ordered_diff<int>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Complete change
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'f', 'g', 'h', 'i'};
      auto result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, target);
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Diff order
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'e', 'd', 'c', 'b', 'a'};
      auto result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'d', 'c', 'b', 'a'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // shift left
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'b', 'c', 'd', 'e', 'f'};
      auto result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'f'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // shift right
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'z', 'a', 'b', 'c', 'd'};
      auto result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'z'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // non-unique
      vector<char> source = {'a', 'b', 'c', 'd', 'e', 'c', 'a', 'q'};
      vector<char> target = {'z', 'a', 'b', 'c', 'd', 'a'};
      auto result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'z'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Long diff
      vector<char>       source = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      vector<char>       target = {'x', 'y', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      auto               result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'x', 'y'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Longer diff
      vector<char>       source = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      vector<char>       target = {'x', 'y', 'z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      auto               result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'x', 'y', 'z'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Longer still diff
      vector<char>       source = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      vector<char>       target = {'t', 'u', 'v', 'w', 'x', 'y', 'z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      auto               result = ordered_diff<char>::diff(source, target);
      verify_inserted(result, std::vector{'t', 'u', 'v', 'w', 'x', 'y', 'z'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   { // Longer still diff with additional diff
      vector<char>       source = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
      vector<char>       target = {'t', 'u', 'v', 'w', 'x', 'y', 'z', 'a', 'b', 'c', 'd', 'x', 'e', 'f', 'g', 'h'};
      auto               result = ordered_diff<char>::diff(source, target);
      print_diff(result);
      verify_inserted(result, std::vector{'t', 'u', 'v', 'w', 'x', 'y', 'z', 'x'});
      source = ordered_diff<char>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
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
      vector<string> source = {"hello", "how", "are", "you", "today"};
      vector<string> target = {"hi", "are", "you", "here"};
      auto result = ordered_diff<string>::diff(source, target);
      source = ordered_diff<string>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   {
      vector<string> source = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      vector<string> target = {"prod2", "prod1", "prod3", "prod4", "prod5"};
      auto result = ordered_diff<string>::diff(source, target);
      source = ordered_diff<string>::apply_diff(std::move(source), result);
      BOOST_TEST(source == target);
   }
   {
      vector<string> source = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      vector<string> target = {"prod5", "prod1", "prod2", "prod3", "prod4"};
      auto result = ordered_diff<string>::diff(source, target);
      source = ordered_diff<string>::apply_diff(std::move(source), std::move(result));
      BOOST_TEST(source == target);
   }
   {
      vector<string> source = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      vector<string> target = {"prod2", "prod3", "prod4", "prod5", "prod6"};
      auto result = ordered_diff<string>::diff(source, target);
      source = ordered_diff<string>::apply_diff(std::move(source), std::move(result));
      BOOST_TEST(source == target);
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
      BOOST_TEST(count_moves::num_moves == 2u); // one move is for std::reverse
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()
