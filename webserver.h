#pragma once

// Async HTTP server (serves the page) + WebSocket (/ws) that pushes live
// readings. Read-only in step 2. It NEVER touches I2C directly — when control
// is added (step 3) the WebSocket handlers will enqueue Commands.
void web_begin();
void web_tick();        // call in loop(): cleans up dead ws clients
void web_broadcast();   // push the current SensorState to all ws clients
