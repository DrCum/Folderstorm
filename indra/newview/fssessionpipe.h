/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * SPDX-License-Identifier: LGPL-2.1-or-later
 * $/LicenseInfo$
 */
#ifndef FS_SESSION_PIPE_H
#define FS_SESSION_PIPE_H
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "fssessionprotocol.h"
#include <limits>

namespace fs_session
{
// Liveness hint on a PID-validated worker window. Authentication stays on the
// inherited pipe; this lets rollback wait for a lost peer's input revocation.
constexpr const wchar_t* InputLeaseProperty = L"FolderstormSessionPrototypeInputOwner";
// One read and one write in flight; owned buffers outlive overlapped operations.
// No blocking I/O, worker threads or unbounded message queues in either UI loop.
class Pipe
{
public:
    Pipe() = default;
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    ~Pipe() { close(); }
    bool open(HANDLE handle)
    {
        close();
        mHandle = handle;
        if (handle == INVALID_HANDLE_VALUE || !handle || GetFileType(handle) != FILE_TYPE_PIPE)
        { close(); return false; }
        if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0)) { close(); return false; }
        mRead.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        mWrite.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!mRead.hEvent || !mWrite.hEvent) { close(); return false; }
        return true;
    }
    bool alive() const { return mHandle != INVALID_HANDLE_VALUE; }
    bool writing() { progressWrite(); return mWriting; }
    bool send(const Message& message)
    {
        progressWrite();
        if (!alive() || mWriting || !encode(message, mOut)) return false;
        mWritten = 0;
        startWrite();
        return alive();
    }
    bool receive(Message& message)
    {
        progressWrite();
        if (!alive()) return false;
        if (!mReading) startRead();
        if (!alive()) return false;
        if (mReading)
        {
            DWORD count = 0;
            if (!GetOverlappedResult(mHandle, &mRead, &count, FALSE))
            {
                if (GetLastError() != ERROR_IO_INCOMPLETE) close();
                return false;
            }
            mReading = false;
            if (!count) { close(); return false; }
            mReadSize += count;
        }
        if (mReadSize != FrameSize) return false;
        mReadSize = 0;
        if (!decode(mIn, message)) { close(); return false; }
        return true;
    }
    void close()
    {
        if (alive())
        {
            CancelIoEx(mHandle, nullptr);
            DWORD ignored = 0;
            // Drain cancellation before destroying OVERLAPPED storage/events.
            if (mReading) GetOverlappedResult(mHandle, &mRead, &ignored, TRUE);
            if (mWriting) GetOverlappedResult(mHandle, &mWrite, &ignored, TRUE);
            CloseHandle(mHandle);
        }
        if (mRead.hEvent) CloseHandle(mRead.hEvent);
        if (mWrite.hEvent) CloseHandle(mWrite.hEvent);
        mHandle = INVALID_HANDLE_VALUE;
        mRead = {}; mWrite = {};
        mReading = mWriting = mWritePending = false;
        mReadSize = mWritten = 0;
    }
private:
    void startRead()
    {
        ResetEvent(mRead.hEvent);
        DWORD count = 0;
        if (ReadFile(mHandle, mIn.data() + mReadSize, static_cast<DWORD>(FrameSize - mReadSize), &count, &mRead))
        { if (!count) close(); else mReadSize += count; }
        else if (GetLastError() == ERROR_IO_PENDING) mReading = true;
        else close();
    }
    void startWrite()
    {
        ResetEvent(mWrite.hEvent);
        DWORD count = 0;
        if (WriteFile(mHandle, mOut.data() + mWritten, static_cast<DWORD>(FrameSize - mWritten), &count, &mWrite))
        {
            if (!count) { close(); return; }
            mWritten += count;
            mWriting = mWritten != FrameSize;
            // An incomplete immediate write is continued on the next poll.
            mWritePending = false;
        }
        else if (GetLastError() == ERROR_IO_PENDING) { mWriting = true; mWritePending = true; }
        else close();
    }
    void progressWrite()
    {
        if (!alive() || !mWriting) return;
        if (mWritePending)
        {
            DWORD count = 0;
            if (!GetOverlappedResult(mHandle, &mWrite, &count, FALSE))
            {
                if (GetLastError() != ERROR_IO_INCOMPLETE) close();
                return;
            }
            if (!count) { close(); return; }
            mWritten += count;
        }
        mWriting = false; mWritePending = false;
        if (mWritten != FrameSize) startWrite();
    }
    HANDLE mHandle = INVALID_HANDLE_VALUE;
    OVERLAPPED mRead{}, mWrite{};
    Frame mIn{}, mOut{};
    DWORD mReadSize = 0, mWritten = 0;
    bool mReading = false, mWriting = false, mWritePending = false;
};
}
#endif
#endif
