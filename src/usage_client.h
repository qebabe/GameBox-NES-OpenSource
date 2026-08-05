#pragma once

// Usage reporting is best-effort and never blocks the caller. Events are only
// sent while Wi-Fi is connected and do not contain SSIDs, raw MACs, or paths.
bool usageReportOnline();
bool usageReportEvent(const char* eventName);
bool usageReportEvent(const char* eventName, const char* propertyName,
                      const char* propertyValue);
