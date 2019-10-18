#pragma once

#include <boost/interprocess/managed_mapped_file.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/containers/set.hpp>
#include <boost/interprocess/containers/flat_map.hpp>
#include <boost/interprocess/containers/deque.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/allocators/node_allocator.hpp>
#include <boost/interprocess/allocators/private_node_allocator.hpp>
#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/core/demangle.hpp>

#include <boost/multi_index_container.hpp>

#include <boost/chrono.hpp>
#include <boost/config.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>

#include <array>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>

#include <chainbase/pinnable_mapped_file.hpp>
#include <chainbase/undo_index.hpp>

#ifndef CHAINBASE_NUM_RW_LOCKS
   #define CHAINBASE_NUM_RW_LOCKS 10
#endif

#ifdef CHAINBASE_CHECK_LOCKING
   #define CHAINBASE_REQUIRE_READ_LOCK(m, t) require_read_lock(m, typeid(t).name())
   #define CHAINBASE_REQUIRE_WRITE_LOCK(m, t) require_write_lock(m, typeid(t).name())
#else
   #define CHAINBASE_REQUIRE_READ_LOCK(m, t)
   #define CHAINBASE_REQUIRE_WRITE_LOCK(m, t)
#endif

namespace chainbase {

   namespace bip = boost::interprocess;
   namespace bfs = boost::filesystem;
   using std::unique_ptr;
   using std::vector;

   template<typename T, typename S>
   class chainbase_node_allocator {
    public:
      using value_type = T;
      using reference = T&;
      using const_reference = const T&;
      using pointer = bip::offset_ptr<T>;
      using const_pointer = bip::offset_ptr<const T>;
      using void_pointer = bip::offset_ptr<void>;
      using const_void_pointer = bip::offset_ptr<const void>;
      using segment_manager = pinnable_mapped_file::segment_manager;
      using difference_type = std::ptrdiff_t;
      using size_type = std::size_t;
      template<typename U>
      struct rebind { using other = chainbase_node_allocator<U, S>; };
      chainbase_node_allocator(segment_manager* manager) : _manager{manager} {}
      chainbase_node_allocator(const chainbase_node_allocator& other) : _manager(other._manager) {}
      template<typename U>
      chainbase_node_allocator(const chainbase_node_allocator<U, S>& other) : _manager(other._manager) {}
      pointer allocate(std::size_t num) {
         if (num == 1) {
            if (_freelist == nullptr) {
               get_some();
            }
            list_item* result = &*_freelist;
            _freelist = _freelist->_next;
            result->~list_item();
            return pointer{(T*)result};
         } else {
            return pointer{(T*)_manager->allocate(num*sizeof(T))};
         }
      }
      void deallocate(const pointer& p, std::size_t num) {
         if (num == 1) {
            _freelist = new (&*p) list_item{_freelist};
         } else {
            _manager->deallocate(&*p);
         }
      }
      pointer address(reference val) const { return pointer{&val}; }
      const_pointer address(const_reference val) const { return const_pointer{&val}; }
      template<typename... A>
      void construct(const pointer &p, A&&... a) {
         new(&*p) T{a...};
      }
      void destroy(const pointer& p) {
         p->~T();
      }
      bool operator==(const chainbase_node_allocator& other) const { return this == &other; }
      bool operator!=(const chainbase_node_allocator& other) const { return this != &other; }
      segment_manager* get_segment_manager() const { return _manager.get(); }
    private:
      template<typename T2, typename S2>
      friend class chainbase_node_allocator;
      void get_some() {
         char* result = (char*)_manager->allocate(sizeof(T) * 64);
         _freelist = bip::offset_ptr<list_item>{(list_item*)result};
         for(int i = 0; i < 63; ++i) {
            char* next = result + sizeof(T);
            new(result) list_item{bip::offset_ptr<list_item>{(list_item*)next}};
            result = next;
         }
         new(result) list_item{nullptr};
      }
      struct list_item { bip::offset_ptr<list_item> _next; };
      bip::offset_ptr<pinnable_mapped_file::segment_manager> _manager;
      bip::offset_ptr<list_item> _freelist{};
   };

   template<typename T>
   using allocator = bip::allocator<T, pinnable_mapped_file::segment_manager>;

   template<typename T>
   using node_allocator = chainbase_node_allocator<T, pinnable_mapped_file::segment_manager>;
  
   //typedef bip::basic_string< char, std::char_traits< char >, allocator< char > > shared_string;

   template<typename T>
   using shared_vector = std::vector<T, allocator<T> >;

   class shared_cow_string {
      struct impl {
         uint32_t reference_count;
         uint32_t size;
         char data[0];
      };
    public:
      using allocator_type = allocator<char>;
      using iterator = const char*;
      using const_iterator = const char*;
      explicit shared_cow_string(const allocator<char>& alloc) : _data(nullptr), _alloc(alloc) {}
      template<typename Iter>
      explicit shared_cow_string(Iter begin, Iter end, const allocator<char>& alloc) : shared_cow_string(alloc) {
         std::size_t size = std::distance(begin, end);
         impl* new_data = (impl*)&*_alloc.allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         std::copy(begin, end, new_data->data);
         new_data->data[size] = '\0';
         _data = new_data;
      }
      explicit shared_cow_string(const char* ptr, std::size_t size, const allocator<char>& alloc) : shared_cow_string(alloc) {
         impl* new_data = (impl*)&*_alloc.allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         std::memcpy(new_data->data, ptr, size);
         new_data->data[size] = '\0';
         _data = new_data;
      }
      explicit shared_cow_string(std::size_t size, boost::container::default_init_t, const allocator<char>& alloc) : shared_cow_string(alloc) {
         impl* new_data = (impl*)&*_alloc.allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         new_data->data[size] = '\0';
         _data = new_data;
      }
      shared_cow_string(const shared_cow_string& other) : _data(other._data), _alloc(other._alloc) {
         if(_data != nullptr) {
            ++_data->reference_count;
         }
      }
      shared_cow_string(shared_cow_string&& other) : _data(other._data), _alloc(other._alloc) {
         other._data = nullptr;
      }
      shared_cow_string& operator=(const shared_cow_string& other) {
         *this = shared_cow_string{other};
         return *this;
      }
      shared_cow_string& operator=(shared_cow_string&& other) {
         if (this != &other) {
            _data = other._data;
            other._data = nullptr;
         }
         return *this;
      }
      ~shared_cow_string() {
         dec_refcount();
      }
      void resize(std::size_t new_size) {
         impl* new_data = (impl*)&*_alloc.allocate(sizeof(impl) + new_size + 1);
         new_data->reference_count = 1;
         new_data->size = new_size;
         std::size_t size = this->size();
         if (new_size < size) {
            std::memcpy(new_data->data, _data->data, new_size);
         } else {
            if(size != 0) {
               std::memcpy(new_data->data, _data->data, size);
            }
            std::memset(new_data->data + size, 0, new_size - size);
         }
         new_data->data[new_size] = '\0';
         dec_refcount();
         _data = new_data;
      }
      void assign(const char* ptr, std::size_t size) {
         impl* new_data = (impl*)&*_alloc.allocate(sizeof(impl) + size + 1);
         new_data->reference_count = 1;
         new_data->size = size;
         if(size)
            std::memcpy(new_data->data, ptr, size);
         new_data->data[size] = '\0';
         if (_data) {
            dec_refcount();
         }
         _data = new_data;
      }
      void assign(const unsigned char* ptr, std::size_t size) {
         assign((char*)ptr, size);
      }
      const char * c_str() const {
         if (_data) return _data->data;
         else return nullptr;
      }
      const char * data() const { return c_str(); }
      std::size_t size() const {
         if (_data) return _data->size;
         else return 0;
      }
      const_iterator begin() const { return c_str(); }
      const_iterator end() const {
         if (_data) return _data->data + _data->size;
         else return nullptr;
      }
      int compare(std::size_t start, std::size_t count, const char* other, std::size_t other_size) const {
         std::size_t cmp_len = std::min(count, other_size);
         const char* start_ptr = data() + start;
         int result = std::char_traits<char>::compare(start_ptr, other, cmp_len);
         if (result != 0) return result;
         else if (count < other_size) return -1;
         else if(count > other_size) return 1;
         else return 0;
      }
      bool operator==(const shared_cow_string& rhs) const {
        return size() == rhs.size() && std::memcmp(data(), rhs.data(), size()) == 0;
      }
      bool operator!=(const shared_cow_string& rhs) const { return !(*this == rhs); }
      const allocator_type& get_allocator() const { return _alloc; }
    private:
      void dec_refcount() {
         if(_data && --_data->reference_count == 0) {
            _alloc.deallocate((char*)&*_data, sizeof(shared_cow_string) + _data->size + 1);
         }
      }
      bip::offset_ptr<impl> _data;
      allocator< char > _alloc;
   };
   using shared_string = shared_cow_string;

   typedef boost::interprocess::interprocess_sharable_mutex read_write_mutex;
   typedef boost::interprocess::sharable_lock< read_write_mutex > read_lock;

   /**
    *  Object ID type that includes the type of the object it references
    */
   template<typename T>
   class oid {
      public:
         oid( int64_t i = 0 ):_id(i){}

         oid& operator++() { ++_id; return *this; }

         friend bool operator < ( const oid& a, const oid& b ) { return a._id < b._id; }
         friend bool operator > ( const oid& a, const oid& b ) { return a._id > b._id; }
         friend bool operator <= ( const oid& a, const oid& b ) { return a._id <= b._id; }
         friend bool operator >= ( const oid& a, const oid& b ) { return a._id >= b._id; }
         friend bool operator == ( const oid& a, const oid& b ) { return a._id == b._id; }
         friend bool operator != ( const oid& a, const oid& b ) { return a._id != b._id; }
         friend std::ostream& operator<<(std::ostream& s, const oid& id) {
            s << boost::core::demangle(typeid(oid<T>).name()) << '(' << id._id << ')'; return s;
         }

         int64_t _id = 0;
   };

   template<uint16_t TypeNumber, typename Derived>
   struct object
   {
      typedef oid<Derived> id_type;
      static const uint16_t type_id = TypeNumber;
   };

   /** this class is ment to be specified to enable lookup of index type by object type using
    * the SET_INDEX_TYPE macro.
    **/
   template<typename T>
   struct get_index_type {};

   /**
    *  This macro must be used at global scope and OBJECT_TYPE and INDEX_TYPE must be fully qualified
    */
   #define CHAINBASE_SET_INDEX_TYPE( OBJECT_TYPE, INDEX_TYPE )  \
   namespace chainbase { template<> struct get_index_type<OBJECT_TYPE> { typedef INDEX_TYPE type; }; }

   #define CHAINBASE_DEFAULT_CONSTRUCTOR( OBJECT_TYPE ) \
   template<typename Constructor, typename Allocator> \
   OBJECT_TYPE( Constructor&& c, Allocator&&  ) { c(*this); }

   template< typename value_type >
   class undo_state
   {
      public:
         typedef typename value_type::id_type                      id_type;
         typedef allocator< std::pair<const id_type, value_type> > id_value_allocator_type;
         typedef allocator< id_type >                              id_allocator_type;

         template<typename T>
         undo_state( allocator<T> al )
         :old_values( id_value_allocator_type( al.get_segment_manager() ) ),
          removed_values( id_value_allocator_type( al.get_segment_manager() ) ),
          new_ids( id_allocator_type( al.get_segment_manager() ) ){}

         typedef boost::interprocess::map< id_type, value_type, std::less<id_type>, id_value_allocator_type >  id_value_type_map;
         typedef boost::interprocess::set< id_type, std::less<id_type>, id_allocator_type >                    id_type_set;

         id_value_type_map            old_values;
         id_value_type_map            removed_values;
         id_type_set                  new_ids;
         id_type                      old_next_id = 0;
         int64_t                      revision = 0;
   };

   /**
    * The code we want to implement is this:
    *
    * ++target; try { ... } finally { --target }
    *
    * In C++ the only way to implement finally is to create a class
    * with a destructor, so that's what we do here.
    */
   class int_incrementer
   {
      public:
         int_incrementer( int32_t& target ) : _target(target)
         { ++_target; }
         ~int_incrementer()
         { --_target; }

         int32_t get()const
         { return _target; }

      private:
         int32_t& _target;
   };

   template<typename MultiIndexType>
   using generic_index = multi_index_to_undo_index<MultiIndexType>;

   class abstract_session {
      public:
         virtual ~abstract_session(){};
         virtual void push()             = 0;
         virtual void squash()           = 0;
         virtual void undo()             = 0;
   };

   template<typename SessionType>
   class session_impl : public abstract_session
   {
      public:
         session_impl( SessionType&& s ):_session( std::move( s ) ){}

         virtual void push() override  { _session.push();  }
         virtual void squash() override{ _session.squash(); }
         virtual void undo() override  { _session.undo();  }
      private:
         SessionType _session;
   };

   class abstract_index
   {
      public:
         abstract_index( void* i ):_idx_ptr(i){}
         virtual ~abstract_index(){}
         virtual void     set_revision( uint64_t revision ) = 0;
         virtual unique_ptr<abstract_session> start_undo_session( bool enabled ) = 0;

         virtual int64_t revision()const = 0;
         virtual void    undo()const = 0;
         virtual void    squash()const = 0;
         virtual void    commit( int64_t revision )const = 0;
         virtual void    undo_all()const = 0;
         virtual uint32_t type_id()const  = 0;
         virtual uint64_t row_count()const = 0;
         virtual const std::string& type_name()const = 0;
         virtual std::pair<int64_t, int64_t> undo_stack_revision_range()const = 0;

         virtual void remove_object( int64_t id ) = 0;

         void* get()const { return _idx_ptr; }
      private:
         void* _idx_ptr;
   };

   template<typename BaseIndex>
   class index_impl : public abstract_index {
      public:
         index_impl( BaseIndex& base ):abstract_index( &base ),_base(base){}

         virtual unique_ptr<abstract_session> start_undo_session( bool enabled ) override {
            return unique_ptr<abstract_session>(new session_impl<typename BaseIndex::session>( _base.start_undo_session( enabled ) ) );
         }

         virtual void     set_revision( uint64_t revision ) override { _base.set_revision( revision ); }
         virtual int64_t  revision()const  override { return _base.revision(); }
         virtual void     undo()const  override { _base.undo(); }
         virtual void     squash()const  override { _base.squash(); }
         virtual void     commit( int64_t revision )const  override { _base.commit(revision); }
         virtual void     undo_all() const override {_base.undo_all(); }
         virtual uint32_t type_id()const override { return BaseIndex::value_type::type_id; }
         virtual uint64_t row_count()const override { return _base.indices().size(); }
         virtual const std::string& type_name() const override { return BaseIndex_name; }
         virtual std::pair<int64_t, int64_t> undo_stack_revision_range()const override { return _base.undo_stack_revision_range(); }

         virtual void     remove_object( int64_t id ) override { return _base.remove_object( id ); }
      private:
         BaseIndex& _base;
         std::string BaseIndex_name = boost::core::demangle( typeid( typename BaseIndex::value_type ).name() );
   };

   template<typename IndexType>
   class index : public index_impl<IndexType> {
      public:
         index( IndexType& i ):index_impl<IndexType>( i ){}
   };


   class read_write_mutex_manager
   {
      public:
         read_write_mutex_manager()
         {
            _current_lock = 0;
         }

         ~read_write_mutex_manager(){}

         void next_lock()
         {
            _current_lock++;
            new( &_locks[ _current_lock % CHAINBASE_NUM_RW_LOCKS ] ) read_write_mutex();
         }

         read_write_mutex& current_lock()
         {
            return _locks[ _current_lock % CHAINBASE_NUM_RW_LOCKS ];
         }

         uint32_t current_lock_num()
         {
            return _current_lock;
         }

      private:
         std::array< read_write_mutex, CHAINBASE_NUM_RW_LOCKS >     _locks;
         std::atomic< uint32_t >                                    _current_lock;
   };


   /**
    *  This class
    */
   class database
   {
      public:
         enum open_flags {
            read_only     = 0,
            read_write    = 1
         };

         using database_index_row_count_multiset = std::multiset<std::pair<unsigned, std::string>>;

         database(const bfs::path& dir, open_flags write = read_only, uint64_t shared_file_size = 0, bool allow_dirty = false,
                  pinnable_mapped_file::map_mode = pinnable_mapped_file::map_mode::mapped,
                  std::vector<std::string> hugepage_paths = std::vector<std::string>());
         ~database();
         database(database&&) = default;
         database& operator=(database&&) = default;
         bool is_read_only() const { return _read_only; }
         void flush();
         void set_require_locking( bool enable_require_locking );

#ifdef CHAINBASE_CHECK_LOCKING
         void require_lock_fail( const char* method, const char* lock_type, const char* tname )const;

         void require_read_lock( const char* method, const char* tname )const
         {
            if( BOOST_UNLIKELY( _enable_require_locking & _read_only & (_read_lock_count <= 0) ) )
               require_lock_fail(method, "read", tname);
         }

         void require_write_lock( const char* method, const char* tname )
         {
            if( BOOST_UNLIKELY( _enable_require_locking & (_write_lock_count <= 0) ) )
               require_lock_fail(method, "write", tname);
         }
#endif

         struct session {
            public:
               session( session&& s ):_index_sessions( std::move(s._index_sessions) ){}
               session( vector<std::unique_ptr<abstract_session>>&& s ):_index_sessions( std::move(s) )
               {
               }

               ~session() {
                  undo();
               }

               void push()
               {
                  for( auto& i : _index_sessions ) i->push();
                  _index_sessions.clear();
               }

               void squash()
               {
                  for( auto& i : _index_sessions ) i->squash();
                  _index_sessions.clear();
               }

               void undo()
               {
                  for( auto& i : _index_sessions ) i->undo();
                  _index_sessions.clear();
               }

            private:
               friend class database;
               session(){}

               vector< std::unique_ptr<abstract_session> > _index_sessions;
         };

         session start_undo_session( bool enabled );

         int64_t revision()const {
             if( _index_list.size() == 0 ) return -1;
             return _index_list[0]->revision();
         }

         void undo();
         void squash();
         void commit( int64_t revision );
         void undo_all();


         void set_revision( uint64_t revision )
         {
             CHAINBASE_REQUIRE_WRITE_LOCK( "set_revision", uint64_t );
             for( auto i : _index_list ) i->set_revision( revision );
         }


         template<typename MultiIndexType>
         void add_index() {
            const uint16_t type_id = generic_index<MultiIndexType>::value_type::type_id;
            typedef generic_index<MultiIndexType>          index_type;
            typedef typename index_type::allocator_type    index_alloc;

            std::string type_name = boost::core::demangle( typeid( typename index_type::value_type ).name() );

            if( !( _index_map.size() <= type_id || _index_map[ type_id ] == nullptr ) ) {
               BOOST_THROW_EXCEPTION( std::logic_error( type_name + "::type_id is already in use" ) );
            }

            index_type* idx_ptr = nullptr;
            if( _read_only )
               idx_ptr = _db_file.get_segment_manager()->find_no_lock< index_type >( type_name.c_str() ).first;
            else
               idx_ptr = _db_file.get_segment_manager()->find< index_type >( type_name.c_str() ).first;
            bool first_time_adding = false;
            if( !idx_ptr ) {
               if( _read_only ) {
                  BOOST_THROW_EXCEPTION( std::runtime_error( "unable to find index for " + type_name + " in read only database" ) );
               }
               first_time_adding = true;
               idx_ptr = _db_file.get_segment_manager()->construct< index_type >( type_name.c_str() )( index_alloc( _db_file.get_segment_manager() ) );
             }

            idx_ptr->validate();

            // Ensure the undo stack of added index is consistent with the other indices in the database
            if( _index_list.size() > 0 ) {
               auto expected_revision_range = _index_list.front()->undo_stack_revision_range();
               auto added_index_revision_range = idx_ptr->undo_stack_revision_range();

               if( added_index_revision_range.first != expected_revision_range.first ||
                   added_index_revision_range.second != expected_revision_range.second ) {

                  if( !first_time_adding ) {
                     BOOST_THROW_EXCEPTION( std::logic_error(
                        "existing index for " + type_name + " has an undo stack (revision range [" +
                        std::to_string(added_index_revision_range.first) + ", " + std::to_string(added_index_revision_range.second) +
                        "]) that is inconsistent with other indices in the database (revision range [" +
                        std::to_string(expected_revision_range.first) + ", " + std::to_string(expected_revision_range.second) +
                        "]); corrupted database?"
                     ) );
                  }

                  if( _read_only ) {
                     BOOST_THROW_EXCEPTION( std::logic_error(
                        "new index for " + type_name +
                        " requires an undo stack that is consistent with other indices in the database; cannot fix in read-only mode"
                     ) );
                  }

                  idx_ptr->set_revision( static_cast<uint64_t>(expected_revision_range.first) );
                  while( idx_ptr->revision() < expected_revision_range.second ) {
                     idx_ptr->start_undo_session(true).push();
                  }
               }
            }

            if( type_id >= _index_map.size() )
               _index_map.resize( type_id + 1 );

            auto new_index = new index<index_type>( *idx_ptr );
            _index_map[ type_id ].reset( new_index );
            _index_list.push_back( new_index );
         }

         auto get_segment_manager() -> decltype( ((pinnable_mapped_file*)nullptr)->get_segment_manager()) {
            return _db_file.get_segment_manager();
         }

         auto get_segment_manager()const -> std::add_const_t< decltype( ((pinnable_mapped_file*)nullptr)->get_segment_manager() ) > {
            return _db_file.get_segment_manager();
         }

         size_t get_free_memory()const
         {
            return _db_file.get_segment_manager()->get_free_memory();
         }

         template<typename MultiIndexType>
         const generic_index<MultiIndexType>& get_index()const
         {
            CHAINBASE_REQUIRE_READ_LOCK("get_index", typename MultiIndexType::value_type);
            typedef generic_index<MultiIndexType> index_type;
            typedef index_type*                   index_type_ptr;
            assert( _index_map.size() > index_type::value_type::type_id );
            assert( _index_map[index_type::value_type::type_id] );
            return *index_type_ptr( _index_map[index_type::value_type::type_id]->get() );
         }

         template<typename MultiIndexType, typename ByIndex>
         auto get_index()const -> decltype( ((generic_index<MultiIndexType>*)( nullptr ))->indices().template get<ByIndex>() )
         {
            CHAINBASE_REQUIRE_READ_LOCK("get_index", typename MultiIndexType::value_type);
            typedef generic_index<MultiIndexType> index_type;
            typedef index_type*                   index_type_ptr;
            assert( _index_map.size() > index_type::value_type::type_id );
            assert( _index_map[index_type::value_type::type_id] );
            return index_type_ptr( _index_map[index_type::value_type::type_id]->get() )->indices().template get<ByIndex>();
         }

         template<typename MultiIndexType>
         generic_index<MultiIndexType>& get_mutable_index()
         {
            CHAINBASE_REQUIRE_WRITE_LOCK("get_mutable_index", typename MultiIndexType::value_type);
            typedef generic_index<MultiIndexType> index_type;
            typedef index_type*                   index_type_ptr;
            assert( _index_map.size() > index_type::value_type::type_id );
            assert( _index_map[index_type::value_type::type_id] );
            return *index_type_ptr( _index_map[index_type::value_type::type_id]->get() );
         }

         template< typename ObjectType, typename IndexedByType, typename CompatibleKey >
         const ObjectType* find( CompatibleKey&& key )const
         {
             CHAINBASE_REQUIRE_READ_LOCK("find", ObjectType);
             typedef typename get_index_type< ObjectType >::type index_type;
             const auto& idx = get_index< index_type >().indices().template get< IndexedByType >();
             auto itr = idx.find( std::forward< CompatibleKey >( key ) );
             if( itr == idx.end() ) return nullptr;
             return &*itr;
         }

         template< typename ObjectType >
         const ObjectType* find( oid< ObjectType > key = oid< ObjectType >() ) const
         {
             CHAINBASE_REQUIRE_READ_LOCK("find", ObjectType);
             typedef typename get_index_type< ObjectType >::type index_type;
             return get_index< index_type >().find( key );
         }

         template< typename ObjectType, typename IndexedByType, typename CompatibleKey >
         const ObjectType& get( CompatibleKey&& key )const
         {
             CHAINBASE_REQUIRE_READ_LOCK("get", ObjectType);
             auto obj = find< ObjectType, IndexedByType >( std::forward< CompatibleKey >( key ) );
             if( !obj ) {
                std::stringstream ss;
                ss << "unknown key (" << boost::core::demangle( typeid( key ).name() ) << "): " << key;
                BOOST_THROW_EXCEPTION( std::out_of_range( ss.str().c_str() ) );
             }
             return *obj;
         }

         template< typename ObjectType >
         const ObjectType& get( const oid< ObjectType >& key = oid< ObjectType >() )const
         {
             CHAINBASE_REQUIRE_READ_LOCK("get", ObjectType);
             auto obj = find< ObjectType >( key );
             if( !obj ) {
                std::stringstream ss;
                ss << "unknown key (" << boost::core::demangle( typeid( key ).name() ) << "): " << key._id;
                BOOST_THROW_EXCEPTION( std::out_of_range( ss.str().c_str() ) );
             }
             return *obj;
         }

         template<typename ObjectType, typename Modifier>
         void modify( const ObjectType& obj, Modifier&& m )
         {
             CHAINBASE_REQUIRE_WRITE_LOCK("modify", ObjectType);
             typedef typename get_index_type<ObjectType>::type index_type;
             get_mutable_index<index_type>().modify( obj, m );
         }

         template<typename ObjectType>
         void remove( const ObjectType& obj )
         {
             CHAINBASE_REQUIRE_WRITE_LOCK("remove", ObjectType);
             typedef typename get_index_type<ObjectType>::type index_type;
             return get_mutable_index<index_type>().remove( obj );
         }

         template<typename ObjectType, typename Constructor>
         const ObjectType& create( Constructor&& con )
         {
             CHAINBASE_REQUIRE_WRITE_LOCK("create", ObjectType);
             typedef typename get_index_type<ObjectType>::type index_type;
             return get_mutable_index<index_type>().emplace( std::forward<Constructor>(con) );
         }

         database_index_row_count_multiset row_count_per_index()const {
            database_index_row_count_multiset ret;
            for(const auto& ai_ptr : _index_map) {
               if(!ai_ptr)
                  continue;
               ret.emplace(make_pair(ai_ptr->row_count(), ai_ptr->type_name()));
            }
            return ret;
         }

      private:
         pinnable_mapped_file                                        _db_file;
         bool                                                        _read_only = false;

         /**
          * This is a sparse list of known indices kept to accelerate creation of undo sessions
          */
         vector<abstract_index*>                                     _index_list;

         /**
          * This is a full map (size 2^16) of all possible index designed for constant time lookup
          */
         vector<unique_ptr<abstract_index>>                          _index_map;

#ifdef CHAINBASE_CHECK_LOCKING
         int32_t                                                     _read_lock_count = 0;
         int32_t                                                     _write_lock_count = 0;
         bool                                                        _enable_require_locking = false;
#endif
   };

   template<typename Object, typename... Args>
   using shared_multi_index_container = boost::multi_index_container<Object,Args..., chainbase::node_allocator<Object> >;
}  // namepsace chainbase
