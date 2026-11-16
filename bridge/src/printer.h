#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================
//  PRINTER LINK (Bluetooth Classic SPP)
//
//  One job at a time, from whichever task gets the lock first:
//  the HTTP server on the loop task or the port 9100 task. The
//  link is opened on demand, kept for a minute after the last
//  job so a run of labels does not pay the connect each time,
//  then closed: an open link keeps the printer's radio awake and
//  drains its battery.
// ============================================================

enum JobStatus : uint8_t {
  JOB_OK,
  JOB_BUSY,             // another job or a scan holds the link
  JOB_NO_PRINTER,       // no address chosen
  JOB_CONNECT_FAILED,
  JOB_WRITE_FAILED,     // link lost, queue stuck or 30 s deadline passed
  JOB_ABORTED,          // the sender went away before the end
};

struct JobResult {
  JobStatus status;
  size_t    bytes;
  uint32_t  ms;
};

// Brings up the Bluetooth stack as SPP master. Once, from setup().
void printerBegin();

// Closes an idle link. From loop(), every pass.
void printerLoop();

// A job in three steps, so a body can be streamed through without holding it
// in RAM (no PSRAM: 256 KiB would not fit). jobBegin() takes the lock and
// connects; on anything but JOB_OK the lock is not held and jobEnd() must not
// be called. jobWrite() and jobEnd() run on the task that called jobBegin(),
// since a FreeRTOS mutex belongs to the task that took it.
JobStatus jobBegin();
bool      jobWrite(const uint8_t* data, size_t len);
JobResult jobEnd(bool aborted);

// A whole job from one buffer: begin, write, end.
JobResult printBuffer(const uint8_t* data, size_t len);

// The built-in test label as a TSPL job.
String buildTestLabel(const String& ip, const String& host);

// The HTTP status and the "error" text the API uses for a result.
int         jobHttpStatus(JobStatus s);
const char* jobErrorText(JobStatus s);

// Classic inquiry, about 10 s. False when the link is busy.
bool printerScan(JsonArray devices);

// Stores a new printer (address "" forgets it and its pairing) and closes
// the link, so the next job uses the new values. Under the job lock, since the
// port 9100 task reads the settings: false, and nothing stored, while a job or
// a scan is running.
bool printerSetConfig(const String& addr, const String& name, const String& pin);

// For /api/status.
void     printerStatusJson(JsonObject printer);
bool     printerBusy();
uint32_t printerJobCount();
// False before the first job since boot.
bool     printerLastJobJson(JsonObject last);
