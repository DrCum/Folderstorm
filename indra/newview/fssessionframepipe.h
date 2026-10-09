/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_FRAME_PIPE_H
#define FS_SESSION_FRAME_PIPE_H
#ifdef _WIN32
#include "fssessionpreview.h"
#include <windows.h>
#include <cstring>
#include <vector>
namespace fs_session
{
// One latest frame, no video queue. Nonblocking kernel mutex avoids seqlock
// data races and torn images; a busy reader/writer drops that sample.
class FrameLane
{
public:
    static constexpr std::size_t Size = FrameSize + PreviewBytes;
    FrameLane() = default;
    FrameLane(const FrameLane&) = delete;
    FrameLane& operator=(const FrameLane&) = delete;
    ~FrameLane() { close(); }
    HANDLE mapping() const { return mMapping; }
    HANDLE mutex() const { return mMutex; }
    bool create(const WorkerId& id, DWORD host)
    {
        SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &inherit, PAGE_READWRITE, 0, static_cast<DWORD>(Size), nullptr);
        HANDLE mutex = CreateMutexW(&inherit, FALSE, nullptr);
        if (!open(mapping, mutex)) return false;
        // Parent handles need inheritance until the explicit child launch.
        if (!SetHandleInformation(mMapping, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT) ||
            !SetHandleInformation(mMutex, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) return false;
        Message initial; initial.kind = Kind::Status; initial.worker = id; initial.pid = host;
        Frame header{};
        if (!encode(initial, header)) return false;
        std::memcpy(mData, header.data(), header.size()); return true;
    }
    bool open(HANDLE mapping, HANDLE mutex)
    {
        close(); mMapping = mapping; mMutex = mutex;
        if (!mapping || mapping == INVALID_HANDLE_VALUE || !mutex || mutex == INVALID_HANDLE_VALUE) { close(); return false; }
        if (!SetHandleInformation(mapping, HANDLE_FLAG_INHERIT, 0) || !SetHandleInformation(mutex, HANDLE_FLAG_INHERIT, 0)) { close(); return false; }
        mData = static_cast<std::uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, Size));
        if (!mData) { close(); return false; }
        return true;
    }
    bool bootstrap(const WorkerId& id, DWORD host)
    {
        if (!lock()) return false;
        Frame bytes{}; std::memcpy(bytes.data(), mData, bytes.size()); Message initial;
        const bool result = decode(bytes, initial) && initial.worker == id && initial.pid == host && initial.state == State::Starting;
        return ReleaseMutex(mMutex) && result;
    }
    bool publish(const Message& message, const std::uint8_t* pixels, std::size_t size)
    {
        if (size > PreviewBytes || (size && (!pixels || size != static_cast<std::size_t>(message.width) * message.height * 4 ||
            !previewSize({message.width, message.height})))) return false;
        Frame header{};
        if (!encode(message, header) || !lock()) return false;
        if (size) std::memcpy(mData + FrameSize, pixels, size);
        // Commit metadata last; an abandoned write is invalidated by lock().
        std::memcpy(mData, header.data(), header.size());
        ReleaseMutex(mMutex); return true;
    }
    bool read(const Message& session, std::uint64_t previous, Message& message, std::vector<std::uint8_t>& pixels)
    {
        if (!lock()) return false;
        Frame header{}; std::memcpy(header.data(), mData, header.size()); Message candidate;
        const bool valid = decode(header, candidate) && previewIdentity(candidate, session) && candidate.event > previous;
        if (valid)
        {
            const auto size = static_cast<std::size_t>(candidate.width) * candidate.height * 4;
            pixels.assign(mData + FrameSize, mData + FrameSize + size); message = std::move(candidate);
        }
        ReleaseMutex(mMutex); return valid;
    }
    void close()
    {
        if (mData) UnmapViewOfFile(mData);
        if (mMapping && mMapping != INVALID_HANDLE_VALUE) CloseHandle(mMapping);
        if (mMutex && mMutex != INVALID_HANDLE_VALUE) CloseHandle(mMutex);
        mData = nullptr; mMapping = mMutex = INVALID_HANDLE_VALUE;
    }
private:
    bool lock()
    {
        if (!mData) return false;
        const DWORD result = WaitForSingleObject(mMutex, 0);
        if (result == WAIT_ABANDONED) { std::memset(mData, 0, FrameSize); ReleaseMutex(mMutex); return false; }
        return result == WAIT_OBJECT_0;
    }
    HANDLE mMapping = INVALID_HANDLE_VALUE, mMutex = INVALID_HANDLE_VALUE;
    std::uint8_t* mData = nullptr;
};
}
#endif
#endif
