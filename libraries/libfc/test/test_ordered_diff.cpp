#include <boost/test/unit_test.hpp>

#include <fc/container/ordered_diff.hpp>
#include <fc/exception/exception.hpp>

using namespace fc;

BOOST_AUTO_TEST_SUITE(ordered_diff_tests)

BOOST_AUTO_TEST_CASE(ordered_diff_test) try {
   using namespace std;

   { // Basic case
      vector<char>       source = {'a', 'b', 'c', 'd', 'e'};
      vector<char>       target = {'a', 'c', 'e', 'f'};
      auto               result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // Basic case, deque
      using ordered_deque_char_diff = ordered_diff<char, std::deque>;
      deque<char>       source = {'a', 'x', 'c', 'd', 'e'};
      deque<char>       target = {'z', 'c', 'y', 'f'};
      auto               result = ordered_deque_char_diff::diff(source, target);
      ordered_deque_char_diff::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // Empty vectors
      vector<char> source;
      vector<char> target;
      ordered_diff<char>::diff_result result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // All elements removed
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target;
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // All elements inserted
      vector<char> source;
      vector<char> target = {'a', 'b', 'c', 'd', 'e'};
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // No change
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = source;
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // Mix of removals and inserts
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'a', 'c', 'e', 'f', 'g', 'h'};
      ordered_diff<char>::diff_result result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // Mix of removals and inserts
      vector<int> source = {1, 2, 3, 4, 5};
      vector<int> target = {3, 4, 6, 2, 0};
      auto result = ordered_diff<int>::diff(source, target);
      ordered_diff<int>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // Complete change
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'f', 'g', 'h', 'i'};
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // Diff order
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'e', 'd', 'c', 'b', 'a'};
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // shift left
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'b', 'c', 'd', 'e', 'f'};
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   { // shift right
      vector<char> source = {'a', 'b', 'c', 'd', 'e'};
      vector<char> target = {'z', 'a', 'b', 'c', 'd'};
      auto result = ordered_diff<char>::diff(source, target);
      ordered_diff<char>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_CASE(ordered_diff_string_test) try {
   using namespace std;
   {
      vector<string> source = {"hello", "how", "are", "you", "today"};
      vector<string> target = {"hi", "are", "you", "here"};
      auto result = ordered_diff<string>::diff(source, target);
      ordered_diff<string>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   {
      vector<string> source = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      vector<string> target = {"prod2", "prod1", "prod3", "prod4", "prod5"};
      auto result = ordered_diff<string>::diff(source, target);
      ordered_diff<string>::apply_diff(source, result);
      BOOST_TEST(source == target);
   }
   {
      vector<string> source = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      vector<string> target = {"prod5", "prod1", "prod2", "prod3", "prod4"};
      auto result = ordered_diff<string>::diff(source, target);
      ordered_diff<string>::apply_diff(source, std::move(result));
      BOOST_TEST(source == target);
   }
   {
      vector<string> source = {"prod1", "prod2", "prod3", "prod4", "prod5"};
      vector<string> target = {"prod2", "prod3", "prod4", "prod5", "prod6"};
      auto result = ordered_diff<string>::diff(source, target);
      ordered_diff<string>::apply_diff(source, std::move(result));
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
      ordered_diff<count_moves>::apply_diff(source, std::move(result));
      BOOST_TEST(source == target);
      BOOST_TEST(count_moves::num_moves == 1);
   }

} FC_LOG_AND_RETHROW();

BOOST_AUTO_TEST_SUITE_END()