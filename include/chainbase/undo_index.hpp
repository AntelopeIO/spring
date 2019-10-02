#ifndef EOSIO_UNDO_INDEX_HPP_INCLUDED
#define EOSIO_UNDO_INDEX_HPP_INCLUDED

#include <boost/intrusive/set.hpp>
#include <boost/container/deque.hpp>
#include <boost/throw_exception.hpp>
#include <memory>
#include <type_traits>

namespace eosio {

   template<typename F>
   struct scope_exit {
    public:
      scope_exit(F&& f) : _f(f) {}
      ~scope_exit() { if(!_canceled) _f(); }
      void cancel() { _canceled = true; }
    private:
      F _f;
      bool _canceled = false;
   };

   template<typename Key, typename T>
   struct get_key {
      using type = std::decay_t<decltype(Key{}(std::declval<const T&>().item()))>;
      decltype(auto) operator()(const T& arg) const { return Key{}(arg.item()); }
   };

   template<typename T>
   struct identity_key {
      using type = std::decay_t<decltype(std::declval<const T&>()._item)>;
      const auto& operator()(const T& t) { return t._item; }
   };

   template<typename Allocator, typename T>
   using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

   template<typename T, typename Allocator, typename... Keys>
   class undo_index {
      template<typename K>
      using hook = 
         boost::intrusive::set_base_hook<
            boost::intrusive::tag<K>,
            boost::intrusive::void_pointer<typename std::allocator_traits<Allocator>::void_pointer>,
            boost::intrusive::optimize_size<true>>;
    public:
      using id_type = std::decay_t<decltype(std::declval<T>().id)>;
      using value_type = T;

      undo_index() = default;
      explicit undo_index(const Allocator& a) : _undo_stack{a}, _allocator{a}, _new_ids_allocator{a} {}
      ~undo_index() {
         for(undo_state& state : _undo_stack) {
            dispose(state);
         }
         clear_impl<1>();
         std::get<0>(_indices).clear_and_dispose([&](node* p){ dispose(*p); });
      }
    
      struct node : hook<Keys>..., T {
         template<typename... A>
         node(A&&... a) : T{a...} {}
         const T& item() const { return *this; }
      };

      using indices_type = std::tuple<
         boost::intrusive::set<node, boost::intrusive::base_hook<hook<Keys>>, boost::intrusive::key_of_value<get_key<Keys, node>>>...>;

      struct id_node : hook<void>  {
         id_node(const id_type& id) : _item(id) {}
         id_type _item;
      };

      struct undo_state {
         std::tuple_element_t<0, indices_type> old_values;
         std::tuple_element_t<0, indices_type> removed_values;
         boost::intrusive::set<id_node, boost::intrusive::base_hook<hook<void>>, boost::intrusive::key_of_value<identity_key<id_node>>> new_ids;
         id_type old_next_id = 0;
      };

      template<typename Constructor>
      const value_type& emplace( Constructor&& c ) {
         node* p = _allocator.allocate(1);
         auto guard0 = scope_exit{[&]{ _allocator.deallocate(p, 1); }};
         _allocator.construct(p);
         auto guard1 = scope_exit{[&]{ _allocator.destroy(p); }};
         p->id = _next_id;
         c(static_cast<T&>(*p));
         if(!insert_impl(*p))
            BOOST_THROW_EXCEPTION( std::logic_error{ "could not insert object, most likely a uniqueness constraint was violated" } );
         auto guard2 = scope_exit{ [&]{ erase_impl(*p); } };
         on_create(*p);
         ++_next_id;
         guard2.cancel();
         guard1.cancel();
         guard0.cancel();
         return *p;
      }

      template<typename Modifier>
      void modify( const value_type& obj, Modifier&& m) {
         node* backup = on_modify(obj);
         node& node_ref = const_cast<node&>(static_cast<const node&>( obj ));
         erase_impl(node_ref);
         m(const_cast<T&>(obj));
         if(!insert_impl(node_ref) && backup) {
            insert_impl(*backup);
         }
      }

      void remove( const value_type& obj ) {
         auto& node_ref = const_cast<node&>(static_cast<const node&>(obj));
         erase_impl(node_ref);
         if(on_remove(node_ref)) {
            dispose(node_ref);
         }
      }

      template<typename CompatibleKey>
      const value_type* find( CompatibleKey&& key) const {
         const auto& index = std::get<0>(_indices);
         auto iter = index.find(key);
         if (iter != index.end()) {
            return &*iter;
         } else {
            return nullptr;
         }
      }

      class session {
       public:
         session(undo_index& idx, bool enabled)
          : _index(idx),
            _apply(enabled),
            _revision(enabled?idx.add_session():-1) {}
         ~session() { if(_apply) _index.undo(); }
         void push() { _apply = false; }
         void squash() {
            if ( _apply ) _index.squash();
            _apply = false;
         }
         void undo() {
            if ( _apply ) _index.undo();
            _apply = false;
         }
         int64_t revision() const { return _revision; }
       private:
         session(session&&) = delete;
         session& operator=(session&&) = delete;
         undo_index& _index;
         bool _apply = true;
         int64_t _revision = 0;
      };

      int64_t revision() const { return _revision; }

      session start_undo_session( bool enabled ) {
         return session{*this, enabled};
      }

     /**
      * Discards all undo history prior to revision
      */
     void commit( int64_t revision ) {
        revision = std::min(revision, _revision);
        while( _revision - _undo_stack.size() < revision ) {
           dispose(_undo_stack.front());
           _undo_stack.pop_front();
        }
     }

    private:

      int64_t add_session() {
         _undo_stack.emplace_back();
         _undo_stack.back().old_next_id = _next_id;
         return ++_revision;
      }

      void undo() {
         undo_state& undo_info = _undo_stack.back();
         // erase all new_ids
         undo_info.new_ids.clear_and_dispose([&](id_node* id){
            auto& node_ref = *std::get<0>(_indices).find(id->_item);
            erase_impl(node_ref);
            dispose(node_ref);
            dispose(id);
         });
         // replace old_values - if there is a conflict, erase the conflict
         undo_info.old_values.clear_and_dispose([this](node* p) { insert_or_replace(*p); });
         // insert all removed_values
         undo_info.removed_values.clear_and_dispose([this](node* p) { insert_impl(*p); });
         _next_id = undo_info.old_next_id;
         _undo_stack.pop_back();
         --_revision;
      }
      void squash() {
         if (_undo_stack.empty()) {
            return;
         } else if (_undo_stack.size() == 1) {
            _undo_stack.pop_back();
            return;
         }
         undo_state& last_state = _undo_stack.back();
         undo_state& prev_state = _undo_stack[_undo_stack.size() - 2];
         last_state.new_ids.clear_and_dispose([this, &prev_state](id_node* p) {
            auto iter = prev_state.removed_values.find(p->_item);
            if ( iter != prev_state.removed_values.end() ) {
               auto& node_ref = *iter;
               prev_state.removed_values.erase(iter);
               prev_state.old_values.insert(node_ref);
               dispose(p);
            } else {
               // Not in old_values or new_ids
               prev_state.new_ids.insert(*p);
            }
         });
         last_state.removed_values.clear_and_dispose([this, &prev_state](node* p){
            auto new_iter = prev_state.new_ids.find(p->id);
            if (new_iter != prev_state.new_ids.end()) {
               prev_state.new_ids.erase_and_dispose(new_iter, [this](id_node* id){ dispose(id); });
               dispose(*p);
            } else {
               auto old_iter = prev_state.old_values.find(p->id);
               if (old_iter != prev_state.old_values.end()) {
                  auto& node_ref = *old_iter;
                  prev_state.old_values.erase(old_iter);
                  prev_state.removed_values.insert(node_ref);
                  dispose(*p);
               } else {
                  // Not in removed_values
                  prev_state.removed_values.insert(*p);
               }
            }
         });
         last_state.old_values.clear_and_dispose([this, &prev_state](node* p){
            auto new_iter = prev_state.new_ids.find(p->id);
            if (new_iter != prev_state.new_ids.end()) {
               dispose(*p);
            } else {
               auto old_iter = prev_state.old_values.find(p->id);
               if (old_iter != prev_state.old_values.end()) {
                  dispose(*p);
               } else {
                  // Not in removed_values
                  prev_state.removed_values.insert(*p);
               }
            }
         });
         _undo_stack.pop_back();
         --_revision;
      }
      template<int N = 0>
      bool insert_impl(node& p) {
         if constexpr (N < sizeof...(Keys)) {
            auto [iter, inserted] = std::get<N>(_indices).insert(p);
            if(!inserted) return false;
            auto guard = scope_exit{[this,iter=iter]{ std::get<N>(_indices).erase(iter); }};
            if(insert_impl<N+1>(p)) {
               guard.cancel();
               return true;
            }
            return false;
         }
         return true;
      }

      // inserts a node and removes any existing nodes that conflict with it
      template<int N = 0>
      void insert_or_replace(node& p) {
         if constexpr (N < sizeof...(Keys)) {
            auto [iter, inserted] = std::get<N>(_indices).insert(p);
            if(!inserted) {
               node& conflict = *iter;
               erase_impl(conflict);
               dispose(conflict);
               std::get<N>(_indices).insert(p);
            }
            insert_or_replace<N + 1>(p);
         }
      }

      template<int N = sizeof...(Keys)>
      void erase_impl(node& p) {
         if constexpr (N > 0) {
            auto& setN = std::get<N-1>(_indices);
            setN.erase(setN.iterator_to(p));
            erase_impl<N-1>(p);
         }
      }

      void on_create(const value_type& value) {
         if(!_undo_stack.empty()) {
            auto& undo_info = _undo_stack.back();
            auto pos = undo_info.removed_values.find(value.id);
            if(pos != undo_info.removed_values.end()) {
               auto& elem = *pos;
               undo_info.removed_values.erase(pos);
               undo_info.old_values.insert(elem);
            } else {
               // Not in old_values or new_ids
               id_node* new_id = _new_ids_allocator.allocate(1);
               auto guard0 = scope_exit{[&]{ _new_ids_allocator.deallocate(new_id, 1); }};
               _new_ids_allocator.construct(new_id, value.id);
               guard0.cancel();
               _undo_stack.back().new_ids.insert(*new_id);
            }
         }
      }

      node* on_modify( const value_type& obj) {
         if (!_undo_stack.empty()) {
            auto& undo_info = _undo_stack.back();
            if ( undo_info.new_ids.find( obj.id ) != undo_info.new_ids.end() ) {
               // Nothing to do
            } else if(undo_info.old_values.find( obj.id ) != undo_info.old_values.end() ) {
               // Nothing to do
            } else {
               // Not in removed_values
               node* p = _allocator.allocate(1);
               auto guard0 = scope_exit{[&]{ _allocator.deallocate(p, 1); }};
               _allocator.construct(p, obj);
               guard0.cancel();
               undo_info.old_values.insert(*p);
               return p;
            }
         }
         return nullptr;
      }
      void commit_one() noexcept {
         dispose(_undo_stack.front());
         _undo_stack.pop_front();
      }
      template<int N = 0>
      void clear_impl() noexcept {
         if constexpr(N < sizeof...(Keys)) {
            std::get<N>(_indices).clear();
            clear_impl<N+1>();
         }
      }
      void dispose(node& node_ref) noexcept {
         node* p{&node_ref};
         _allocator.destroy(p);
         _allocator.deallocate(p, 1);
      }
      void dispose(id_node* p) noexcept {
         _new_ids_allocator.destroy(p);
         _new_ids_allocator.deallocate(p, 1);
      }
      void dispose(undo_state& state) noexcept {
         state.new_ids.clear_and_dispose([this](id_node* p){ dispose(p); });
         state.old_values.clear_and_dispose([this](node* p){ dispose(*p); });
         state.old_values.clear_and_dispose([this](node* p){ dispose(*p); });
      }
      // returns true if the node should be destroyed
     bool on_remove( node& obj) {
        if (!_undo_stack.empty()) {
           auto& undo_info = _undo_stack.back();
           auto new_pos = undo_info.new_ids.find( obj.id );
           if ( new_pos != undo_info.new_ids.end() ) {
              id_node* p = &*new_pos;
              undo_info.new_ids.erase( new_pos );
              dispose(p);
              return true;
           }
           auto old_pos = undo_info.old_values.find( obj.id );
           if( old_pos != undo_info.old_values.end() ) {
              auto& node_ref = *old_pos;
              undo_info.old_values.erase(old_pos);
              undo_info.removed_values.insert(node_ref);
              return true;
           } else {
              undo_info.removed_values.insert(obj);
              return false;
           }
        }
        return true;
     }
     indices_type _indices;
     boost::container::deque<undo_state, rebind_alloc_t<Allocator, undo_state>> _undo_stack;
     rebind_alloc_t<Allocator, node> _allocator;
     rebind_alloc_t<Allocator, id_node> _new_ids_allocator;
     id_type _next_id = 0;
     int64_t _revision = 0;
  };

}

#endif
