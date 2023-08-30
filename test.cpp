#import <array>
#import <cstdint>
#import <string>
#import <string_view>

namespace literals {
    enum _block : bool {noblock, block};
    enum _ifempty : bool {noifempty, ifempty};
    enum _dest {pins, x, y};
    enum _cond {always, not_x, x_dec, not_y, y_dec, x_not_y, pin, not_osre};
}

constexpr uint16_t build_int16(std::array<bool, 16> in) {
    uint16_t value = 0;
    for (int i = 0; i < 15; ++i) {
        value |= in[i];
        value <<= 1;
    }
    return value;
}

struct Builder {
    std::array<uint16_t, 32> v;
    int count = 0;
    int wrap_target_addr;
    int wrap_addr;

    std::array<std::string_view, 32> labels;

    constexpr
    int lookup_label(std::string_view label) {
        int index = -1;
        for(int i = 0; i < 32; ++i) {
            if(labels[i] == label) {
                index = i;
                break;
            }
        }

        //TODO: Detect missing label

        return index;
    }

    constexpr
    Builder& pull(literals::_block block, literals::_ifempty ifempty = literals::noifempty) {
        v[count++] = 0b100 << 13 | 0b1 << 7 | ifempty << 6 | block << 5 | 0;
        return *this;
    }
    constexpr
    Builder& out(literals::_dest dest, int bit_count) {
        v[count++] = 0b011 << 13 | dest << 5 | (bit_count == 32 ? 0 : bit_count -  1);
        return *this;
    }
    constexpr
    Builder& wrap_target() {
        wrap_target_addr = count;
        return *this;
    }
    constexpr
    Builder& wrap() {
        wrap_addr = count - 1;
        return *this;
    }
    constexpr
    Builder& mov(literals::_dest dest, literals::_dest src) {
        v[count++] = 0b101 << 13 | dest << 5 | src;
        return *this;
    }
    constexpr
    Builder& set(literals::_dest dest, int data) {
        v[count++] = 0b111 << 13 | dest << 5 | data;
        return *this;
    }
    constexpr
    Builder& label(const char* name) {
        labels[count] = name;
        return *this;
    }
    constexpr
    Builder& jmp(literals::_cond cond, const char* addr) {
        v[count++] = 0b000 << 13 | cond << 5 | lookup_label(addr);
        return *this;
    }
    constexpr
    Builder& build() {
        return *this;
    }
};

static void test1() {
    // From https://github.com/raspberrypi/pico-examples/blob/master/pio/pio_blink/blink.pio

    using namespace literals;
    constexpr auto b = Builder()
    .pull(block)
    .out(y, 32)
    .wrap_target()
    .mov(x, y)
    .set(pins, 1)          // Turn LED on
    .label("lp1")         
    .jmp(x_dec, "lp1")     // Delay for (x + 1) cycles, x is a 32 bit number
    .mov(x, y)
    .set(pins, 0)          // Turn LED off
    .label("lp2")
    .jmp(x_dec, "lp2")     // Delay for the same number of cycles again
    .wrap()
    .build();

    static_assert(b.v[0] == 0x80a0);
    static_assert(b.v[1] == 0x6040);
    static_assert(b.v[2] == 0xa022);
    static_assert(b.v[3] == 0xe001);
    static_assert(b.v[4] == 0x0044);
    static_assert(b.v[5] == 0xa022);
    static_assert(b.v[6] == 0xe000);
    static_assert(b.v[7] == 0x0047);

    static_assert(b.wrap_target_addr == 2);
    static_assert(b.wrap_addr == 7);
}

int main() {
    test1();
}
