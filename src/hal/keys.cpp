#include "keys.h"

#include <M5Cardputer.h>

namespace lorascout {
namespace hal {

bool Keys::begin() { return true; }

Key Keys::poll() {
    M5Cardputer.update();
    if (!M5Cardputer.Keyboard.isChange()) return Key::None;
    if (!M5Cardputer.Keyboard.isPressed()) return Key::None;

    const Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();
    lastChar_ = 0;

    if (state.enter) return Key::Enter;
    if (state.del) return Key::Back;
    if (state.space) return Key::Space;

    for (const char c : state.word) {
        // The Cardputer has no arrow cluster; the semicolon row doubles as one,
        // which is the convention every Cardputer firmware uses.
        switch (c) {
            case ';': return Key::Up;
            case '.': return Key::Down;
            case ',': return Key::Left;
            case '/': return Key::Right;
            case '`': return Key::Back;
            default: break;
        }
        lastChar_ = c;
        return Key::Char;
    }
    return Key::None;
}

}  // namespace hal
}  // namespace lorascout
