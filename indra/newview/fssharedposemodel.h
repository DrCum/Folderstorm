/**
 * @file fssharedposemodel.h
 * @brief Portable consent, reservations, history and framing for shared posing.
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */
#ifndef FS_SHARED_POSE_MODEL_H
#define FS_SHARED_POSE_MODEL_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace FSSharedPose
{
constexpr std::size_t MAX_JOINTS = 512;
constexpr std::size_t MAX_PACKET = 800;
constexpr std::size_t FRAGMENT_SIZE = 600;
constexpr std::size_t MAX_FRAGMENTS = 128;
constexpr double LEASE_SECONDS = 5.;
constexpr double PEER_SECONDS = 10.;

struct Joint
{
    // Quaternion, local position, local scale. Effective values, not UI deltas.
    std::array<float, 10> transform{{0, 0, 0, 1, 0, 0, 0, 1, 1, 1}};
    std::uint8_t priority = 7;
    bool enabled = true;
    bool worldLocked = false;
    bool mirrored = false;
    bool operator==(const Joint& other) const
    {
        return transform == other.transform && priority == other.priority && enabled == other.enabled &&
               worldLocked == other.worldLocked && mirrored == other.mirrored;
    }
    bool operator!=(const Joint& other) const { return !(*this == other); }
};
using Pose = std::map<std::string, Joint>;
using JointSet = std::set<std::string>;

inline bool validName(const std::string& name)
{
    return !name.empty() && name.size() <= 64 &&
        name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") == std::string::npos;
}

inline bool validJoint(const Joint& joint)
{
    double length = 0.;
    for (std::size_t i = 0; i < joint.transform.size(); ++i)
    {
        if (!std::isfinite(joint.transform[i])) return false;
        if (i >= 4 && std::abs(joint.transform[i]) > 1024.f) return false;
        if (i < 4) length += static_cast<double>(joint.transform[i]) * joint.transform[i];
    }
    return length > .5 && length < 1.5 && joint.priority <= 7;
}

inline bool validPose(const Pose& pose)
{
    if (pose.empty() || pose.size() > MAX_JOINTS) return false;
    for (const auto& item : pose)
        if (!validName(item.first) || !validJoint(item.second)) return false;
    return true;
}

// Compact little-endian records keep a complete Bento skeleton affordable.
inline std::string encodePose(const Pose& pose)
{
    if (!validPose(pose)) return {};
    std::string bytes;
    for (const auto& item : pose)
    {
        bytes.push_back(static_cast<char>(item.first.size()));
        bytes += item.first;
        bytes.push_back(static_cast<char>(item.second.priority));
        bytes.push_back(static_cast<char>((item.second.enabled ? 1 : 0) | (item.second.worldLocked ? 2 : 0) |
                                         (item.second.mirrored ? 4 : 0)));
        for (float value : item.second.transform)
        {
            std::uint32_t bits;
            std::memcpy(&bits, &value, sizeof(bits));
            for (unsigned shift = 0; shift < 32; shift += 8)
                bytes.push_back(static_cast<char>((bits >> shift) & 255));
        }
    }
    return bytes;
}

inline std::optional<Pose> decodePose(const std::string& bytes)
{
    Pose result;
    std::size_t offset = 0;
    while (offset < bytes.size())
    {
        const auto length = static_cast<unsigned char>(bytes[offset++]);
        if (!length || length > 64 || offset + length + 42 > bytes.size()) return {};
        const std::string name = bytes.substr(offset, length);
        offset += length;
        Joint joint;
        joint.priority = static_cast<std::uint8_t>(bytes[offset++]);
        const auto enabled = static_cast<unsigned char>(bytes[offset++]);
        if (enabled > 7) return {};
        joint.enabled = (enabled & 1) != 0;
        joint.worldLocked = (enabled & 2) != 0;
        joint.mirrored = (enabled & 4) != 0;
        for (float& value : joint.transform)
        {
            std::uint32_t bits = 0;
            for (unsigned shift = 0; shift < 32; shift += 8)
                bits |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset++])) << shift;
            std::memcpy(&value, &bits, sizeof(value));
        }
        if (!validName(name) || !validJoint(joint) || !result.emplace(name, joint).second || result.size() > MAX_JOINTS)
            return {};
    }
    if (result.empty()) return {};
    return result;
}

class OwnerSession
{
public:
    struct Reservation { bool granted = false; std::string occupiedBy; };
    void start(std::string owner, std::string session, Pose pose)
    {
        clear();
        if (!validPose(pose)) return;
        mOwner = std::move(owner);
        mSession = std::move(session);
        mPose = std::move(pose);
    }
    void clear() { *this = OwnerSession(); }
    bool active() const { return !mSession.empty(); }
    const std::string& session() const { return mSession; }
    const Pose& pose() const { return mPose; }
    std::uint32_t revision() const { return mRevision; }
    void grant(const std::string& editor, const std::string& token, double now)
    {
        if (active() && !editor.empty() && !token.empty() && editor != mOwner)
            mEditors[editor] = {token, now + PEER_SECONDS};
    }
    bool authorized(const std::string& editor, const std::string& token, double now) const
    {
        if (!active()) return false;
        if (editor == mOwner) return token.empty();
        const auto found = mEditors.find(editor);
        return found != mEditors.end() && found->second.token == token && found->second.expires > now;
    }
    bool heartbeat(const std::string& editor, const std::string& token, double now)
    {
        if (!authorized(editor, token, now)) return false;
        const auto found = mEditors.find(editor);
        if (found != mEditors.end()) found->second.expires = now + PEER_SECONDS;
        return true;
    }
    void revoke(const std::string& editor)
    {
        mEditors.erase(editor);
        for (auto it = mLeases.begin(); it != mLeases.end();)
            if (it->second.editor == editor) it = mLeases.erase(it); else ++it;
    }
    std::vector<std::string> editors() const
    {
        std::vector<std::string> result;
        for (const auto& item : mEditors) result.push_back(item.first);
        return result;
    }
    void expire(double now)
    {
        for (auto it = mEditors.begin(); it != mEditors.end();)
        {
            if (it->second.expires <= now) { const auto editor = it->first; ++it; revoke(editor); }
            else ++it;
        }
        for (auto it = mLeases.begin(); it != mLeases.end();)
            if (it->second.expires <= now) it = mLeases.erase(it); else ++it;
    }
    Reservation acquire(const std::string& editor, const std::string& token, const std::string& lease,
                        const JointSet& joints, double now)
    {
        expire(now);
        if (!authorized(editor, token, now) || lease.empty() || joints.empty() || joints.size() > MAX_JOINTS)
            return {};
        for (const auto& joint : joints)
        {
            if (!mPose.count(joint)) return {};
            for (const auto& item : mLeases)
                if (item.second.joints.count(joint) && (item.first != lease || item.second.editor != editor))
                    return {false, item.second.editor};
        }
        auto existing = mLeases.find(lease);
        if (existing != mLeases.end())
        {
            if (existing->second.editor != editor || existing->second.joints != joints) return {};
            existing->second.expires = now + LEASE_SECONDS;
            return {true, {}};
        }
        Lease entry{editor, joints, now + LEASE_SECONDS, {}};
        for (const auto& joint : joints) entry.before.emplace(joint, mPose.at(joint));
        mLeases.emplace(lease, std::move(entry));
        return {true, {}};
    }
    bool renew(const std::string& editor, const std::string& token, const std::string& lease, double now)
    {
        const auto found = mLeases.find(lease);
        if (!authorized(editor, token, now) || found == mLeases.end() || found->second.editor != editor ||
            found->second.expires <= now) return false;
        found->second.expires = now + LEASE_SECONDS;
        return true;
    }
    bool apply(const std::string& editor, const std::string& token, const std::string& lease,
               const Pose& changes, bool final, double now, bool record = true, std::uint32_t sequence = 0)
    {
        auto found = mLeases.find(lease);
        if (!authorized(editor, token, now) || found == mLeases.end() || found->second.editor != editor ||
            found->second.expires <= now || !validPose(changes) ||
            (sequence && sequence <= found->second.sequence)) return false;
        for (const auto& item : changes)
            if (!found->second.joints.count(item.first)) return false;
        ++mRevision;
        if (sequence) found->second.sequence = sequence;
        for (const auto& item : changes)
        {
            if (mPose.at(item.first) != item.second)
            {
                mPose[item.first] = item.second;
                mJointRevisions[item.first] = mRevision;
            }
        }
        if (final)
        {
            if (record)
            {
                History entry;
                for (const auto& item : found->second.before)
                {
                    if (mPose.at(item.first) == item.second) continue;
                    entry.before.emplace(item);
                    entry.after.emplace(item.first, mPose.at(item.first));
                    entry.expected[item.first] = mJointRevisions[item.first];
                }
                if (!entry.before.empty())
                {
                    auto& history = mHistory[editor];
                    history.undo.push_back(std::move(entry));
                    if (history.undo.size() > 20) history.undo.erase(history.undo.begin());
                    history.redo.clear();
                }
            }
            mLeases.erase(found);
        }
        return true;
    }
    JointSet historyJoints(const std::string& editor, bool redo) const
    {
        JointSet result;
        auto found = mHistory.find(editor);
        if (found == mHistory.end()) return result;
        const auto& stack = redo ? found->second.redo : found->second.undo;
        if (!stack.empty()) for (const auto& item : stack.back().before) result.insert(item.first);
        return result;
    }
    bool restore(const std::string& editor, const std::string& token, const std::string& lease, bool redo, double now)
    {
        auto found = mHistory.find(editor);
        if (found == mHistory.end()) return false;
        auto& source = redo ? found->second.redo : found->second.undo;
        auto& destination = redo ? found->second.undo : found->second.redo;
        if (source.empty()) return false;
        History entry = source.back();
        for (const auto& expected : entry.expected)
            if (mJointRevisions[expected.first] != expected.second) return false;
        if (!apply(editor, token, lease, redo ? entry.after : entry.before, true, now, false)) return false;
        for (auto& expected : entry.expected) expected.second = mJointRevisions[expected.first];
        source.pop_back();
        if (!source.empty())
        {
            const auto& current = redo ? source.back().before : source.back().after;
            for (auto& expected : source.back().expected)
                if (entry.expected.count(expected.first) && current.at(expected.first) == mPose.at(expected.first))
                    expected.second = mJointRevisions[expected.first];
        }
        destination.push_back(std::move(entry));
        return true;
    }
    void release(const std::string& editor, const std::string& lease)
    {
        const auto found = mLeases.find(lease);
        if (found != mLeases.end() && found->second.editor == editor) mLeases.erase(found);
    }
private:
    struct Editor { std::string token; double expires; };
    struct Lease { std::string editor; JointSet joints; double expires; Pose before; std::uint32_t sequence = 0; };
    struct History { Pose before, after; std::map<std::string, std::uint32_t> expected; };
    struct Stacks { std::vector<History> undo, redo; };
    std::string mOwner, mSession;
    Pose mPose;
    std::uint32_t mRevision = 0;
    std::map<std::string, Editor> mEditors;
    std::map<std::string, Lease> mLeases;
    std::map<std::string, std::uint32_t> mJointRevisions;
    std::map<std::string, Stacks> mHistory;
};

inline std::vector<std::string> fragment(const std::string& id, const std::string& payload)
{
    if (id.size() != 36 || payload.empty() || payload.size() > FRAGMENT_SIZE * MAX_FRAGMENTS) return {};
    std::vector<std::string> result;
    const auto count = (payload.size() + FRAGMENT_SIZE - 1) / FRAGMENT_SIZE;
    for (std::size_t i = 0; i < count; ++i)
        result.push_back("FSP1:" + id + ":" + std::to_string(i) + ":" + std::to_string(count) + ":" +
                         payload.substr(i * FRAGMENT_SIZE, FRAGMENT_SIZE));
    return result;
}

class Assembler
{
public:
    std::optional<std::string> receive(const std::string& sender, const std::string& packet, double now)
    {
        expire(now);
        if (sender.empty() || packet.size() > MAX_PACKET || packet.compare(0, 5, "FSP1:") != 0) return {};
        auto a = packet.find(':', 5), b = packet.find(':', a == std::string::npos ? a : a + 1);
        auto c = packet.find(':', b == std::string::npos ? b : b + 1);
        if (a != 41 || b == std::string::npos || c == std::string::npos) return {};
        auto index = number(packet.substr(a + 1, b - a - 1)), count = number(packet.substr(b + 1, c - b - 1));
        if (!index || !count || *count == 0 || *count > MAX_FRAGMENTS || *index >= *count ||
            packet.size() <= c + 1 || packet.size() - c - 1 > FRAGMENT_SIZE) return {};
        const std::string key = sender + packet.substr(5, 36);
        if (mCompleted.count(key)) return {};
        if (!mFrames.count(key) && mFrames.size() >= 64) return {};
        auto& frame = mFrames[key];
        if (frame.parts.empty()) { frame.parts.resize(*count); frame.expires = now + 15.; }
        if (frame.parts.size() != *count) { mFrames.erase(key); return {}; }
        auto& part = frame.parts[*index];
        const auto data = packet.substr(c + 1);
        if (!part.empty() && part != data) { mFrames.erase(key); return {}; }
        part = data;
        std::string result;
        for (const auto& item : frame.parts) { if (item.empty()) return {}; result += item; }
        mFrames.erase(key);
        if (mCompleted.size() >= 256) mCompleted.erase(mCompleted.begin());
        mCompleted[key] = now + 30.;
        return result;
    }
    void clear() { mFrames.clear(); mCompleted.clear(); }
private:
    struct Frame { std::vector<std::string> parts; double expires = 0.; };
    std::map<std::string, Frame> mFrames;
    std::map<std::string, double> mCompleted;
    static std::optional<std::size_t> number(const std::string& text)
    {
        if (text.empty() || text.size() > 3 || text.find_first_not_of("0123456789") != std::string::npos) return {};
        std::size_t result = 0;
        for (char value : text) result = result * 10 + static_cast<unsigned>(value - '0');
        return result;
    }
    void expire(double now)
    {
        for (auto it = mFrames.begin(); it != mFrames.end();)
            if (it->second.expires <= now) it = mFrames.erase(it); else ++it;
        for (auto it = mCompleted.begin(); it != mCompleted.end();)
            if (it->second <= now) it = mCompleted.erase(it); else ++it;
    }
};
}
#endif
