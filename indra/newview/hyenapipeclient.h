
#pragma once

#include <functional>
#include <string>

namespace HyenaPipeClient
{
// Opens the launcher connection.
bool Connect(const std::wstring& pipeName);

// Preserves the existing startup handshake.
bool SendViewerReady(const std::wstring& pipeName);

// Sends the viewer's MFA challenge to the launcher.
bool SendMFAChallenge(const std::string& message);

// Registers the callback for a response from the launcher.
void SetMFAResponseCallback(std::function<void(const std::string&, bool)> callback);

// Closes the launcher connection and stops its reader.
void Disconnect();
} // namespace HyenaPipeClient
