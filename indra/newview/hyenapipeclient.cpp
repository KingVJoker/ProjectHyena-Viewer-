
#include "llviewerprecompiledheaders.h"
#include "hyenapipeclient.h"

#ifdef _WIN32
#include <Windows.h>
#endif

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace HyenaPipeClient
{
#ifdef _WIN32

static HANDLE      gPipe = INVALID_HANDLE_VALUE;
static std::thread gReaderThread;
static std::mutex  gPipeMutex;
static std::mutex  gCallbackMutex;

static std::function<void(const std::string&, bool)> gMfaCallback;

static std::atomic<bool> gStopping{ false };
static std::atomic<bool> gMfaChallengePending{ false };

static constexpr DWORD  WriteTimeoutMs   = 3000;
static constexpr size_t MaxMessageLength = 1024;

// Automatically close a Windows event handle.
struct ScopedEvent
{
    HANDLE handle = nullptr;

    ScopedEvent() { handle = CreateEventW(nullptr, TRUE, FALSE, nullptr); }

    ~ScopedEvent()
    {
        if (handle)
            CloseHandle(handle);
    }

    ScopedEvent(const ScopedEvent&)            = delete;
    ScopedEvent& operator=(const ScopedEvent&) = delete;
};

// Write using overlapped I/O.
static bool WriteOverlapped(HANDLE pipe, const std::string& line)
{
    if (pipe == INVALID_HANDLE_VALUE || line.empty())
        return false;

    ScopedEvent event;

    if (!event.handle)
        return false;

    OVERLAPPED overlapped = {};
    overlapped.hEvent     = event.handle;

    DWORD written  = 0;
    DWORD expected = static_cast<DWORD>(line.size());

    BOOL success = WriteFile(pipe, line.data(), expected, &written, &overlapped);

    if (!success)
    {
        DWORD error = GetLastError();

        if (error != ERROR_IO_PENDING)
        {
            LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_FAILED error=" << error << LL_ENDL;
            return false;
        }

        DWORD waitResult = WaitForSingleObject(event.handle, WriteTimeoutMs);

        if (waitResult != WAIT_OBJECT_0)
        {
            LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_WAIT_FAILED result=" << waitResult << LL_ENDL;

            // Request cancellation, then wait until the
            // operation has actually completed or cancelled.
            CancelIoEx(pipe, &overlapped);

            DWORD cancelledBytes = 0;

            GetOverlappedResult(pipe, &overlapped, &cancelledBytes, TRUE);

            return false;
        }

        success = GetOverlappedResult(pipe, &overlapped, &written, FALSE);
    }

    if (!success || written != expected)
    {
        LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_INCOMPLETE"
                              << " written=" << written << " expected=" << expected << LL_ENDL;
        return false;
    }

    return true;
}

// Send a newline-delimited message.
static bool WriteMessage(const std::string& message)
{
    LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_ENTER"
                          << " message_length=" << message.size() << LL_ENDL;

    std::lock_guard<std::mutex> lock(gPipeMutex);

    if (gStopping.load() || gPipe == INVALID_HANDLE_VALUE)
    {
        LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_FAILED"
                              << " reason=not_connected" << LL_ENDL;

        return false;
    }

    const std::string line = message + "\n";

    LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_BEGIN"
                          << " expected_bytes=" << line.size() << LL_ENDL;

    bool success = WriteOverlapped(gPipe, line);

    LL_WARNS("HyenaPipe") << "HYENA_PIPE_WRITE_RETURN"
                          << " success=" << (success ? 1 : 0) << LL_ENDL;

    return success;
}

// Read one newline-delimited message.
// Each read uses its own overlapped event.
static bool ReadLine(HANDLE pipe, std::string& message)
{
    message.clear();

    ScopedEvent event;

    if (!event.handle)
        return false;

    while (!gStopping.load() && message.size() < MaxMessageLength)
    {
        char  ch        = '\0';
        DWORD bytesRead = 0;

        ResetEvent(event.handle);

        OVERLAPPED overlapped = {};
        overlapped.hEvent     = event.handle;

        BOOL success = ReadFile(pipe, &ch, 1, &bytesRead, &overlapped);

        if (!success)
        {
            DWORD error = GetLastError();

            if (error != ERROR_IO_PENDING)
                return false;

            success = GetOverlappedResult(pipe, &overlapped, &bytesRead, TRUE);
        }

        if (!success || bytesRead == 0)
            return false;

        if (gStopping.load())
            return false;

        if (ch == '\n')
        {
            if (!message.empty() && message.back() == '\r')
            {
                message.pop_back();
            }

            return true;
        }

        message.push_back(ch);
    }

    return false;
}

// Process MFA responses on the reader thread.
static void ReaderLoop(HANDLE pipe)
{
    while (!gStopping.load())
    {
        std::string message;

        if (!ReadLine(pipe, message))
            break;

        constexpr char Prefix[] = "MFA_RESPONSE|";

        if (message.compare(0, sizeof(Prefix) - 1, Prefix) != 0)
        {
            continue;
        }

        const std::string payload = message.substr(sizeof(Prefix) - 1);

        const size_t separator = payload.find('|');

        if (separator == std::string::npos)
            continue;

        const std::string code = payload.substr(0, separator);

        const std::string rememberText = payload.substr(separator + 1);

        if (code.size() != 6 || rememberText.size() != 1 || (rememberText != "0" && rememberText != "1"))
        {
            continue;
        }

        bool validCode = true;

        for (char ch : code)
        {
            if (ch < '0' || ch > '9')
            {
                validCode = false;
                break;
            }
        }

        if (!validCode || !gMfaChallengePending.exchange(false))
        {
            continue;
        }

        std::function<void(const std::string&, bool)> callback;

        {
            std::lock_guard<std::mutex> lock(gCallbackMutex);

            callback = gMfaCallback;
        }

        if (callback && !gStopping.load())
        {
            callback(code, rememberText == "1");
        }
    }

    LL_INFOS("HyenaPipe") << "HYENA_PIPE_READER_EXIT" << LL_ENDL;
}

#endif

bool Connect(const std::wstring& pipeName)
{
#ifdef _WIN32

    if (pipeName.empty())
        return false;

    std::lock_guard<std::mutex> lock(gPipeMutex);

    if (gPipe != INVALID_HANDLE_VALUE)
        return !gStopping.load();

    // A previous reader must be joined by Disconnect
    // before creating another connection.
    if (gReaderThread.joinable())
        return false;

    const std::wstring fullPath = L"\\\\.\\pipe\\" + pipeName;

    HANDLE pipe = CreateFileW(fullPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

    if (pipe == INVALID_HANDLE_VALUE)
    {
        LL_WARNS("HyenaPipe") << "HYENA_PIPE_CONNECT_FAILED error=" << GetLastError() << LL_ENDL;

        return false;
    }

    // Preserve the existing launcher handshake.
    const std::string ready = "VIEWER_READY\n";

    if (!WriteOverlapped(pipe, ready))
    {
        CloseHandle(pipe);
        return false;
    }

    gStopping            = false;
    gMfaChallengePending = false;
    gPipe                = pipe;

    gReaderThread = std::thread([pipe]() { ReaderLoop(pipe); });

    LL_INFOS("HyenaPipe") << "HYENA_PIPE_CONNECTED_OVERLAPPED" << LL_ENDL;

    return true;

#else
    (void)pipeName;
    return false;
#endif
}

bool SendViewerReady(const std::wstring& pipeName)
{ return Connect(pipeName); }

bool SendMFAChallenge(const std::string& message)
{
#ifdef _WIN32

    if (message.size() > 700)
        return false;

    // Preserve the existing Base64 protocol.
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789+/";

    std::string encoded;

    encoded.reserve(((message.size() + 2) / 3) * 4);

    for (size_t i = 0; i < message.size(); i += 3)
    {
        const unsigned int a = static_cast<unsigned char>(message[i]);

        const unsigned int b = i + 1 < message.size() ? static_cast<unsigned char>(message[i + 1]) : 0;

        const unsigned int c = i + 2 < message.size() ? static_cast<unsigned char>(message[i + 2]) : 0;

        const unsigned int value = (a << 16) | (b << 8) | c;

        encoded.push_back(alphabet[(value >> 18) & 63]);

        encoded.push_back(alphabet[(value >> 12) & 63]);

        encoded.push_back(i + 1 < message.size() ? alphabet[(value >> 6) & 63] : '=');

        encoded.push_back(i + 2 < message.size() ? alphabet[value & 63] : '=');
    }

    gMfaChallengePending = true;

    if (!WriteMessage("MFA_CHALLENGE|" + encoded))
    {
        gMfaChallengePending = false;
        return false;
    }

    return true;

#else
    (void)message;
    return false;
#endif
}

void SetMFAResponseCallback(std::function<void(const std::string&, bool)> callback)
{
#ifdef _WIN32

    std::lock_guard<std::mutex> lock(gCallbackMutex);
    gMfaCallback = std::move(callback);

#else
    (void)callback;
#endif
}

void Disconnect()
{
#ifdef _WIN32

    // The current integration calls Disconnect from
    // outside the pipe reader thread.
    if (gReaderThread.joinable() && gReaderThread.get_id() == std::this_thread::get_id())
    {
        LL_WARNS("HyenaPipe") << "Disconnect must not run on reader thread." << LL_ENDL;
        return;
    }

    gStopping            = true;
    gMfaChallengePending = false;

    // Cancel outstanding reads while the handle
    // remains valid.
    {
        std::lock_guard<std::mutex> lock(gPipeMutex);

        if (gPipe != INVALID_HANDLE_VALUE)
        {
            CancelIoEx(gPipe, nullptr);
        }
    }

    // Wait for the reader to finish before closing
    // the handle it uses.
    if (gReaderThread.joinable())
    {
        gReaderThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(gPipeMutex);

        if (gPipe != INVALID_HANDLE_VALUE)
        {
            CloseHandle(gPipe);
            gPipe = INVALID_HANDLE_VALUE;
        }
    }

    LL_INFOS("HyenaPipe") << "HYENA_PIPE_DISCONNECTED" << LL_ENDL;

#endif
}

} // namespace HyenaPipeClient
