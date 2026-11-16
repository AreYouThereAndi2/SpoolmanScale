// Druckbruecke: WLAN -> Bluetooth Classic SPP bridge for the TYPONOS PM240.
// See bridge.h for the why and README.md for the API.

#include <Arduino.h>

#include "bridge.h"
#include "net.h"
#include "printer.h"
#include "raw_port.h"
#include "web.h"

// How often the free heap goes to the log. Without PSRAM it is the number to
// watch, and a slow leak shows up here long before a crash.
#define HEAP_LOG_MS (5UL * 60 * 1000)

static uint32_t s_heapLog = 0;

void setup() {
  Serial.begin(115200);
  delay(200);
  logLine("%s %s, reset reason %d", BRIDGE_NAME, BRIDGE_VERSION, (int)esp_reset_reason());
  configLoad();
  logLine("Printer: %s, hostname %s, API key %s",
          cfg.printerAddr.isEmpty() ? "none" : cfg.printerAddr.c_str(), cfg.hostname.c_str(),
          cfg.apiKey.isEmpty() ? "off" : "on");
  // Bluetooth first: its controller memory is taken in one block, which is
  // easier before WiFi has spread its buffers over the heap.
  printerBegin();
  netBegin();
  webBegin();
  rawPortBegin();
  logLine("Heap: %u free, %u largest block", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

void loop() {
  netLoop();
  webLoop();
  printerLoop();
  if (millis() - s_heapLog > HEAP_LOG_MS) {
    s_heapLog = millis();
    logLine("Heap: %u free, %u min, %u largest block", (unsigned)ESP.getFreeHeap(),
            (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  }
  delay(2);
}
