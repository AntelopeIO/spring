using namespace eosio;

namespace savanna {

    class bitset {
        using word_t = uint8_t;
        static constexpr size_t num_bits = sizeof(word_t) * 8;
        static size_t round_up(size_t sz) { return (sz + num_bits - 1) / num_bits; }

    public:

        // compare two bitsets
        static std::pair<savanna::bitset, savanna::bitset> compare(const savanna::bitset& bs1, const savanna::bitset& bs2) {
            check(bs1.size() == bs2.size(), "bitsets must be of the same size");

            size_t size = bs1.size();
            std::vector<word_t> intersection_data(bs1.data.size());
            std::vector<word_t> symmetric_difference_data(bs1.data.size());

            savanna::bitset intersection(size);
            savanna::bitset symmetric_difference(size);
            for (size_t i = 0; i < bs1.data.size(); ++i) {
                intersection.data[i]         = bs1.data[i] & bs2.data[i];
                symmetric_difference.data[i] = bs1.data[i] ^ bs2.data[i];
            }

            return {intersection, symmetric_difference};
        }

        bitset(size_t size) 
            : data(round_up(size), 0) {}
        
         bitset(size_t size, const std::vector<word_t>& raw_bitset)
           : data(raw_bitset) {
               check(raw_bitset.size() == round_up(size), "invalid raw bitset size");
           }

        // Set a bit to 1
        void set(size_t index) {
            check_bounds(index);
            data[index / num_bits] |= (1UL << (index % num_bits));
        }

        // Clear a bit (set to 0)
        void clear(size_t index) {
            check_bounds(index);
            data[index / num_bits] &= ~(1UL << (index % num_bits));
        }

        // Check if a bit is set
        bool test(size_t index) const {
            check_bounds(index);
            return (data[index / num_bits] & (1UL << (index % num_bits))) != 0;
        }

        // Size of the bitset
        size_t size() const {
            return num_bits * data.size();
        }
        
        std::string to_string() const {
            const char* hex_chars = "0123456789abcdef";
            std::string result;
            result.reserve(data.size() * 2); // Each byte will be represented by two hex characters
            for (auto byte : data) {
                result.push_back(hex_chars[byte & 0x0F]);
                result.push_back(hex_chars[(byte >> 4) & 0x0F]);
            }
            return result;
        }

    private:
        std::vector<word_t> data;

        // Check if the index is within bounds
        void check_bounds(size_t index) const {
            check(index < size(), "bitset index out of bounds");
        }
    };

}