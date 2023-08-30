#import <array>
#import <cstdint>
#import <string>
#import <string_view>
#import <variant>

namespace literals {
    //enum _block : bool {noblock, block};
    enum _ifempty : bool {noifempty, ifempty};
    //enum _dest {pins, x, y, null, pindirs};
    enum _cond {always, not_x, x_dec, not_y, y_dec, x_not_y, pin, not_osre};
    //enum _mode {clear /*, block */};
    //enum _src {gpip, pins, irq};

    constexpr int block = 1;
    constexpr int noblock = 0;
    constexpr int clear = 0;
    using _block = int;
    using _mode = int;

    constexpr int pins = 0;
    constexpr int x = 1;
    constexpr int y = 2;
    constexpr int null = 3;
    constexpr int pindirs = 4;
    constexpr int pc = 5;
    constexpr int isr = 6;
    constexpr int exec = 7;

    using _dest = int;
    using _src = int;

    constexpr
    int rel(int idx) {
        return 0b10000 | idx;
    }
}

constexpr int dec(int value) {
    return value == 32 ? 0 : value;
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
    int wrap_target_addr{0};
    int wrap_addr{32};

    std::array<std::string_view, 32> labels;
    std::array<std::string_view, 32> target_labels;

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
        v[count++] = 0b011 << 13 | dest << 5 | dec(bit_count);
        return *this;
    }
    constexpr
    Builder& in(literals::_dest dest, int bit_count) {
        v[count++] = 0b010 << 13 | dest << 5 | dec(bit_count);
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
    Builder& irq(literals::_mode mode, int index) {
        int clr;
        int wait;
        if(mode == literals::block) {
            clr = 0;
            wait = 1;
        } else {
            clr = 1;
            wait = 0;
        }
        v[count++] = 0b110 << 13 | 0b0 << 7 | clr << 6 | wait << 5 | index;
        return *this;
    }

    constexpr
    Builder& nop() {
        return mov(literals::y, literals::y);
    }

    constexpr
    Builder& label(const char* name) {
        labels[count] = name;
        return *this;
    }
    constexpr
    Builder& jmp(literals::_cond cond, const char* addr) {
        target_labels[count] = addr;
        v[count++] = 0b000 << 13 | cond << 5;
        return *this;
    }

    constexpr
    Builder& wait(int pol, literals::_src src, int index) {
        if (src == literals::pin) src = 1;  // TODO:
        v[count++] = 0b001 << 13 | pol << 7 | src << 5 | index;
        return *this;
    }

    constexpr
    Builder& build() {
        // Resolve label addresses
        for(int i = 0; i < count; ++i) {
            if((v[i] >> 13) == 0) {
                // Jmp
                v[i] |= lookup_label(target_labels[i]);
            }
        }
        return *this;
    }

    constexpr
    Builder& operator[](int delay) {
        v[count - 1] |= delay << 8;
        return *this;
    }

    constexpr
    Builder& side(int index) {
        // LSB = delay
        // MSB = side
        const bool has_side = true;  // TODO:
        v[count - 1] |= has_side << 12;
        v[count - 1] |= index << 11;
        return *this;
    }
};

static void test2() {
    // From https://github.com/raspberrypi/pico-examples/blob/master/pio/i2c/i2c.pio

    using namespace literals;
    constexpr auto b = Builder()
    .label("do_nack")
    .jmp(y_dec, "entry_point")        // Continue if NAK was expected
    .irq(block, rel(0))             // Otherwise stop, ask for help
    .label("do_byte")
    .set(x, 7)                   // Loop 8 times
    .label("bitloop")
    .out(pindirs, 1)         [7] // Serialise write data (all-ones if reading)
    .nop().side(1) [2] // SCL rising edge
    .wait(1, pin, 1)          [4] // Allow clock to be stretched
    .in(pins, 1)             [7] // Sample read data in middle of SCL pulse
    .jmp(x_dec, "bitloop").side(0)[7] // SCL falling edge
    .out(pindirs, 1)         [7] // On reads, we provide the ACK.
    .nop().side(1) [7]       // SCL rising edge
    .wait(1, pin, 1)          [7] // Allow clock to be stretched
    .jmp(pin, "do_nack").side(0)[2] // Test SDA for ACK/NAK, fall through if ACK
    .label("entry_point")
    .wrap_target()
    .out(x, 6)                   // Unpack Instr count
    .out(y, 1)                   // Unpack the NAK ignore bit
    .jmp(not_x, "do_byte")             // Instr == 0, this is a data record.
    .out(null, 32)               // Instr > 0, remainder of this OSR is invalid
    .label("do_exec")
    .out(exec, 16)               // Execute one instruction per FIFO word
    .jmp(x_dec, "do_exec")            // Repeat n + 1 times
    .wrap()
    .build();

    static_assert(b.v[0] == 0x008c);
    static_assert(b.v[1] == 0xc030);
    static_assert(b.v[2] == 0xe027);
    static_assert(b.v[3] == 0x6781);
    static_assert(b.v[4] == 0xba42);
    static_assert(b.v[5] == 0x24a1);
    static_assert(b.v[6] == 0x4701);
    static_assert(b.v[7] == 0x1743);
    static_assert(b.v[8] == 0x6781);
    static_assert(b.v[9] == 0xbf42);
    static_assert(b.v[10] == 0x27a1);
    static_assert(b.v[11] == 0x12c0);
    static_assert(b.v[12] == 0x6026);
    static_assert(b.v[13] == 0x6041);
    static_assert(b.v[14] == 0x0022);
    static_assert(b.v[15] == 0x6060);
    static_assert(b.v[16] == 0x60f0);
    static_assert(b.v[17] == 0x0050);

    static_assert(b.wrap_target_addr == 12);
    static_assert(b.wrap_addr == 17);
}

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
    test2();
}
