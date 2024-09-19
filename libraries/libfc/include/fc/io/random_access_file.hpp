#pragma once
#include <fc/filesystem.hpp>
#include <fc/io/datastream.hpp>
#include <fc/io/raw.hpp>

#include <boost/beast.hpp>

#include <boost/iostreams/categories.hpp>
#include <boost/iostreams/pipeline.hpp>
#include <boost/iostreams/positioning.hpp>
#include <boost/interprocess/mapped_region.hpp>

#ifdef _WIN32
#include <boost/asio/random_access_file.hpp>
#endif

/*
random_access_file is a thread-safe and mutex-free interface for reading and writing to a file. Though, reading from a span another thread
 is simultaneously writing to will give undefined results; likewise with multiple threads writing to the same span. In other words, simultaneous
 reads, writes, and resizes are not atomic with one another.

Construction of a random_access_file always creates the file if set to read_write mode.

With a single random_access_file instance, calling unpack_from() and pack_to() from multiple threads simultaneously is allowed. Upon return
 of pack_to() contents will have been flushed.
pack_to_end() writes the bytes as if O_APPEND. Since buffers may be flushed multiple times during the function (not just at return),
 it's not advised to use this simultaneously from multiple threads.

read_ds() and write_ds() may be called from multiple threads simultaneously. These return datastream impls that can be used with
 fc's pack/unpack. Multiple read_datastream and write_datastream created from the same random_access_file can be used simultaneously from different
 threads, but an individual read/write_datastream must only be used by a single thread at a time. unpack_from() and pack_to() are just
 simple helper functions based on these read/write_datastreams. Be aware that read/write_datastreams are buffered. Write buffers are flushed
 on an internal threshold, upon a call to flush(), and upon destruction of the datastream. If a buffered write fails during destruction an
 exception is NOT thrown. So if acting upon write failure is important call flush() prior to destruction; flush() does throw on failure.

seekable_device() may be called from multiple threads simultaneously. This returns a Boost Iostreams SeekableDevice. Similar to datastreams,
 multiple devices created from the same random_access_file can be used simultaneously from different threads, but an individual
 device must only be used by a single thread at a time. These read/write_devices are _not_ buffered.

There is a another important interaction between random_access_file and the read/write_datastream/device that a file creates: it is safe to use
 the datastream/device after the random_access_file they were created from has been destroyed. For example, the following is considered safe:
   std::optional<random_access_file> thefile = random_access_file("somefile");
   write_datstream ds = thefile.write_ds(42);
   thefile.reset();
   fc::raw::pack(ds, (uint32_t)0xbeef);

size(), resize(), and punch_hole() may be called from multiple threads simultaneously. Other threads performing reads or writes on affected
 ranges will give undefined results.

random_access_file isn't copyable, but it is movable. Any calls on a moved-from random_access_file are undefined. Any existing datastreams
 and devices from before the move continue to remain valid. is_valid() can be used to determine if a random_access_file is in the invalid
 moved-from state
*/

namespace fc {

namespace impl {
constexpr static ssize_t append_t = -1;

#ifndef _WIN32
struct random_access_file_context {
   random_access_file_context(const random_access_file_context&) = delete;
   random_access_file_context& operator=(const random_access_file_context&) = delete;

   using native_handle_type = int;

   random_access_file_context(const std::filesystem::path& path, bool read_and_write) : display_path(path) {
      int flags = 0;
      if(read_and_write)
         flags = O_RDWR | O_CREAT;
      else
         flags = O_RDONLY;
#if defined(O_CLOEXEC)
      flags |= O_CLOEXEC;
#endif
#if !defined(__linux__)
      //see pwrite BUGS; we can't set O_APPEND and use pwrite properly on linux. fortunately there is a workaround (see further down)
      flags |= O_APPEND;
#endif
      fd = open(path.c_str(), flags, DEFFILEMODE);
      FC_ASSERT(fd != -1, "Failed to open ${fn}: ${e}", ("fn", display_path)("e", strerror(errno)));
#if !defined(O_CLOEXEC)
      //just swallow errors on this paranoia
      flags = fcntl(fd, F_GETFD);
      if(flags != -1)
         fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
#endif
      struct stat st;
      file_block_size = 4096;
      if(fstat(fd, &st) == 0)
         file_block_size = st.st_blksize;
   }

   template<typename MutableBufferSequence>
   ssize_t read_from(const MutableBufferSequence& mbs, ssize_t offs) {
      struct iovec iov[IOV_MAX];
      int i = 0;
      for(const boost::asio::mutable_buffer& b : mbs) {
         iov[i].iov_base = b.data();
         iov[i].iov_len = b.size();
         if(++i == IOV_MAX)
            break;
      }
      ssize_t red = -1;
      do {
         red = preadv(fd, iov, i, offs);
      } while(red == -1 && errno == EINTR);
      FC_ASSERT(red != -1, "read failure on file ${fn}: ${e}", ("fn", display_path)("e", strerror(errno)));
      return red;
   }

   template<typename ConstBufferSequence>
   void write_to(const ConstBufferSequence& cbs, ssize_t offs) {
      boost::beast::buffers_suffix bs_left(cbs);
      while(boost::beast::buffer_bytes(bs_left)) {
         struct iovec iov[IOV_MAX];
         int i = 0;
         for(const boost::asio::const_buffer& b : bs_left) {
            iov[i].iov_base = (char*)b.data();
            iov[i].iov_len = b.size();
            if(++i == IOV_MAX)
               break;
         }

         ssize_t wrote = -1;
         do {
            if(offs == append_t) {
#ifdef __linux__
               wrote = pwritev2(fd, iov, i, 0, RWF_APPEND);   //linux *not* opened with O_APPEND, appending requires special flag
               if(wrote == -1 && errno == EOPNOTSUPP)         //fallback for kernels before 4.16
                  wrote = pwritev(fd, iov, i, size());
#else
               wrote = writev(fd, iov, i);                    //opened with O_APPEND, just write
#endif
            } else {
               wrote = pwritev(fd, iov, i, offs);
            }
         } while(wrote == -1 && errno == EINTR);
         FC_ASSERT(wrote != -1, "write failure on file ${fn}: ${e}", ("fn", display_path)("e", strerror(errno)));

         bs_left.consume(wrote);
         if(offs != append_t)
            offs += wrote;
      }
   }

   size_t size() const {
      struct stat st;
      int r = fstat(fd, &st);
      FC_ASSERT(r == 0, "fstat failure on file ${fn}: ${e}", ("fn", display_path)("e", strerror(errno)));
      return st.st_size;
   }

   void resize(size_t size) const {
      int r;
      do {
         r = ftruncate(fd, static_cast<off_t>(size));
      } while(r == -1 && errno == EINTR);
      FC_ASSERT(r == 0, "failed to resize file ${fn} to ${sz} bytes: ${e}", ("fn", display_path)("sz", size)("e", strerror(errno)));
   }

   void punch_hole(size_t begin, size_t end) {
      int ret = -1;
#if defined(__linux__)
      ret = fallocate(fd, FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE, static_cast<off_t>(begin), static_cast<off_t>(end-begin));
#elif defined(__APPLE__)
      struct fpunchhole puncher = {0, 0, static_cast<off_t>(begin), static_cast<off_t>(end-begin)};
      ret = fcntl(fd, F_PUNCHHOLE, &puncher);
#elif defined(__FreeBSD__)
      struct spacectl_range puncher = {static_cast<off_t>(begin), static_cast<off_t>(end-begin)};
      ret = fspacectl(fd, SPACECTL_DEALLOC, &puncher, 0, nullptr);
#else
      errno = ENOTSUP;
#endif
      if(ret == -1 && !one_hole_punch_warning_is_enough.test_and_set())
         wlog("Failed to punch hole in file ${fn}: ${e}", ("fn", display_path)("e", strerror(errno)));
   }

   native_handle_type native_handle() const {
      return fd;
   }

   ~random_access_file_context() {
      if(fd != -1)
         close(fd);
   }

   native_handle_type    fd = -1;
   std::atomic_flag      one_hole_punch_warning_is_enough;
   std::filesystem::path display_path;
   size_t                file_block_size = 4096;
};
#else
#warning WIN32 impl of random_access_file_context has some failing tests
struct random_access_file_context {
   random_access_file_context(const random_access_file_context&) = delete;
   random_access_file_context& operator=(const random_access_file_context&) = delete;

   using native_handle_type = HANDLE;

   random_access_file_context(const std::filesystem::path& path, bool read_and_write) : display_path(path), file(local_ctx, path.generic_string().c_str(),
                read_and_write ? boost::asio::random_access_file::read_only : boost::asio::random_access_file::create | boost::asio::random_access_file::read_write) {
      //TODO: is this right?
      FILE_STORAGE_INFO file_storage_info;
      if(GetFileInformationByHandleEx(native_handle(), FileStorageInfo, &file_storage_info, sizeof(file_storage_info)))
         file_block_size = file_storage_info.FileSystemEffectivePhysicalBytesPerSectorForAtomicity;
   }

   template<typename MutableBufferSequence>
   ssize_t read_from(const MutableBufferSequence& mbs, ssize_t offs) {
      try {
         return file.read_some_at(offs, mbs);
      }
      catch(boost::system::system_error& e) {
         if(e.code() == boost::asio::error::eof)
            return 0;
         throw;
      }
   }

   template<typename ConstBufferSequence>
   void write_to(const ConstBufferSequence& cbs, ssize_t offs) {
      //interestingly enough, append_t will work as expected here because -1 = 0xffffffffffffffff for offset means to
      // append in Win32 API. This is leaning on implemention specific behavior in asio though (that offset is passed
      // untouched to underlying Win32 call)
      boost::beast::buffers_suffix bs_left(cbs);
      while(boost::beast::buffer_bytes(bs_left)) {
         size_t wrote = file.write_some_at(offs, bs_left);
         bs_left.consume(wrote);
         if(offs != append_t)
            offs += wrote;
      }
   }

   size_t size() const {
      return file.size();
   }

   void resize(size_t size) {
      file.resize(size);
   }

   void punch_hole(size_t begin, size_t end) {
      if(DeviceIoControl(native_handle(), FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, nullptr, nullptr) == FALSE) {
         if(!one_hole_punch_warning_is_enough.test_and_set())
            wlog("Failed to enable sparseness on file ${fn}", ("fn", display_path));
         return;
      }
      FILE_ZERO_DATA_INFORMATION holeinfo = {begin, end};
      if(DeviceIoControl(native_handle(), FSCTL_SET_ZERO_DATA, &holeinfo, sizeof(holeinfo), nullptr, 0, nullptr, 0) == FALSE) {
         if(!one_hole_punch_warning_is_enough.test_and_set())
            wlog("Failed to punch hole on file ${fn}", ("fn", display_path));
   }

   native_handle_type native_handle() {
      return file.native_handle();
   }

   boost::asio::io_context         local_ctx;
   std::filesystem::path           display_path;
   boost::asio::random_access_file file;
   std::atomic_flag                one_hole_punch_warning_is_enough;
   size_t                          file_block_size = 4096;
};
#endif

using random_access_file_context_ptr = std::shared_ptr<impl::random_access_file_context>;
}

class random_access_file {
public:
   enum flags {
      read_only  = 1<<0,
      read_write = 1<<1,
   };

   class read_datastream {
      friend class random_access_file;

      read_datastream(const read_datastream&) = delete;
      read_datastream& operator=(const read_datastream&) = delete;

      read_datastream(impl::random_access_file_context_ptr& ctx, const ssize_t start_pos) : ctx(ctx), next_pos(start_pos) {}

      void check_available(size_t size) {
         while(buffer.size() < size) {
            ssize_t red = ctx->read_from(buffer.prepare(64*1024), next_pos);
            if(red == 0)
               FC_THROW_EXCEPTION(out_of_range_exception, "unexpected end of file ${fn}", ("fn", ctx->display_path));
            buffer.commit(red);
            next_pos += red;
         }
      }

   public:
      read_datastream(read_datastream&& o) = default;

      void skip(size_t size) {
         check_available(size);
         buffer.consume(size);
      }

      bool read(char* d, size_t size) {
         check_available(size);
         boost::asio::buffer_copy(boost::asio::buffer(d, size), buffer.cdata());
         buffer.consume(size);
         return true;
      }

      bool get(unsigned char& c) {
         return get(*(char*)&c);
      }
      bool get(char& c) {
         return read(&c, 1);
      }

   private:
      boost::beast::multi_buffer           buffer;
      impl::random_access_file_context_ptr ctx;
      ssize_t                              next_pos;
   };

   class write_datastream {
      friend class random_access_file;
      constexpr static unsigned buffer_flush_threshold = 256*1024;

      write_datastream(const write_datastream&) = delete;
      write_datastream& operator=(const write_datastream&) = delete;

      write_datastream(impl::random_access_file_context_ptr& ctx, const ssize_t start_pos) : ctx(ctx), next_pos(start_pos) {
         buffer.reserve(buffer_flush_threshold + 4*1024);
      }

      void do_write() {
         try {
            ctx->write_to(buffer.cdata(), next_pos);
         } catch(...) {
            //prevent another write attempt during dtor
            buffer.clear();
            throw;
         }
         if(next_pos != impl::append_t)
            next_pos += buffer.size();
         buffer.clear();
      }

      void do_write_if_buffer_large() {
         if(buffer.size() > buffer_flush_threshold)
            do_write();
      }

   public:
      write_datastream(write_datastream&& o) = default;

      bool write(const char* d, size_t s) {
         boost::asio::buffer_copy(buffer.prepare(s), boost::asio::const_buffer(d, s));
         buffer.commit(s);

         do_write_if_buffer_large();
         return true;
      }

      void flush() {
         do_write();
      }

      inline bool put(char c) {
         return write(&c, sizeof(char));
      }

      ~write_datastream() {
         try {
            if(ctx)
               do_write();
         } FC_LOG_AND_DROP(("write failure ignored"));
      }

   private:
      boost::beast::flat_buffer                buffer;
      impl::random_access_file_context_ptr     ctx;
      ssize_t                                  next_pos;
   };

   class device {
   public:
      friend class random_access_file;
      typedef char char_type;
      typedef boost::iostreams::seekable_device_tag category;

      explicit device(impl::random_access_file_context_ptr& ctx) : ctx(ctx) {}

      //"To be usable with the streams and stream buffers provided by the Boost Iostreams library, Devices must model Blocking."
      //"A Device is Blocking if a read request never produces fewer characters than requested except at end-of-stream"
      std::streamsize read(char* s, std::streamsize n) {
         ssize_t total_red = 0;
         while(n - total_red) {
            ssize_t red = ctx->read_from(boost::asio::buffer(s+total_red, n-total_red), pos);
            if(red == 0)
               break;
            pos += red;
            total_red += red;
         }
         return total_red ?: -1;
      }

      std::streamsize write(const char* s, std::streamsize n) {
         ctx->write_to(boost::asio::const_buffer(s, n), pos);
         pos += n;
         return n;
      }

      std::streampos seek(boost::iostreams::stream_offset off, std::ios_base::seekdir way) {
         if(way == std::ios_base::beg)
            pos = off;
         else if(way == std::ios_base::end)
            pos = ctx->size() + off;
         else
            pos += off;

         return pos;
      }

   private:
      impl::random_access_file_context_ptr ctx;
      ssize_t                              pos = 0;
   };

   random_access_file(const std::filesystem::path& path, const flags open_flags = read_write) : ctx(new impl::random_access_file_context(path, open_flags == read_write)) {}

   random_access_file(const random_access_file&) = delete;
   random_access_file& operator=(const random_access_file&) = delete;
   random_access_file(random_access_file&&) = default;
   random_access_file& operator=(random_access_file&&) = default;

   template<typename T>
   T unpack_from(const ssize_t offset) {
      T t;
      read_datastream ds(ctx, offset);
      fc::raw::unpack(ds, t);
      return t;
   }

   template<typename T>
   void pack_to(const T& v, const ssize_t offset) {
      write_datastream ds(ctx, offset);
      fc::raw::pack(ds, v);
      ds.flush();
   }

   template<typename T>
   void pack_to_end(const T& v) {
      write_datastream ds(ctx, impl::append_t);
      fc::raw::pack(ds, v);
      ds.flush();
   }

   read_datastream read_ds(const ssize_t offset) {
      return read_datastream(ctx, offset);
   }

   write_datastream write_ds(const ssize_t offset) {
      return write_datastream(ctx, offset);
   }

   write_datastream append_ds() {
      return write_datastream(ctx, impl::append_t);
   }

   device seekable_device() {
      return device(ctx);
   }

   size_t size() const {
      return ctx->size();
   }

   void resize(size_t size) {
      FC_ASSERT(size <= std::numeric_limits<off_t>::max(), "setting file ${fn} too large", ("fn", ctx->display_path));
      ctx->resize(size);
   }

   void punch_hole(size_t begin, size_t end) {
      FC_ASSERT(begin <= std::numeric_limits<off_t>::max(), "start of hole punch out of range for ${fn}", ("fn", ctx->display_path));
      FC_ASSERT(end <= std::numeric_limits<off_t>::max(), "end of hole punch out of range for ${fn}", ("fn", ctx->display_path));

      //some OS really want the hole punching to be aligned to FS block size
      if(begin % ctx->file_block_size) {
         begin &= ~(ctx->file_block_size-1);
         begin += ctx->file_block_size;
      }
      end &= ~(ctx->file_block_size-1);

      if(begin >= end)
         return;

      ctx->punch_hole(begin, end);
   }

   impl::random_access_file_context::native_handle_type native_handle() const {
      return ctx->native_handle();
   }

   boost::interprocess::mapping_handle_t get_mapping_handle() const {
      return {native_handle(), false};
   }

   bool is_valid() const {
      return !!ctx;
   }

   std::filesystem::path display_path() const {
      return ctx->display_path;
   }
   void set_display_path(std::filesystem::path& new_path) {
      ctx->display_path = new_path;
   }

private:
   impl::random_access_file_context_ptr ctx;
};

BOOST_IOSTREAMS_PIPABLE(random_access_file::device, 0)

}