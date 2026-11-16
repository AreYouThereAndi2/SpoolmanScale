#include "raw_port.h"

#include <WiFi.h>

#include "bridge.h"
#include "printer.h"

#define RAW_PORT        9100
// A client that sends nothing for this long is taken to be done. Some keep
// the socket open after the last byte instead of closing it.
#define RAW_IDLE_MS     30000
#define RAW_TASK_STACK  6144

static void rawTask(void*) {
  NetworkServer srv(RAW_PORT);
  srv.begin();
  srv.setNoDelay(true);
  static uint8_t buf[1024];
  for (;;) {
    NetworkClient c = srv.accept();
    if (!c) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    const String from = c.remoteIP().toString();
    const JobStatus st = jobBegin();
    if (st != JOB_OK) {
      // No status channel in this protocol: closing is the answer. CUPS
      // retries a refused job by itself.
      logLine("9100: job from %s refused (%s)", from.c_str(), jobErrorText(st));
      c.stop();
      continue;
    }
    logLine("9100: job from %s", from.c_str());
    uint32_t last = millis();
    for (;;) {
      const int avail = c.available();
      if (avail > 0) {
        const int n = c.read(buf, avail < (int)sizeof(buf) ? avail : (int)sizeof(buf));
        if (n > 0) {
          jobWrite(buf, (size_t)n);
          last = millis();
        }
        continue;
      }
      if (!c.connected()) break;
      if (millis() - last > RAW_IDLE_MS) {
        logLine("9100: idle for %d s, ending the job", RAW_IDLE_MS / 1000);
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    jobEnd(false);
    c.stop();
  }
}

void rawPortBegin() {
  xTaskCreatePinnedToCore(rawTask, "raw9100", RAW_TASK_STACK, nullptr, 1, nullptr, 1);
  logLine("TCP: port %d", RAW_PORT);
}
