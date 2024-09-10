#pragma once

#include <eosio/chain/thread_utils.hpp>
#include <fc/time.hpp>
#include <atomic>

namespace eosio {

class production_pause_vote_tracker {
public:
   // By default, start with maximum vote_timeout so that production pause mechanism is effectively disabled.
   // Pre-conditions: vote_timeout and block_acceptance_tolerance should both not be negative.
   explicit production_pause_vote_tracker(fc::microseconds block_acceptance_tolerance = fc::milliseconds(250),
                                          fc::microseconds vote_timeout               = fc::microseconds::maximum())
      : negative_vote_timeout(), block_acceptance_delta(), latest_other_vote(), latest_producer_vote() {
      set_vote_timeout(vote_timeout);
      set_block_acceptance_tolerance(block_acceptance_tolerance);
   }

   // Can be called concurrently with all member functions except:
   //    + set_vote_timeout
   //    + check_pause_status
   //
   // Sets the vote timeout duration which drives the production pause status.
   //
   // Pre-condition: vote_timeout should be positive.
   void set_vote_timeout(fc::microseconds vote_timeout) {
      if (vote_timeout.count() > 0) {
         negative_vote_timeout = fc::microseconds(-vote_timeout.count());
      }
   }

   // Can be called concurrently with all member functions except:
   //    + set_block_acceptance_tolerance
   //    + record_received_block
   //
   // Sets the block acceptance tolerance duration which determines the relevancy of blocks to record.
   //
   // votes can be signaled before accepted_block_header, accepted_block_header is currently only signaled from the
   // main thread while votes can be signaled off the main thread. This allows votes to be signaled for a block before
   // the block is signaled. We could track last accepted block id and correlate that with the vote signal, but simpler
   // to add a signal_tolerance. Normally the tolerance only needs to be a few milliseconds, but no real harm in making
   // it larger. Half a block interval is a nice value.
   //
   // Pre-condition: block_acceptance_tolerance should not be negative.
   void set_block_acceptance_tolerance(fc::microseconds block_acceptance_tolerance) {
      assert(block_acceptance_tolerance.count() >= 0);

      if (block_acceptance_tolerance.count() >= 0) {
         block_acceptance_delta = fc::microseconds(-block_acceptance_tolerance.count());
      }
   }

   // Can be called concurrently with all member functions.
   //
   // Returns true if vote was recorded.
   //
   // The only reason another vote would not be recorded is if its received time is
   // not more recent than the received time of a previously recorded other vote.
   bool record_received_other_vote(fc::time_point vote_received_time) {
      return latest_other_vote.record_received_vote(vote_received_time);
   }

   // Can be called concurrently with all member functions.
   //
   // Returns true if vote was recorded.
   //
   // The only reason a producer vote would not be recorded is if its received time is
   // not more recent than the received time of a previously recorded producer vote.
   bool record_received_producer_vote(fc::time_point vote_received_time) {
      return latest_producer_vote.record_received_vote(vote_received_time);
   }

   // Can be called concurrently with all member functions except:
   //    + set_block_acceptance_tolerance
   //
   // This function only records the received block if it is relevant for the correct functioning of
   // the check_pause_status function evaluated at times no older than the received time of the block.
   //
   // Returns true if block was recorded.
   //
   // A block will only be recorded if all the following conditions hold for either
   // the latest recorded other vote or the latest recorded producer vote:
   //   1. The received time of the block must be more recent than received time of latest recorded
   //      (other/producer) vote by more than the current block acceptance threshold.
   //   2. If there is a still tracked recorded block with a received time more recent than the received time of
   //      the latest recorded (other/producer) vote, then that block's received time must be more recent than
   //      the received time of the new block to record.
   bool record_received_block(fc::time_point block_received_time) {
      bool recorded = false;
      recorded |= latest_other_vote.record_received_block(block_received_time, block_acceptance_delta);
      recorded |= latest_producer_vote.record_received_block(block_received_time, block_acceptance_delta);
      return recorded;
   }

   // Can be called concurrently with all member functions.
   //
   // This function loses track of any previously recorded blocks.
   void force_unpause() {
      latest_other_vote.reset_received_blocks();
      latest_producer_vote.reset_received_blocks();
   }

   struct pause_status {
      struct received_times {
         fc::time_point                latest_vote{};
         std::optional<fc::time_point> earliest_conflict_block{}; // If present, must be greater than latest_vote.
      };

      enum class pause_reason : unsigned int {
         not_paused        = 0b00,
         old_other_vote    = 0b01,
         old_producer_vote = 0b10,
         old_votes         = old_other_vote | old_producer_vote,
      };

      received_times other_vote{};
      received_times producer_vote{};

      pause_reason reason() const {
         unsigned int r = static_cast<unsigned int>(pause_reason::not_paused);

         if (other_vote.earliest_conflict_block.has_value()) {
            r |= static_cast<unsigned int>(pause_reason::old_other_vote);
         }

         if (producer_vote.earliest_conflict_block.has_value()) {
            r |= static_cast<unsigned int>(pause_reason::old_producer_vote);
         }

         return static_cast<pause_reason>(r);
      }

      bool should_pause() const {
         return reason() != pause_reason::not_paused;
      }
   };

   // Specify which vote timing check is needed in `check_pause_status`.
   // If `producer`, then `other_vote` in returned `pause_status` is
   // the default value and can be ignored.
   // If `other`, then `producer_vote` in returned `pause_status` is
   // the default value and can be ignored.
   // Regardless, `should_pause()` and `reason()` called on returned
   // `paused_status` is correct according to specified `pause_check`.
   enum class pause_check { producer, other, both };

   // Can be called concurrently with all member functions except:
   //    + set_vote_timeout
   //
   // Returns a struct that provides useful time information tracked by this class
   // which, most importantly, determines whether production should be paused.
   // To determine whether production should be paused or not, simply check the boolean value
   // returned from the should_pause member function of the returned struct.
   pause_status check_pause_status(fc::time_point current_time, pause_check check = pause_check::both) const {
      pause_status status;

      const auto threshold_time = fc::time_point(current_time).safe_add(negative_vote_timeout);

      auto process_vote_timing =
         [&](const vote_timing& vt, pause_status::received_times& rt) {
         const auto s = vt.get_vote_timing_status();

         rt.latest_vote = s.latest_vote;

         if (!s.first_block_after_vote.has_value())
            return;

         if (*s.first_block_after_vote < threshold_time) {
            rt.earliest_conflict_block = *s.first_block_after_vote;
         }
      };

      if (check != pause_check::producer) {
         assert(check == pause_check::both || check == pause_check::other);
         process_vote_timing(latest_other_vote, status.other_vote);
      }
      if (check != pause_check::other) {
         assert(check == pause_check::both || check == pause_check::producer);
         process_vote_timing(latest_producer_vote, status.producer_vote);
      }

      return status;
   }

private:
   class vote_timing {
   public:
      vote_timing() : latest_vote(), first_block_after_vote() {}

      // Can be called concurrently with all member functions.
      bool record_received_vote(fc::time_point vote_received_time) {
         auto vote_orig = latest_vote.load(std::memory_order_relaxed);

         if (vote_received_time <= vote_orig)
            return false;

         latest_vote.store(vote_received_time, std::memory_order_relaxed);

         return true;
      }

      // Can be called concurrently with all member functions.
      bool record_received_block(fc::time_point block_received_time, fc::microseconds block_acceptance_delta) {
         auto vote_time = latest_vote.load(std::memory_order_relaxed);

         const auto adjusted_block_received_time = fc::time_point(block_received_time).safe_add(block_acceptance_delta);

         if (adjusted_block_received_time <= vote_time)
            return false;

         auto block_orig = first_block_after_vote.load(std::memory_order_relaxed);

         if ((vote_time < block_orig) && (block_orig <= block_received_time))
            return false;

         first_block_after_vote.store(block_received_time, std::memory_order_relaxed);

         return true;
      }

      // Can be called concurrently with all member functions.
      void reset_received_blocks() {
         first_block_after_vote.store(fc::time_point{}, std::memory_order_relaxed);
      }

      struct vote_timing_status {
         fc::time_point                latest_vote;
         std::optional<fc::time_point> first_block_after_vote; // If present, must be greater than latest_vote.
      };

      // Can be called concurrently with all member functions.
      vote_timing_status get_vote_timing_status() const {
         vote_timing_status status;

         status.latest_vote = latest_vote.load(std::memory_order_relaxed);

         const auto block_time = first_block_after_vote.load(std::memory_order_relaxed);

         if (status.latest_vote < block_time) {
            status.first_block_after_vote = block_time;
         }

         return status;
      }

   private:
      std::atomic<fc::time_point> latest_vote;
      std::atomic<fc::time_point> first_block_after_vote;
      // Ignore if time is less than or equal to that of latest_vote.
   };

   fc::microseconds negative_vote_timeout;
   fc::microseconds block_acceptance_delta;
   alignas(chain::hardware_destructive_interference_sz)
   vote_timing      latest_other_vote;
   vote_timing      latest_producer_vote;
};

} // namespace eosio