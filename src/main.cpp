#include "app/app.h"

namespace {
lorascout::app::App g_app;
}

void setup() { g_app.begin(); }

void loop() { g_app.loop(); }
