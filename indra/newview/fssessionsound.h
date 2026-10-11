/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_SOUND_H
#define FS_SESSION_SOUND_H
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
namespace fs_session
{
struct WavSound
{
    static constexpr std::size_t MaxBytes = 4*1024*1024;
    std::vector<std::uint8_t> bytes;
    std::size_t dataOffset = 0, dataBytes = 0;
    unsigned int bits = 0;
    static bool decode(std::vector<std::uint8_t> input,WavSound& output)
    {
        if (input.size() < 44 || input.size() > MaxBytes) return false;
        const auto tag = [&](std::size_t at,const char* name)
        { return at+4 <= input.size() && std::equal(name,name+4,input.begin()+static_cast<std::ptrdiff_t>(at)); };
        const auto number = [&](std::size_t at,unsigned int length)
        { std::uint32_t n = 0; for (unsigned int i = 0; i < length; ++i) n |= static_cast<std::uint32_t>(input[at+i])<<(i*8); return n; };
        if (!tag(0,"RIFF") || !tag(8,"WAVE") || number(4,4) != input.size()-8) return false;
        WavSound next; bool format = false, data = false;
        std::uint32_t rate = 0, alignment = 0, average = 0;
        for (std::size_t at = 12; at < input.size();)
        {
            if (input.size()-at < 8) return false;
            const auto length = number(at+4,4); const auto body = at+8;
            if (length > input.size()-body) return false;
            if (tag(at,"fmt "))
            {
                if (format || (length != 16 && length != 18) || number(body,2) != 1 || (length == 18 && number(body+16,2))) return false;
                const auto channels = number(body+2,2); rate = number(body+4,4); average = number(body+8,4);
                alignment = number(body+12,2); next.bits = number(body+14,2);
                if (channels < 1 || channels > 2 || rate < 8000 || rate > 48000 || (next.bits != 8 && next.bits != 16) ||
                    alignment != channels*next.bits/8 || average != rate*alignment) return false;
                format = true;
            }
            if (tag(at,"data"))
            { if (data) return false; data = true; next.dataOffset = body; next.dataBytes = length; }
            const std::size_t padded = static_cast<std::size_t>(length)+(length&1u);
            if (padded > input.size()-body) return false;
            at = body+padded;
        }
        if (!format || !data || !next.dataBytes || next.dataBytes%alignment || next.dataBytes > static_cast<std::size_t>(average)*10) return false;
        next.bytes = std::move(input); output = std::move(next); return true;
    }
    std::vector<std::uint8_t> volume(unsigned int percent) const
    {
        if (bytes.empty() || percent > 100) return {};
        auto result = bytes;
        for (std::size_t at = dataOffset; at < dataOffset+dataBytes; at += bits/8)
        {
            if (bits == 8) result[at] = static_cast<std::uint8_t>(128+(static_cast<int>(result[at])-128)*static_cast<int>(percent)/100);
            else
            {
                const unsigned int encoded = static_cast<unsigned int>(result[at]) | (static_cast<unsigned int>(result[at+1])<<8);
                const int sample = encoded < 32768 ? static_cast<int>(encoded) : static_cast<int>(encoded)-65536;
                const auto scaled = static_cast<std::uint16_t>(sample*static_cast<int>(percent)/100);
                result[at] = static_cast<std::uint8_t>(scaled&255); result[at+1] = static_cast<std::uint8_t>(scaled>>8);
            }
        }
        return result;
    }
};
inline std::vector<std::uint8_t> builtinAlertTone(unsigned int volume)
{
    constexpr std::uint32_t samples = 3528, rate = 22050;
    std::vector<std::uint8_t> bytes(44+samples*2,0);
    const auto number = [&](std::size_t at,std::uint32_t value,unsigned int size)
    { for (unsigned int i = 0; i < size; ++i) bytes[at+i] = static_cast<std::uint8_t>((value>>(8*i))&255); };
    const auto text = [&](std::size_t at,const char* value) { for (unsigned int i = 0; i < 4; ++i) bytes[at+i] = static_cast<std::uint8_t>(value[i]); };
    text(0,"RIFF"); number(4,36+samples*2,4); text(8,"WAVE"); text(12,"fmt "); number(16,16,4); number(20,1,2); number(22,1,2);
    number(24,rate,4); number(28,rate*2,4); number(32,2,2); number(34,16,2); text(36,"data"); number(40,samples*2,4);
    for (std::uint32_t i = 0; i < samples; ++i)
    {
        const double envelope = std::sin(3.141592653589793*static_cast<double>(i)/samples);
        const auto sample = static_cast<std::int16_t>(std::sin(6.283185307179586*880.0*static_cast<double>(i)/rate)*envelope*6000.0*(std::min)(100u,volume)/100.0);
        number(44+static_cast<std::size_t>(i)*2,static_cast<std::uint16_t>(sample),2);
    }
    return bytes;
}
}
#endif
