#pragma once

// Raw TCP port 9100 (JetDirect / "AppSocket"): everything a client sends
// until it closes is one job. What CUPS' socket:// backend and many label
// tools speak. Runs in a task of its own, so a job here and an HTTP request
// can meet, and the second one is told "busy".
void rawPortBegin();
