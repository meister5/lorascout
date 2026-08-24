// Cardputer ADV keyboard.
//
// Isolated behind this interface on purpose: the keyboard is the one part of
// the ADV whose library surface differs most from the original Cardputer, so if
// anything needs adjusting against the installed M5 libraries it is here and
// nowhere else.
#pragma once

#include <cstdint>

namespace lorascout {
namespace hal {

enum class Key : uint8_t {
    None = 0,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Back,        // ESC / delete
    Space,
    Char,        // see lastChar()
};

class Keys {
public:
    bool begin();
    // Poll once per UI frame. Returns Key::None when nothing was pressed.
    Key poll();
    char lastChar() const { return lastChar_; }

private:
    char lastChar_ = 0;
};

}  // namespace hal
}  // namespace lorascout
