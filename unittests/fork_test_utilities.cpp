#include "fork_test_utilities.hpp"

private_key_type get_private_key( name keyname, string role ) {
   return private_key_type::regenerate<fc::ecc::private_key_shim>(fc::sha256::hash(keyname.to_string()+role));
}

public_key_type  get_public_key( name keyname, string role ){
   return get_private_key( keyname, role ).get_public_key();
}

void push_blocks( tester& from, tester& to, uint32_t block_num_limit ) {
   block_num_type from_head_num = std::min( from.control->fork_db_head().block_num(), block_num_limit );
   block_num_type to_head_num = to.control->fork_db_head().block_num();
   while( to_head_num < from_head_num) {
      auto fb = from.control->fetch_block_by_number( ++to_head_num );
      to.push_block( fb );
   }
}

namespace {
   template<typename Pred>
   bool produce_empty_blocks_until( base_tester& t, uint32_t max_num_blocks_to_produce, Pred&& pred) {
      for( uint32_t blocks_produced = 0;
           blocks_produced < max_num_blocks_to_produce;
           t.produce_block(), ++blocks_produced )
      {
         if( pred() )
            return true;
      }

      return pred();
   }
}

bool produce_until_transition( base_tester& t,
                               account_name last_producer,
                               account_name next_producer,
                               uint32_t max_num_blocks_to_produce )
{
   return produce_empty_blocks_until(t, max_num_blocks_to_produce, [&t, last_producer, next_producer]() {
      return t.control->pending_block_producer() == next_producer && t.control->head().producer() == last_producer;
   });
}

void produce_until_transition( base_tester& t) {
   account_name pending = t.control->pending_block_producer();
   do {
      t.produce_block();
   } while (t.control->pending_block_producer() == pending) ;
}

bool produce_until_blocks_from( base_tester& t,
                                const std::set<account_name>& expected_producers,
                                uint32_t max_num_blocks_to_produce )
{
   auto remaining_producers = expected_producers;
   return produce_empty_blocks_until(t, max_num_blocks_to_produce, [&t, &remaining_producers]() {
      remaining_producers.erase(t.control->head().producer());
      return remaining_producers.size() == 0;
   });
}

