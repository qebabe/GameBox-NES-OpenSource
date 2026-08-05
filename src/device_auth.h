#pragma once

#include <Arduino.h>

class HTTPClient;

// Registers a per-device random token over TLS. The server stores only its
// SHA-256 hash. Registration is idempotent across interrupted requests.
bool deviceAuthEnsureRegistered(String& error);
void deviceAuthAddHeaders(HTTPClient& http);
void deviceAuthHandleHttpStatus(int statusCode);
bool deviceAuthIsRegistered();
