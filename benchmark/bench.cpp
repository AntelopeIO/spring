#include <chainbase/undo_index.hpp>
#include <chainbase/chainbase.hpp>
#include <filesystem>
#include <iostream>
#include <chrono>

#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>

namespace bip = boost::interprocess;
namespace fs  = std::filesystem;
namespace bmi = boost::multi_index;

template<typename T>
using test_allocator_base = chainbase::chainbase_node_allocator<T, chainbase::segment_manager>;

template<typename T>
class test_allocator : public test_allocator_base<T> {
public:
   using base = test_allocator_base<T>;
   test_allocator(chainbase::segment_manager *mgr) : base(mgr) {}
   template<typename U>   test_allocator(const test_allocator<U>& o) : base(o.get_segment_manager()) {}
   template<typename U>   struct rebind { using other = test_allocator<U>; };
   typename base::pointer allocate(std::size_t count) {
      return base::allocate(count);
   }
};

using shared_string = chainbase::shared_string;

struct elem_t {
   template<typename C, typename A>
   elem_t(C&& c, A&& a) : str(a) {
      c(*this);
   }
   
   friend std::ostream& operator<<(std::ostream& os, const elem_t& e) {
      os  << '[' << e.id << ", " << e.val << ']';
      return os;
   }
      
   uint64_t id;
   uint64_t val;
   shared_string str;
};

template<typename time_unit = std::milli>
struct stopwatch
{
   stopwatch() { _start = clock::now(); }
   ~stopwatch() {
      using duration_t = std::chrono::duration<float, time_unit>;
      point end = clock::now();
      float elapsed = std::chrono::duration_cast<duration_t>(end - _start).count();
      printf("Bench time %14.2fs\n", elapsed / 1000);
   }
   using clock = std::chrono::high_resolution_clock;
   using point = std::chrono::time_point<clock>;
   point _start;
};

template<typename T>
struct key_impl;
template<typename C, typename T>
struct key_impl<T C::*> { template<auto F> using fn = boost::multi_index::member<C, T, F>; };

template<auto Fn>
using key = typename key_impl<decltype(Fn)>::template fn<Fn>;


int main()
{
   fs::path temp = fs::temp_directory_path() / "pinnable_mapped_file";
   try {
      constexpr size_t num_elems = 32 * 1024 * 1024;
      chainbase::pinnable_mapped_file db(temp, true, 64 * num_elems, false, chainbase::pinnable_mapped_file::map_mode::mapped);
      test_allocator<elem_t> alloc(db.get_segment_manager());
      chainbase::undo_index<elem_t, test_allocator<elem_t>, bmi::ordered_unique<key<&elem_t::id>>> i0(alloc);
      boost::random::mt19937 gen;
      boost::random::uniform_int_distribution<> dist(1, num_elems);
      
      stopwatch sw;
      for (size_t i=0; i<num_elems; ++i) {
         size_t id = dist(gen);
         const elem_t* e = i0.find(id);
         if (e) {
            //std::cout << *e << '\n';
            i0.modify(*e, [old=e](elem_t& e) { e.val = old->val + 1; });
         } else {
            auto &e = i0.emplace([](elem_t& e) {
               e.val = 0;
               e.str = "a string";
            });
            if (e.id % 5 == 0)
               i0.remove(e);
         }
      }
   } catch (...) {
      fs::remove_all(temp);
      throw;
   }
   fs::remove_all(temp);
   return 0;
}
