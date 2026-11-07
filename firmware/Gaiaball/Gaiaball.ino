// Gaiaball v2 firmware entry point. All work happens in gaia::App and its tasks;
// see firmware/README.md and firmware/docs/architecture.md.
#include "src/App.h"

static gaia::App app;

void setup() { app.begin(); }

// The Arduino loop task has nothing to do: park it forever.
void loop() { vTaskDelay(portMAX_DELAY); }
