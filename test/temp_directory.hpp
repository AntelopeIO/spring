#include <filesystem>

class temp_directory {
   std::filesystem::path tmp_path;

 public:
   temp_directory(const std::filesystem::path& tempFolder = std::filesystem::temp_directory_path()) {
      std::filesystem::path template_path{ tempFolder / "chainbase-tests-XXXXXX" };
      char                  tmp_buf[4096];
      strncpy(tmp_buf, template_path.c_str(), 4096);
      if (mkdtemp(tmp_buf) == nullptr)
         throw std::system_error(errno, std::generic_category(), __PRETTY_FUNCTION__);
      tmp_path = tmp_buf;
   }
   temp_directory(const temp_directory&) = delete;
   temp_directory(temp_directory&& other) { tmp_path.swap(other.tmp_path); }

   ~temp_directory() {
      if (!tmp_path.empty()) {
         std::error_code ec;
         std::filesystem::remove_all(tmp_path, ec);
      }
   }

   temp_directory& operator=(const temp_directory&) = delete;
   temp_directory& operator=(temp_directory&& other) {
      tmp_path.swap(other.tmp_path);
      return *this;
   }
   const std::filesystem::path& path() const { return tmp_path; }
};