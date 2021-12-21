#include <cstdint>
#include <fstream>
#include <algorithm>
#include <array>
#include <stack>
#include <random>
#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>

int main(int argc, const char *argv[]) {
  if (argc < 2) return 1;
  std::random_device rd;
  std::mt19937 engine{rd()};

  using namespace std::this_thread;
  using namespace std::chrono_literals;

  using int_distrib_t = std::uniform_int_distribution<int>;
  using interval = int_distrib_t::param_type;
  int_distrib_t rand;

  using byte = std::uint8_t;
  using u8 = std::uint8_t;
  using u16 = std::uint16_t;

  // true if followed original behavior
  const bool shift = true;
  const bool jump = true;
  const bool load_store = false;

  constexpr byte font[]{0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70, 0xF0, 0x10,
                        0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0, 0x90, 0x90, 0xF0, 0x10,
                        0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0, 0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0,
                        0x10, 0x20, 0x40, 0x40, 0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0,
                        0x10, 0xF0, 0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
                        0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0, 0xF0, 0x80,
                        0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80};

  std::array<byte, 4 * 1024> ram{};
  std::copy(std::begin(font), std::end(font), std::begin(ram));

  constexpr u16 start_address = 0x200;
  std::ifstream fin{argv[1]};
  std::copy(std::istreambuf_iterator<char>(fin), {}, std::begin(ram) + start_address);

  u16 PC{start_address};
  u16 I{};

  using screenline = std::array<bool, 64>;
  using screen_t = std::array<screenline, 32>;
  screen_t screen;

  std::stack<u16> stack;

  u8 delay_timer{};
  u8 sound_timer{};

  std::array<byte, 16> VX{};
  byte &VF = VX[15];

  bool keypad[16]{};

  while (true) {
    bool frame_changed = false;

    const u16 op = (ram[PC] << 8) | ram[PC + 1];

    const u16 nnn = op & 0x0fff;
    const u8 nn = op & 0x00ff;
    const u8 n = op & 0x000f;

    const u8 x = (op & 0x0f00) >> 8;
    const u8 y = (op & 0x00f0) >> 4;

    // clang-format off
    switch ((op & 0xf000) >> 12) {
      case 0x0:
        switch (op & 0x00ff) {
          case 0xe0: { for (auto &line : screen) line.fill(false); } break;
          case 0xee: PC = stack.top(); stack.pop(); break;
        }
        break;

      case 0x1: PC = nnn; break;
      case 0x2: stack.push(PC); PC = nnn; break;
      case 0x3: if (VX[x] == nn) { PC += 2; } break;
      case 0x4: if (VX[x] != nn) { PC += 2; } break;
      case 0x5: if (VX[x] == VX[y]) { PC += 2; } break;
      case 0x6: VX[x] = nn; break;
      case 0x7: VX[x] += nn; break;
      case 0x8:
        switch (op & 0x000f) {
          case 0x0: VX[x] = VX[y]; break;
          case 0x1: VX[x] |= VX[y]; break;
          case 0x2: VX[x] &= VX[y]; break;
          case 0x3: VX[x] ^= VX[y]; break;
          case 0x4: VF = (VX[x] + VX[y]) > 0xff; VX[x] += VX[y]; break;
          case 0x5: VF = VX[x] > VX[y]; VX[x] -= VX[y]; break;
          case 0x6: if (shift) { VX[x] = VX[y]; } VF = VX[x] & 0b0000'0001; VX[x] >>= 1; break;
          case 0x7: VF = VX[y] > VX[x]; VX[x] = VX[y] - VX[x]; break;
          case 0xe: if (shift) { VX[x] = VX[y]; } VF = VX[x] & 0b1000'0000; VX[x] <<= 1; break;
        }
        break;

      case 0x9: if (VX[x] != VX[y]) { PC += 2; } break;
      case 0xa: I = nnn; break;
      case 0xb: if (jump) { PC = VX[0] + nnn; } else { PC = VX[x] + nn; } break;
      case 0xc: VX[x] = nn & rand(engine, interval{0, 0xff}); break;
      case 0xd: {
        const u8 X = VX[x] % 64;
        const u8 Y = VX[y] % 32;
        const u8 N = n;

        // clang-format on
        VF = 0;
        for (std::size_t row = 0; row < N; ++row) {
          if (Y + row > 31) break;

          const byte tile_line_on_memory = ram[I + row];
          if (tile_line_on_memory == 0) continue;

          for (std::size_t column = 0; column < 7; ++column) {
            if (X + column > 63) break;

            bool &tile_pixel_on_screen = screen[Y + row][X + column];

            if (tile_line_on_memory & (0b1000'0000 >> column)) {
              if (tile_pixel_on_screen) {
                VF = 1;
                tile_pixel_on_screen = false;
              } else {
                tile_pixel_on_screen = true;
              }
              frame_changed = true;
            }
          }
        }
      } break;

        // clang-format off
      case 0xe:
        switch (op & 0x00ff) {
          case 0x9e: if (keypad[VX[x]] & 0x0f) { PC += 2; } break;
          case 0xa1: if (!keypad[VX[x] & 0x0f]) { PC += 2; } break;
        }

        break;

      case 0xf:
        switch (op & 0x00ff) {
          case 0x07: delay_timer = VX[x]; break;
          case 0x15: VX[x] = delay_timer; break;
          case 0x18: VX[x] = sound_timer; break;
          case 0x1e: if ((I + VX[x]) > 0x0fff) { VF = 1; } I += VX[x]; break;
          case 0x0a: // blocking key input
            //  VX[x] = key-input-in-hexa;
            assert(false);
            break;

          case 0x29: I = font[VX[x]]; break;
          case 0x33: ram[I] = VX[x] / 100; ram[I + 1] = (VX[x] % 100) / 10; ram[I + 2] = VX[x] % 10; break;
          case 0x55: std::copy(begin(VX), begin(VX) + x + 1, begin(ram) + I); if (!load_store) { I += (x + 1); } break;
          case 0x65: std::copy(begin(ram) + I, begin(ram) + I + x + 1, begin(VX)); if (!load_store) { I += (x + 1); } break;
        }

        break;
    }
    PC += 2;

    if (delay_timer > 0) { --delay_timer; }
    if (sound_timer > 0) { std::cout << '\a'; --sound_timer; }

    //  std::cout << std::string('\n', 20);
    if (frame_changed) {
      frame_changed = false;
      std::cout << '\n';

      for (const auto &line : screen)
        for (std::cout << '\n'; const bool pixel : line)
          if (pixel)
            std::cout << "\u2b1b";
          else
            std::cout << "\u2b1c";
      
    }

    sleep_for(16ms);
  }
  return 0;
}

