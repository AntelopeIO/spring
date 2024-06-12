#pragma once

#include <boost/iostreams/filtering_streambuf.hpp>

namespace eosio::detail {

namespace bio = boost::iostreams;

// directly adapt from boost/iostreams/filter/counter.hpp and change the type of chars_ to uint64_t.
class counter  {
public:
    typedef char char_type;
    struct category
        : bio::dual_use,
          bio::filter_tag,
          bio::multichar_tag,
          bio::optimally_buffered_tag
        { };

    uint64_t characters() const { return chars_; }
    std::streamsize optimal_buffer_size() const { return 64*1024; }

    template<typename Source>
    std::streamsize read(Source& src, char_type* s, std::streamsize n)
    {
        std::streamsize result = bio::read(src, s, n);
        if (result == -1)
            return -1;
        chars_ += result;
        return result;
    }

    template<typename Sink>
    std::streamsize write(Sink& snk, const char_type* s, std::streamsize n)
    {
        std::streamsize result = bio::write(snk, s, n);
        chars_ += result;
        return result;
    }

private:
    uint64_t chars_ = 0;
};
BOOST_IOSTREAMS_PIPABLE(counter, 0)

}