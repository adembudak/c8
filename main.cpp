// clang-format off

#include <random>
#include <thread>
#include <chrono>
#include <cstdint>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>

int main(int argc, const char *argv[]) {
  if (argc < 2) return 1;

  std::random_device rd;
  std::mt19937 engine{rd()};
  std::uniform_int_distribution<int> rand(0, 0xFF);

  using namespace std::this_thread;
  using namespace std::chrono_literals;

  using byte = std::uint8_t;
  using u8 = std::uint8_t;
  using u16 = std::uint16_t;

  // true if followed original behavior
  const bool shift = true;
  const bool jump = true;
  const bool load_store = false;

  const bool white = true;
  const bool black = false;

  constexpr std::array font{
      0xF0, 0x90, 0x90, 0x90, 0xF0, 0x20, 0x60, 0x20, 0x20, 0x70, 0xF0, 0x10,
      0xF0, 0x80, 0xF0, 0xF0, 0x10, 0xF0, 0x10, 0xF0, 0x90, 0x90, 0xF0, 0x10,
      0x10, 0xF0, 0x80, 0xF0, 0x10, 0xF0, 0xF0, 0x80, 0xF0, 0x90, 0xF0, 0xF0,
      0x10, 0x20, 0x40, 0x40, 0xF0, 0x90, 0xF0, 0x90, 0xF0, 0xF0, 0x90, 0xF0,
      0x10, 0xF0, 0xF0, 0x90, 0xF0, 0x90, 0x90, 0xE0, 0x90, 0xE0, 0x90, 0xE0,
      0xF0, 0x80, 0x80, 0x80, 0xF0, 0xE0, 0x90, 0x90, 0x90, 0xE0, 0xF0, 0x80,
      0xF0, 0x80, 0xF0, 0xF0, 0x80, 0xF0, 0x80, 0x80};

  std::array<byte, 4 * 1024> ram{};
  std::copy(cbegin(font), cend(font), begin(ram));

  constexpr u16 start_address = 0x200;
  std::ifstream fin{argv[1]};
  std::copy(std::istreambuf_iterator(fin), {}, ram.begin() + start_address);

  u16 PC{start_address};
  u16 I{};

  std::array<u16, 16> stack;
  u16 SP = stack.size() -1;

  constexpr u8 screen_width = 64;
  constexpr u8 screen_height = 32;

  using screenline = std::array<bool, screen_width>;
  using screen_t = std::array<screenline, screen_height>;
  screen_t screen{};

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

    switch ((op & 0xf000) >> 12) {
      case 0x0:
        switch (op & 0x00ff) {
          case 0xe0: { for (auto &line : screen) line.fill(black); frame_changed = true; } break;
          case 0xee: PC = stack[SP++]; break;
        }
        break;

      case 0x1: PC = nnn; PC -= 2; break;
      case 0x2: stack[--SP] = PC; PC = nnn; PC -= 2; break;
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
          case 0x6: VF = bool(VX[x] & 0b0000'0001); if (shift) { VX[x] = VX[y] >> 1; } else { VX[x] >>= 1; } break;
          case 0x7: VF = VX[y] > VX[x]; VX[x] = VX[y] - VX[x]; break;
          case 0xe: VF = bool(VX[x] & 0b1000'0000); if (shift) { VX[x] = VX[y] << 1; } else { VX[x] <<= 1; } break;
        }
        break;

      case 0x9: if (VX[x] != VX[y]) { PC += 2; } break;
      case 0xa: I = nnn; break;
      case 0xb: if (jump) { PC = VX[0] + nnn; } else { PC = VX[x] + nn; } PC -= 2; break;
      case 0xc: VX[x] = nn & rand(engine); break;
      case 0xd: {
        const u8 X = VX[x] % screen_width;
        const u8 Y = VX[y] % screen_height;
        const u8 N = n;

        VF = 0;
        for (std::size_t row = 0; row < N; ++row) {
          if (Y + row > screen_height - 1) break;

          const byte tile_line_on_memory = ram[I + row];
          if (tile_line_on_memory == 0) continue;

          for (std::size_t column = 0; column < 8; ++column) {
            if (X + column > screen_width - 1) break;

            bool &tile_pixel_on_screen = screen[Y + row][X + column];

            if (tile_line_on_memory & (0b1000'0000 >> column)) {
              if (tile_pixel_on_screen == white) {
                VF = 1;
                tile_pixel_on_screen = black;
              } else {
                tile_pixel_on_screen = white;
              }
              frame_changed = true;
            }
          }
        }
      } break;

      case 0xe:
        switch (op & 0x00ff) {
          case 0x9e: if (keypad[VX[x]]) { PC += 2; } break;
          case 0xa1: if (!keypad[VX[x]]) { PC += 2; } break;
        }
        break;

      case 0xf:
        switch (op & 0x00ff) {
          case 0x07: VX[x] = delay_timer; break;
          case 0x15: delay_timer = VX[x]; break;
          case 0x18: sound_timer = VX[x]; break;
          case 0x1e: VF = (I + VX[x]) > 0x0fff; I += VX[x]; break;
          case 0x0a: VX[x] = 0xFF & std::getchar(); break;

          case 0x29: I = VX[x]*5; break;
          case 0x33: ram[I] = VX[x] / 100; ram[I + 1] = (VX[x] / 10) % 10; ram[I + 2] = VX[x] % 10; break;
          case 0x55: std::copy_n(begin(VX), x + 1, begin(ram) + I); if (load_store) { I += (x + 1); } break;
          case 0x65: std::copy_n(begin(ram) + I, x + 1, begin(VX)); if (load_store) { I += (x + 1); } break;
        }
        break;
    }
    PC += 2;

    if (delay_timer > 0) { --delay_timer; }
    if (sound_timer > 0) { std::cout << '\a'; --sound_timer; }

    if (frame_changed) {
      frame_changed = false;
      std::cout << std::string('\n', 10);

      for (const auto &line : screen)
        for (std::cout << '\n'; bool pixel : line)
          std::cout << (pixel ? "\u2b1c" : "\u2b1b");
    }

    sleep_for(16ms);
  }
  return 0;
}

