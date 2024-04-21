using namespace eosio;


class bitset {
    using word_t = uint32_t;
    static constexpr size_t num_bits = sizeof(word_t) * 8;
    static size_t round_up(size_t sz) { return (sz + num_bits - 1) / num_bits; }

public:
    bitset(word_t size) 
        : num_bits(size), data(round_up(size), 0) {}
    
   bitset(word_t size, const std::vector<word_t> raw_bitset)
       : num_bits(size), data(raw_bitset) {
           check(raw_bitset.size() == round_up(size), "invalid raw bitset size");
       }

    // Set a bit to 1
    void set(word_t index) {
        check_bounds(index);
        data[index / 32] |= (1UL << (index % num_bits));
    }

    // Clear a bit (set to 0)
    void clear(word_t index) {
        check_bounds(index);
        data[index / num_bits] &= ~(1UL << (index % num_bits));
    }

    // Check if a bit is set
    bool test(word_t index) const {
        check_bounds(index);
        return (data[index / num_bits] & (1UL << (index % num_bits))) != 0;
    }

    // Size of the bitset
    word_t size() const {
        return num_bits;
    }

private:
    word_t num_bits;
    std::vector<word_t> data;

    // Check if the index is within bounds
    void check_bounds(word_t index) const {
        check(index < num_bits, "bitset index out of bounds");
    }
};

