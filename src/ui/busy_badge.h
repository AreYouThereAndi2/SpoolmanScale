#pragma once

// ============================================================
//  THE BUSY BADGE
//
//  A small loading ring in the bottom corner while the loop waits on the network.
//  Those waits are blocking calls: touch is not read and nothing moves until
//  they return, and a screen that stops answering with nothing to say why
//  reads as a hang. The badge is drawn once, before the wait, and taken down
//  after it - it cannot turn while the call is open, nothing runs then, but it
//  says "the scale is waiting for something" from the first moment.
//
//  Opened and closed by the outermost httpStall bracket
//  (services/http_progress.h); nothing else calls it.
// ============================================================

void busyBadgeShow();
void busyBadgeHide();
