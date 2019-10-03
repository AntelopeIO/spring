#ifndef EOSIO_CHAINBASE_UNDO_INDEX_HPP_INCLUDED
#define EOSIO_CHAINBASE_UNDO_INDEX_HPP_INCLUDED

#include <boost/multi_index_container_fwd.hpp>
#include <boost/intrusive/set.hpp>
#include <boost/container/deque.hpp>
#include <boost/throw_exception.hpp>
#include <boost/mpl/copy.hpp>
#include <boost/mpl/back_inserter.hpp>
#include <boost/mp11/mpl.hpp>
#include <boost/mp11/list.hpp>
#include <boost/mp11/algorithm.hpp>
#include <boost/iterator/transform_iterator.hpp>
#include <memory>
#include <type_traits>
#include <iostream>

namespace chainbase {

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
      using type = std::decay_t<decltype(Key{}(std::declval<const T&>()))>;
      decltype(auto) operator()(const T& arg) const { return Key{}(arg); }
   };

   struct identity_key {
      template<typename T>
      const auto& operator()(const T& t) { return t; }
   };

   template<typename Allocator, typename T>
   using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;

   template<typename Index>
   struct index_tag_impl { using type = void; };
   template<template<typename...> class Index, typename Tag, typename... T>
   struct index_tag_impl<Index<boost::multi_index::tag<Tag>, T...>> { using type = Tag; };
   template<typename Index>
   using index_tag = typename index_tag_impl<Index>::type;

   struct identity_index {
      using key_from_value_type = identity_key;
      using compare_type = std::less<>;
   };

   template<typename Index>
   using index_key = typename Index::key_from_value_type;
   template<typename Index>
   using index_compare = typename Index::compare_type;

   template<typename Tag, typename... Keys>
   using index_of_tag = boost::mp11::mp_find<boost::mp11::mp_list<index_tag<Keys>...>, Tag>;

   template<typename R>
   struct cast_f {
      typedef R result_type;
      template<typename T>
      R operator()(T&& t) const { return static_cast<R>(static_cast<T&&>(t)); }
   };

   template<typename K, typename Allocator>
   using hook = 
      boost::intrusive::set_base_hook<
         boost::intrusive::tag<K>,
         boost::intrusive::void_pointer<typename std::allocator_traits<Allocator>::void_pointer>,
         boost::intrusive::link_mode<boost::intrusive::normal_link>,
         boost::intrusive::constant_time_size<true>>;

   template<typename Alloc, typename T>
   using ptr_t = typename rebind_alloc_t<Alloc, T>::pointer;
   template<typename Alloc, typename T>
   using cptr_t = typename rebind_alloc_t<Alloc, T>::const_pointer;

   template<typename Node, typename Key, typename A>
   struct hook_f {
      using hook_type = hook<Key, A>;
      using hook_ptr = ptr_t<A, hook_type>;
      using const_hook_ptr = cptr_t<A, hook_type>;
      using value_type = typename Node::value_type;
      using pointer = ptr_t<A, value_type>;
      using const_pointer = cptr_t<A, value_type>;

      static hook_ptr to_hook_ptr(value_type &value) { return hook_ptr{static_cast<Node*>(&value)}; }
      static const_hook_ptr to_hook_ptr(const value_type &value) { return hook_ptr{static_cast<const Node*>(&value)}; }
      static pointer to_value_ptr(const hook_ptr& n) { return pointer{static_cast<Node*>(&*n)}; }
      static const_pointer to_value_ptr(const const_hook_ptr& n) { return pointer{static_cast<const Node*>(&*n)}; }
   };

   template<typename Node, typename Key>
   using set_base = boost::intrusive::set<
      typename Node::value_type,
      boost::intrusive::function_hook<hook_f<Node, Key, typename Node::allocator_type>>,
      boost::intrusive::key_of_value<get_key<index_key<Key>, typename Node::value_type>>,
      boost::intrusive::compare<index_compare<Key>>>;
  
   template<typename Node, typename Key>
   struct set_impl : set_base<Node, Key> {
     using base_type = set_base<Node, Key>;
      template<typename K>
      auto find(K&& k) {
         return base_type::find(static_cast<K&&>(k), this->key_comp());
      }
      template<typename K>
      auto find(K&& k) const {
         return base_type::find(static_cast<K&&>(k), this->key_comp());
      }
      template<typename K>
      auto lower_bound(K&& k) const {
         return base_type::lower_bound(static_cast<K&&>(k), this->key_comp());
      }
      template<typename K>
      auto upper_bound(K&& k) const {
         return base_type::upper_bound(static_cast<K&&>(k), this->key_comp());
      }
      template<typename K>
      auto equal_range(K&& k) const {
         return base_type::equal_range(static_cast<K&&>(k), this->key_comp());
      }
   };

   template<typename T, typename Allocator, typename... Keys>
   class undo_index {

    public:
      using id_type = std::decay_t<decltype(std::declval<T>().id)>;
      using value_type = T;
      using allocator_type = Allocator;

      undo_index() = default;
      explicit undo_index(const Allocator& a) : _undo_stack{a}, _allocator{a}, _new_ids_allocator{a} {}
      ~undo_index() {
         for(undo_state& state : _undo_stack) {
            dispose(state);
         }
         clear_impl<1>();
         std::get<0>(_indices).clear_and_dispose([&](pointer p){ dispose(*p); });
      }

      void validate() const {}
    
      struct node : hook<Keys, Allocator>..., T {
         using value_type = T;
         using allocator_type = Allocator;
         template<typename... A>
         explicit node(A&&... a) : T{a...} {}
         const T& item() const { return *this; }
      };

      using indices_type = std::tuple<set_impl<node, Keys>...>;

      using index0_type = std::tuple_element_t<0, indices_type>;

      struct id_node : hook<identity_index, Allocator>, id_type  {
         using value_type = id_type;
         using allocator_type = Allocator;
         id_node(const id_type& id) : id_type(id) {}
      };

      using id_pointer = typename rebind_alloc_t<Allocator, id_type>::pointer;
      using pointer = typename rebind_alloc_t<Allocator, value_type>::pointer;
      using const_iterator = boost::iterators::transform_iterator<cast_f<const T&>, typename index0_type::const_iterator>;

      struct undo_state {
         std::tuple_element_t<0, indices_type> old_values;
         std::tuple_element_t<0, indices_type> removed_values;
         set_impl<id_node, identity_index> new_ids;
         id_type old_next_id = 0;
      };

      template<typename Constructor>
      const value_type& emplace( Constructor&& c ) {
         auto p = _allocator.allocate(1);
         auto guard0 = scope_exit{[&]{ _allocator.deallocate(p, 1); }};
         auto new_id = _next_id;
         auto constructor = [&]( value_type& v ) {
            v.id = new_id;
            c( v );
         };
         // _allocator.construct(p, constructor, _allocator);
         new (&*p) node(constructor, _allocator);
         auto guard1 = scope_exit{[&]{ _allocator.destroy(p); }};
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

      template<typename CompatibleKey>
      const value_type& get( CompatibleKey&& key )const {
         auto ptr = find( key );
         if( !ptr ) {
            std::stringstream ss;
            ss << "key not found (" << boost::core::demangle( typeid( key ).name() ) << "): " << key;
            BOOST_THROW_EXCEPTION( std::out_of_range( ss.str().c_str() ) );
         }
         return *ptr;
      }

      void remove_object( int64_t id ) {
         const value_type* val = find( typename value_type::id_type(id) );
         if( !val ) BOOST_THROW_EXCEPTION( std::out_of_range( boost::lexical_cast<std::string>(id) ) );
         remove( *val );
      }

      class session {
       public:
         session(undo_index& idx, bool enabled)
          : _index(idx),
            _apply(enabled),
            _revision(enabled?idx.add_session():-1) {}
         session(session&& other)
           : _index(other._index),
             _apply(other._apply)
         {
            other._apply = false;
         }
         session& operator=(session&& other) {
            if(this != &other) {
               undo();
               _apply = other._apply;
               other._apply = false;
            }
            return *this;
         }
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
         int64_t revision() const {
            // It looks like chainbase doesn't implement this correctly.  We hope it isn't actually used.
            // BOOST_THROW_EXCEPTION(std::logic_error{"session::revision is unsupported"});
           return _revision;
         }
       private:
         undo_index& _index;
         bool _apply = true;
         int64_t _revision = 0;
      };

      int64_t revision() const { return _revision; }

      session start_undo_session( bool enabled ) {
         dump_info("start_undo_session");
         return session{*this, enabled};
      }

      void set_revision( uint64_t revision ) {
         dump_info("set_revision");
         if( _undo_stack.size() != 0 )
            BOOST_THROW_EXCEPTION( std::logic_error("cannot set revision while there is an existing undo stack") );

         if( revision > std::numeric_limits<int64_t>::max() )
            BOOST_THROW_EXCEPTION( std::logic_error("revision to set is too high") );

         _revision = static_cast<int64_t>(revision);
      }

      std::pair<int64_t, int64_t> undo_stack_revision_range() const {
         dump_info("revision_range");
         return { _revision - _undo_stack.size(), _revision };
      }

      /**
       * Discards all undo history prior to revision
       */
      void commit( int64_t revision ) {
         dump_info("commit");
         revision = std::min(revision, _revision);
         while( _revision - _undo_stack.size() < revision ) {
            dispose(_undo_stack.front());
            _undo_stack.pop_front();
         }
      }

      const undo_index& indices() const { return *this; }
      template<typename Tag>
      const auto& get() const { return std::get<index_of_tag<Tag, Keys...>::value>(_indices); }

      template<int N>
      const auto& get() const { return std::get<N>(_indices); }

      std::size_t size() const {
         return std::get<0>(_indices).size();
      }

      bool empty() const {
         return std::get<0>(_indices).empty();
      }

      template<typename Tag, typename Iter>
      auto project(Iter iter) const {
         return get<Tag>().iterator_to(static_cast<const node&>(*iter));
      }

      const auto& stack() const { return _undo_stack; }

      auto begin() const { return const_iterator(get<0>().begin(), cast_f<const T&>{}); }
      auto end() const { return const_iterator(get<0>().end(), cast_f<const T&>{}); }

      void undo_all() {
         while(!_undo_stack.empty()) {
            undo();
         }
      }

      void undo() {
         dump_info("undo");
         if (_undo_stack.empty()) return;
         undo_state& undo_info = _undo_stack.back();
         // erase all new_ids
         undo_info.new_ids.clear_and_dispose([&](id_pointer id){
            auto& node_ref = *std::get<0>(_indices).find(*id);
            erase_impl(node_ref);
            dispose(node_ref);
            dispose(&*id);
         });
         // replace old_values - if there is a conflict, erase the conflict
         undo_info.old_values.clear_and_dispose([this](pointer p) { insert_or_replace(*p); });
         // insert all removed_values
         undo_info.removed_values.clear_and_dispose([this](pointer p) { insert_impl(*p); });
         _next_id = undo_info.old_next_id;
         _undo_stack.pop_back();
         --_revision;
      }
      void squash() {
         dump_info("squash");
         if (_undo_stack.empty()) {
            return;
         } else if (_undo_stack.size() == 1) {
            --_revision;
            _undo_stack.pop_back();
            return;
         }
         undo_state& last_state = _undo_stack.back();
         undo_state& prev_state = _undo_stack[_undo_stack.size() - 2];
         last_state.new_ids.clear_and_dispose([this, &prev_state](id_pointer p) {
            auto iter = prev_state.removed_values.find(*p);
            if ( iter != prev_state.removed_values.end() ) {
               auto& node_ref = *iter;
               prev_state.removed_values.erase(iter);
               prev_state.old_values.insert(node_ref);
               dispose(&*p);
            } else {
               // Not in old_values or new_ids
               prev_state.new_ids.insert(*p);
            }
         });
         last_state.removed_values.clear_and_dispose([this, &prev_state](pointer p){
            auto new_iter = prev_state.new_ids.find(p->id);
            if (new_iter != prev_state.new_ids.end()) {
               prev_state.new_ids.erase_and_dispose(new_iter, [this](id_pointer id){ dispose(&*id); });
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
         last_state.old_values.clear_and_dispose([this, &prev_state](pointer p){
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

    private:

      void dump_info(const char* fn) const {
        std::cout << fn << ": " << "revision=" << _revision
                  << ", undo_stack_size=" << _undo_stack.size()
                  << ", size=" << size()
                  << ", next_id=" << _next_id << std::endl; 
      }

      int64_t add_session() {
         _undo_stack.emplace_back();
         _undo_stack.back().old_next_id = _next_id;
         return ++_revision;
      }

      template<int N = 0>
      bool insert_impl(value_type& p) {
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
      void insert_or_replace(value_type& p) {
         if constexpr (N < sizeof...(Keys)) {
            auto [iter, inserted] = std::get<N>(_indices).insert(p);
            if(!inserted) {
               value_type& conflict = *iter;
               erase_impl(conflict);
               dispose(conflict);
               std::get<N>(_indices).insert(p);
            }
            insert_or_replace<N + 1>(p);
         }
      }

      template<int N = sizeof...(Keys)>
      void erase_impl(value_type& p) {
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
               auto new_id = _new_ids_allocator.allocate(1);
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
               auto p = _allocator.allocate(1);
               auto guard0 = scope_exit{[&]{ _allocator.deallocate(p, 1); }};
               _allocator.construct(p, obj);
               guard0.cancel();
               undo_info.old_values.insert(*p);
               return &*p;
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
      void dispose(value_type& node_ref) noexcept {
         dispose(static_cast<node&>(node_ref));
      }
      void dispose(id_node* p) noexcept {
         _new_ids_allocator.destroy(p);
         _new_ids_allocator.deallocate(p, 1);
      }
      void dispose(id_type* p) noexcept {
         dispose(static_cast<id_node*>(p));
      }
      void dispose(undo_state& state) noexcept {
         state.new_ids.clear_and_dispose([this](id_pointer p){ dispose(&*p); });
         state.old_values.clear_and_dispose([this](pointer p){ dispose(*p); });
         state.old_values.clear_and_dispose([this](pointer p){ dispose(*p); });
      }
      // returns true if the node should be destroyed
      bool on_remove( node& obj) {
         if (!_undo_stack.empty()) {
            auto& undo_info = _undo_stack.back();
            auto new_pos = undo_info.new_ids.find( obj.id );
            if ( new_pos != undo_info.new_ids.end() ) {
               id_type* p = &*new_pos;
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
      using alloc_traits = std::allocator_traits<rebind_alloc_t<Allocator, node>>;
      indices_type _indices;
      boost::container::deque<undo_state, rebind_alloc_t<Allocator, undo_state>> _undo_stack;
      rebind_alloc_t<Allocator, node> _allocator;
      rebind_alloc_t<Allocator, id_node> _new_ids_allocator;
      id_type _next_id = 0;
      int64_t _revision = 0;
   };

   template<typename MultiIndexContainer>
   struct multi_index_to_undo_index_impl;

   template<typename T, typename I, typename A>
   struct mi_to_ui_ii;
   template<typename T, typename... I, typename A>
   struct mi_to_ui_ii<T, boost::mp11::mp_list<I...>, A> {
      using type = undo_index<T, A, I...>;
   };

   template<typename T, typename I, typename A>
   struct multi_index_to_undo_index_impl<boost::multi_index_container<T, I, A>> {
      using as_mp11 = typename boost::mpl::copy<I, boost::mpl::back_inserter<boost::mp11::mp_list<>>>::type;
      using type = typename mi_to_ui_ii<T, as_mp11, A>::type;
   };

   template<typename MultiIndexContainer>
   using multi_index_to_undo_index = typename multi_index_to_undo_index_impl<MultiIndexContainer>::type;
}

#endif
