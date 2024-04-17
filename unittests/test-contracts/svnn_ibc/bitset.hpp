using namespace eosio;

class bitset {
public:
    bitset(uint32_t size) 
        : num_bits(size), data((size + 31) / 32, 0) {}
    
   bitset(uint32_t size, const std::vector<uint32_t> raw_bitset)
       : num_bits(size), data(raw_bitset) {
           check(raw_bitset.size() == (size + 31) / 32, "invalid raw bitset size");

       }

    // Set a bit to 1
    void set(uint32_t index) {
        check_bounds(index);
        data[index / 32] |= (1UL << (index % 32));
    }

    // Clear a bit (set to 0)
    void clear(uint32_t index) {
        check_bounds(index);
        data[index / 32] &= ~(1UL << (index % 32));
    }

    // Check if a bit is set
    bool test(uint32_t index) const {
        check_bounds(index);
        return (data[index / 32] & (1UL << (index % 32))) != 0;
    }

    // Size of the bitset
    uint32_t size() const {
        return num_bits;
    }

private:
    uint32_t num_bits;
    std::vector<uint32_t> data;

    // Check if the index is within bounds
    void check_bounds(uint32_t index) const {
        check(index < num_bits, "bitset index out of bounds");
    }
};

