#pragma once
#include <eosio/chain/block_state_legacy.hpp>
#include <eosio/chain/block_state.hpp>

namespace fc { class cfile_datastream; } // forward decl

namespace eosio::chain {

   template<class BSP>
   struct fork_database_impl;

   using block_branch_t = std::vector<signed_block_ptr>;
   enum class ignore_duplicate_t { no, yes };
   enum class include_root_t { no, yes };
   enum class fork_db_add_t {
      failure,            // add failed
      duplicate,          // already added and ignore_duplicate=true
      added,              // inserted into an existing branch or started a new branch, but not best branch
      appended_to_head,   // new best head of current best branch; no fork switch
      fork_switch         // new best head of new branch, fork switch to new branch
   };

   // Used for logging of comparison values used for best fork determination
   std::string log_fork_comparison(const block_state& bs);
   std::string log_fork_comparison(const block_state_legacy& bs);

   /**
    * @class fork_database_t
    * @brief manages light-weight state for all potential unconfirmed forks
    *
    * As new blocks are received, they are pushed into the fork database. The fork
    * database tracks the longest chain and the last irreversible block number. All
    * blocks older than the last irreversible block are freed after emitting the
    * irreversible signal.
    *
    * An internal mutex is used to provide thread-safety.
    *
    * fork_database should be used instead of fork_database_t directly as it manages
    * the different supported types.
    */
   template<class BSP>  // either block_state_legacy_ptr or block_state_ptr
   class fork_database_t {
   public:
      using bsp_t            = BSP;
      using bs_t             = bsp_t::element_type;
      using bhsp_t           = bs_t::bhsp_t;
      using bhs_t            = bhsp_t::element_type;
      using branch_t         = std::vector<bsp_t>;
      using full_branch_t    = std::vector<bhsp_t>;
      using branch_pair_t    = pair<branch_t, branch_t>;

      explicit fork_database_t();
      ~fork_database_t();

      void open( const char* desc, const std::filesystem::path& fork_db_file, fc::cfile_datastream& ds, validator_t& validator );
      void close( std::ofstream& out );
      size_t size() const;

      bsp_t get_block( const block_id_type& id, include_root_t include_root = include_root_t::no ) const;
      bool block_exists( const block_id_type& id ) const;
      bool validated_block_exists( const block_id_type& id, const block_id_type& claimed_id ) const;

      /**
       *  Purges any existing blocks from the fork database and resets the root block_header_state to the provided value.
       *  The head will also be reset to point to the root.
       */
      void reset_root( const bsp_t& root_bhs );

      /**
       *  Advance root block forward to some other block in the tree.
       */
      void advance_root( const block_id_type& id );

      /**
       *  Add block state to fork database.
       *  Must link to existing block in fork database or the root.
       *  @returns fork_db_add_t - result of the add
       *  @throws unlinkable_block_exception - unlinkable to any branch
       *  @throws fork_database_exception - no root, n is nullptr, protocol feature error, duplicate when ignore_duplicate=false
       */
      fork_db_add_t add( const bsp_t& n, ignore_duplicate_t ignore_duplicate );

      void remove( const block_id_type& id );

      /**
       * Remove all blocks >= block_num
       */
      void remove( block_num_type block_num);

      bool is_valid() const; // sanity checks on this fork_db

      bool   has_root() const;

      /**
       * Root of the fork database, not part of the index. Corresponds to head of the block log. Is an irreversible block.
       * On startup from a snapshot the root->block will be nullptr until root is advanced.
       * Undefined if !has_root()
       */
      bsp_t  root() const;

      /**
       * The best branch head of blocks in the fork database, can be null if include_root_t::no and fork_db is empty
       * @param include_root yes if root should be returned if no blocks in fork database
       */
      bsp_t  head(include_root_t include_root = include_root_t::no) const;

      /**
       * The calculated pending savanna LIB ID that will become LIB or is currently LIB
       */
      block_id_type pending_savanna_lib_id() const;
      bool set_pending_savanna_lib_id( const block_id_type& id );

      /**
       * @return true if id is built on top of pending savanna lib or id == pending_savanna_lib
       */
      bool is_descendant_of_pending_savanna_lib( const block_id_type& id ) const;

      /**
       * @param ancestor the id of a possible ancestor block
       * @param descendant the id of a possible descendant block
       * @return false if either ancestor or descendant not found.
       *         true if any descendant->previous.. == ancestor.
       *         false if unable to find ancestor in any descendant->previous..
       */
      bool is_descendant_of(const block_id_type& ancestor, const block_id_type& descendant) const;

      /**
       *  Returns the sequence of block states resulting from trimming the branch from the
       *  root block (exclusive) to the block with an id of `h` (inclusive) by removing any
       *  block states corresponding to block numbers greater than `trim_after_block_num`.
       *
       *  The order of the sequence is in descending block number order.
       *  A block with an id of `h` must exist in the fork database otherwise this method will throw an exception.
       */
      branch_t fetch_branch( const block_id_type& h, uint32_t trim_after_block_num = std::numeric_limits<uint32_t>::max() ) const;
      block_branch_t fetch_block_branch( const block_id_type& h, uint32_t trim_after_block_num = std::numeric_limits<uint32_t>::max() ) const;

      /**
       * Returns the sequence of block states resulting from trimming the branch from the
       * root block (exclusive) to the block with an id of `h` (inclusive) by removing any
       * block states that are after block `b`. Returns empty if `b` not found on `h` branch.
       */
      branch_t fetch_branch( const block_id_type& h, const block_id_type& b ) const;

      /**
       *  Returns full branch of block_header_state pointers including the root.
       *  The order of the sequence is in descending block number order.
       *  A block with an id of `h` must exist in the fork database otherwise this method will throw an exception.
       */
      full_branch_t fetch_full_branch( const block_id_type& h ) const;

      /**
       *  Returns the block state with a block number of `block_num` that is on the branch that
       *  contains a block with an id of`h`, or the empty shared pointer if no such block can be found.
       */
      bsp_t search_on_branch( const block_id_type& h, uint32_t block_num, include_root_t include_root = include_root_t::no ) const;

      /**
       * search_on_branch( head()->id(), block_num)
       */
      bsp_t search_on_head_branch( uint32_t block_num, include_root_t include_root = include_root_t::no ) const;

      /**
       *  Given two head blocks, return two branches of the fork graph that
       *  end with a common ancestor (same prior block)
       */
      branch_pair_t fetch_branch_from(const block_id_type& first, const block_id_type& second) const;

   private:
      unique_ptr<fork_database_impl<BSP>> my;
   };

   using fork_database_legacy_t = fork_database_t<block_state_legacy_ptr>;
   using fork_database_if_t     = fork_database_t<block_state_ptr>;

   /**
    * Provides mechanism for opening the correct type
    * as well as switching from legacy (old dpos) to instant-finality.
    *
    * All methods assert until open() is closed.
    */
   class fork_database {
   public:
      enum class in_use_t : uint32_t { legacy, savanna, both };

   private:
      static constexpr uint32_t magic_number = 0x30510FDB;

      const std::filesystem::path data_dir;
      std::atomic<in_use_t>  in_use = in_use_t::legacy;
      fork_database_legacy_t fork_db_l; // legacy
      fork_database_if_t     fork_db_s; // savanna

   public:
      explicit fork_database(const std::filesystem::path& data_dir);
      ~fork_database(); // close on destruction

      // not thread safe, expected to be called from main thread before allowing concurrent access
      void open( validator_t& validator );
      void close();
      bool file_exists() const;

      // return the size of the active fork_database
      size_t size() const;

      // switches to using both legacy and savanna during transition
      void switch_from_legacy(const block_state_ptr& root);
      void switch_to(in_use_t v) { in_use = v; }

      in_use_t version_in_use() const { return in_use.load(); }

      // see fork_database_t::fetch_branch(fork_db->head()->id())
      block_branch_t fetch_branch_from_head() const;

      template <class R, class F>
      R apply(const F& f) const {
         if constexpr (std::is_same_v<void, R>) {
            if (in_use.load() == in_use_t::legacy) {
               f(fork_db_l);
            } else {
               f(fork_db_s);
            }
         } else {
            if (in_use.load() == in_use_t::legacy) {
               return f(fork_db_l);
            } else {
               return f(fork_db_s);
            }
         }
      }

      template <class R, class F>
      R apply(const F& f) {
         if constexpr (std::is_same_v<void, R>) {
            if (in_use.load() == in_use_t::legacy) {
               f(fork_db_l);
            } else {
               f(fork_db_s);
            }
         } else {
            if (in_use.load() == in_use_t::legacy) {
               return f(fork_db_l);
            } else {
               return f(fork_db_s);
            }
         }
      }

      /// Apply for when only need lambda executed on savanna fork db
      template <class R, class F>
      R apply_s(const F& f) {
         if constexpr (std::is_same_v<void, R>) {
            if (auto in_use_value = in_use.load(); in_use_value == in_use_t::savanna || in_use_value == in_use_t::both) {
               f(fork_db_s);
            }
         } else {
            if (auto in_use_value = in_use.load(); in_use_value == in_use_t::savanna || in_use_value == in_use_t::both) {
               return f(fork_db_s);
            }
            return {};
         }
      }

      /// Apply for when only need lambda executed on savanna fork db
      template <class R, class F>
      R apply_s(const F& f) const {
         if constexpr (std::is_same_v<void, R>) {
            if (auto in_use_value = in_use.load(); in_use_value == in_use_t::savanna || in_use_value == in_use_t::both) {
               f(fork_db_s);
            }
         } else {
            if (auto in_use_value = in_use.load(); in_use_value == in_use_t::savanna || in_use_value == in_use_t::both) {
               return f(fork_db_s);
            }
            return {};
         }
      }

      /// Apply for when only need lambda executed on legacy fork db
      template <class R, class F>
      R apply_l(const F& f) const {
         if constexpr (std::is_same_v<void, R>) {
            if (auto in_use_value = in_use.load(); in_use_value == in_use_t::legacy || in_use_value == in_use_t::both) {
               f(fork_db_l);
            }
         } else {
            if (auto in_use_value = in_use.load(); in_use_value == in_use_t::legacy || in_use_value == in_use_t::both) {
               return f(fork_db_l);
            }
            return {};
         }
      }

      /// @param legacy_f the lambda to execute if in legacy mode
      /// @param savanna_f the lambda to execute if in savanna mode
      template <class R, class LegacyF, class SavannaF>
      R apply(const LegacyF& legacy_f, const SavannaF& savanna_f) {
         if constexpr (std::is_same_v<void, R>) {
            if (in_use.load() == in_use_t::legacy) {
               legacy_f(fork_db_l);
            } else {
               savanna_f(fork_db_s);
            }
         } else {
            if (in_use.load() == in_use_t::legacy) {
               return legacy_f(fork_db_l);
            } else {
               return savanna_f(fork_db_s);
            }
         }
      }

      /// @param legacy_f the lambda to execute if in legacy mode
      /// @param savanna_f the lambda to execute if in savanna mode
      template <class R, class LegacyF, class SavannaF>
      R apply(const LegacyF& legacy_f, const SavannaF& savanna_f) const {
         if constexpr (std::is_same_v<void, R>) {
            if (in_use.load() == in_use_t::legacy) {
               legacy_f(fork_db_l);
            } else {
               savanna_f(fork_db_s);
            }
         } else {
            if (in_use.load() == in_use_t::legacy) {
               return legacy_f(fork_db_l);
            } else {
               return savanna_f(fork_db_s);
            }
         }
      }

      // Update max_supported_version if the persistent file format changes.
      static constexpr uint32_t min_supported_version = 1;
      static constexpr uint32_t max_supported_version = 3;
   };
} /// eosio::chain

FC_REFLECT_ENUM( eosio::chain::fork_db_add_t,
                 (failure)(duplicate)(added)(appended_to_head)(fork_switch) )
