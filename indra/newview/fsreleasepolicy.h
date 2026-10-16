/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_RELEASE_POLICY_H
#define FS_RELEASE_POLICY_H
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs_release
{
inline constexpr std::size_t MaxFeedBytes=2*1024*1024, MaxReleases=32, MaxAssets=24, MaxNotes=32768;
inline constexpr std::uint64_t CheckInterval=12*60*60;
inline const std::string ReleasesPage="https://github.com/DrCum/Folderstorm/releases";
struct Version
{
    std::array<std::uint64_t,4> parts{};
    static std::optional<Version> parse(const std::string& text)
    {
        if(text.empty() || text.size()>80)return {};
        Version result;std::size_t pos=0;
        for(std::size_t i=0;i<4;++i)
        {
            const auto start=pos;std::uint64_t number=0;
            while(pos<text.size() && text[pos]>='0' && text[pos]<='9')
            {
                const auto digit=static_cast<unsigned int>(text[pos++]-'0');
                if(number>(std::numeric_limits<std::uint64_t>::max()-digit)/10)return {};
                number=number*10+digit;
            }
            if(pos==start || (pos-start>1 && text[start]=='0'))return {};
            result.parts[i]=number;
            if(i<3){if(pos>=text.size() || text[pos++]!='.')return {};}
        }
        return pos==text.size()?std::optional<Version>(result):std::nullopt;
    }
    bool operator<(const Version& other)const{return parts<other.parts;}
    bool operator==(const Version& other)const{return parts==other.parts;}
};
inline bool channelValid(const std::string& value){return value=="stable" || value=="nightly";}
inline bool hex(const std::string& value,std::size_t size)
{return value.size()==size && value.find_first_not_of("0123456789abcdef")==std::string::npos;}
inline std::string host(const std::string& url)
{
    if(url.size()>2048 || url.compare(0,8,"https://")!=0 || url.find_first_of("\\\r\n\t ")!=std::string::npos)return {};
    const auto end=url.find_first_of("/?#",8);const auto result=url.substr(8,end==std::string::npos?end:end-8);
    if(result.empty() || result.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-")!=std::string::npos)return {};
    return result;
}
inline bool trustedUrl(const std::string& url,const std::string& extraHosts={})
{
    const auto name=host(url);if(name.empty())return false;
    if(name=="github.com")return url==ReleasesPage || url.compare(0,ReleasesPage.size()+1,ReleasesPage+"/")==0;
    std::istringstream input(extraHosts);std::string allowed;
    while(std::getline(input,allowed,','))if(!allowed.empty() && name==allowed)return true;
    return false;
}
inline std::string plainText(std::string value,std::size_t limit=MaxNotes)
{
    value.erase(std::remove_if(value.begin(),value.end(),[](unsigned char c){return (c<32 && c!='\n' && c!='\t') || c==127;}),value.end());
    if(value.size()>limit)value.resize(limit);
    return value;
}
struct Artifact
{
    std::string platform,architecture,cpu,flavor,installer,url,sha256;
    std::uint64_t bytes=0;
};
struct Release
{
    std::string version,channel,commit,url,notes;
    std::vector<Artifact> assets;
};
struct Build
{
    std::string version,platform,architecture,cpu,flavor,installer;
};
inline bool matches(const Artifact& asset,const Build& build)
{
    return asset.platform==build.platform && asset.architecture==build.architecture && asset.cpu==build.cpu &&
        asset.flavor==build.flavor && asset.installer==build.installer;
}
enum class State{NoReleases,Current,Available,NoArtifact};
struct Result
{
    State state=State::NoReleases;Release release;std::optional<Artifact> artifact;std::string error;
};
struct Preferences
{
    bool automatic=true;std::string channel="stable",skipped;std::uint64_t lastCheck=0;
    bool due(std::uint64_t now)const{return automatic && (now<lastCheck || now-lastCheck>=CheckInterval);}
    std::string encode()const
    {return "1\n"+std::to_string(automatic)+"\n"+channel+"\n"+skipped+"\n"+std::to_string(lastCheck)+"\n";}
    static bool decode(const std::string& bytes,Preferences& output)
    {
        if(bytes.size()>256)return false;
        std::istringstream input(bytes);std::string schema,enabled,channel,skip,last,extra;
        if(!std::getline(input,schema)||!std::getline(input,enabled)||!std::getline(input,channel)||
           !std::getline(input,skip)||!std::getline(input,last)||std::getline(input,extra) || schema!="1" ||
           (enabled!="0" && enabled!="1") || !channelValid(channel) || (!skip.empty() && !Version::parse(skip)) ||
           last.empty() || last.size()>20 || last.find_first_not_of("0123456789")!=std::string::npos)return false;
        std::uint64_t stamp=0;for(const auto c:last){const auto digit=static_cast<unsigned int>(c-'0');if(stamp>(std::numeric_limits<std::uint64_t>::max()-digit)/10)return false;stamp=stamp*10+digit;}
        output={enabled=="1",channel,skip,stamp};return true;
    }
};
inline Result choose(const std::vector<Release>& releases,const Build& build,const Preferences& prefs,bool manual,const std::string& extraHosts={})
{
    Result result;const auto installed=Version::parse(build.version);
    if(!installed || !channelValid(prefs.channel)){result.error="The installed version or update channel is invalid.";return result;}
    std::optional<Version> best;
    for(const auto& release:releases)
    {
        const auto version=Version::parse(release.version);
        if(!version || !channelValid(release.channel) || (prefs.channel=="stable" && release.channel!="stable") ||
           !hex(release.commit,40) || !trustedUrl(release.url,extraHosts))continue;
        if(!best || *best<*version || (*best==*version && release.channel=="stable"))
        {best=version;result.release=release;}
    }
    if(!best)return result;
    if(!(*installed<*best) || (!manual && prefs.skipped==result.release.version)){result.state=State::Current;return result;}
    result.state=State::NoArtifact;
    for(const auto& asset:result.release.assets)
        if(matches(asset,build) && asset.bytes>0 && hex(asset.sha256,64) && trustedUrl(asset.url,extraHosts))
        {
            if(result.artifact){result.artifact.reset();result.error="The release has duplicate matching downloads.";return result;}
            result.artifact=asset;result.state=State::Available;
        }
    return result;
}
// The decoder is shared by the host and viewer; no account or login data enters it.
std::vector<Release> decodeFeed(const std::string& body,const std::string& extraHosts,std::string& error);
}
#endif
