#pragma once

#include <Arduino.h>
#include <WebServer.h>

// Every setting of this scale as one JSON file, and back again.
//
// GET  /api/backup?secrets=1   the file; without secrets=1 the passwords and
//                              API keys stay out of it
// POST /api/restore            the file as the body; written, then the page
//                              offers the restart that applies it
//
// What belongs to this device rather than to its owner never travels: the
// firmware version, the result of the NFC reset probe, whether an HX711 was
// found. Those are measured again on the device the file is loaded into.
void webBackupRoutes(WebServer &srv);

// The card for a settings page. Expects webShellRestartUi() on the same page:
// a restored file takes effect on the next start.
String webBackupCard();
