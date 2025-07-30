#pragma once

#include <eosio/chain/webassembly/eos-vm-oc/eos-vm-oc.hpp>
#include <eosio/chain/webassembly/eos-vm-oc/ipc_helpers.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/key_extractors.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/lockfree/queue.hpp>

#include <boost/interprocess/mem_algo/rbtree_best_fit.hpp>
#include <boost/asio/local/datagram_protocol.hpp>

#include <fc/crypto/sha256.hpp>

#include <atomic>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace eosio { namespace chain { namespace eosvmoc {

using namespace boost::multi_index;
using namespace boost::asio;

namespace bip = boost::interprocess;

using allocator_t = bip::rbtree_best_fit<bip::null_mutex_family, bip::offset_ptr<void>, alignof(std::max_align_t)>;

struct config;

class code_cache_base {
   public:
      code_cache_base(const std::filesystem::path& data_dir, const eosvmoc::config& eosvmoc_config, const chainbase::database& db);
      ~code_cache_base();

      const int& fd() const { return _cache_fd; }

      void free_code(const digest_type& code_id, const uint8_t& vm_version);

      // mode for get_descriptor_for_code calls
      struct mode {
         bool whitelisted = false;
         bool high_priority = false;
         bool write_window = true;
      };

      // get_descriptor_for_code failure reasons
      enum class get_cd_failure {
         temporary, // oc compile not done yet, users like read-only trxs can retry
         permanent  // oc will not start, users should not retry
      };

   protected:
      struct by_hash;

      typedef boost::multi_index_container<
         code_descriptor,
         indexed_by<
            sequenced<>,
            hashed_unique<tag<by_hash>,
               member<code_descriptor, digest_type, &code_descriptor::code_hash>
            >
         >
      > code_cache_index;
      code_cache_index _cache_index;

      const chainbase::database& _db;
      eosvmoc::config            _eosvmoc_config;

      std::filesystem::path _cache_file_path;
      int                   _cache_fd;
      std::atomic<uint64_t> _executing_id{0}; // id of executing action

      io_context _ctx;
      local::datagram_protocol::socket _compile_monitor_write_socket{_ctx}; // protected by _mtx for async
      local::datagram_protocol::socket _compile_monitor_read_socket{_ctx};

      struct queued_compile_entry {
         compile_wasm_message    msg;
         std::vector<char>       code;

         const digest_type&      code_id() const { return msg.code.code_id; }
      };
      //these are really only useful to the async code cache, but keep them here so free_code can be shared
      using queued_compiles_t = boost::multi_index_container<
         queued_compile_entry,
         indexed_by<
            sequenced<>,
            hashed_unique<tag<by_hash>,
               const_mem_fun<queued_compile_entry, const digest_type&, &queued_compile_entry::code_id>>
         >
      >;
      std::mutex                             _mtx;
      queued_compiles_t                      _queued_compiles;                  // protected by _mtx
      std::unordered_map<digest_type, bool>  _outstanding_compiles_and_poison;  // protected by _mtx
      std::atomic<size_t>                    _outstanding_compiles{0};

      size_t _free_bytes_eviction_threshold;
      void check_eviction_threshold(size_t free_bytes);
      void run_eviction_round();

      void set_on_disk_region_dirty(bool);

      template <typename T>
      void serialize_cache_index(fc::datastream<T>& ds);
};

class code_cache_async : public code_cache_base {
   public:
      // called from async thread, provides code_id of any compiles spawned by get_descriptor_for_code
      using compile_complete_callback = std::function<void(boost::asio::io_context&, const digest_type&, fc::time_point)>;

      code_cache_async(const std::filesystem::path& data_dir, const eosvmoc::config& eosvmoc_config,
                       const chainbase::database& db, compile_complete_callback cb);
      ~code_cache_async();

      //If code is in cache: returns pointer & bumps to front of MRU list
      //If code is not in cache, and not blacklisted, and not currently compiling: return nullptr and kick off compile
      //otherwise: return nullptr
      const code_descriptor* const get_descriptor_for_code(mode m, account_name receiver, const digest_type& code_id,
                                                           const uint8_t& vm_version, get_cd_failure& failure);

   private:
      compile_complete_callback _compile_complete_func; // called from async thread, provides executing_action_id
      std::thread _monitor_reply_thread;
      boost::lockfree::queue<wasm_compilation_result_message> _result_queue;
      std::unordered_set<digest_type> _blacklist;
      size_t _threads;

      void wait_on_compile_monitor_message();
      std::tuple<size_t, size_t> consume_compile_thread_queue();
      void process_queued_compiles();
      void write_message(const digest_type& code_id, const eosvmoc_message& message, std::span<wrapped_fd> fds);

};

class code_cache_sync : public code_cache_base {
   public:
      using code_cache_base::code_cache_base;
      ~code_cache_sync();

      //Can still fail and return nullptr if, for example, there is an expected instantiation failure
      const code_descriptor* const get_descriptor_for_code_sync(mode m, account_name receiver,
                                                                const digest_type& code_id, const uint8_t& vm_version);
};

}}}
