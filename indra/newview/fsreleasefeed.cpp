/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsreleasepolicy.h"
#include <boost/json.hpp>
#include <stdexcept>

namespace fs_release
{
namespace
{
std::string text(const boost::json::object& object,const char* key,std::size_t limit=2048)
{
    const auto* value=object.if_contains(key);
    return value && value->is_string() && value->as_string().size()<=limit?std::string(value->as_string()):std::string{};
}
Release releaseFrom(const boost::json::value& value,const std::string& extraHosts)
{
    Release result;if(!value.is_object())return result;
    const auto& object=value.as_object();const auto* schema=object.if_contains("schema");
    if(!schema || !schema->is_int64() || schema->as_int64()!=1)return result;
    result.version=text(object,"version",80);result.channel=text(object,"channel",16);
    result.commit=text(object,"commit",40);result.url=text(object,"url");result.notes=plainText(text(object,"notes",MaxNotes));
    const auto* assets=object.if_contains("assets");
    if(!Version::parse(result.version) || !channelValid(result.channel) || !hex(result.commit,40) ||
        !trustedUrl(result.url,extraHosts) || !assets || !assets->is_array() || assets->as_array().size()>MaxAssets)return {};
    for(const auto& entry:assets->as_array())
    {
        if(!entry.is_object())return {};
        const auto& asset=entry.as_object();Artifact item;
        item.platform=text(asset,"platform",16);item.architecture=text(asset,"architecture",16);item.cpu=text(asset,"cpu",16);
        item.flavor=text(asset,"flavor",16);item.installer=text(asset,"installer",16);item.url=text(asset,"url");item.sha256=text(asset,"sha256",64);
        const auto* bytes=asset.if_contains("bytes");
        if(!bytes || (!bytes->is_uint64() && !bytes->is_int64()) || (bytes->is_int64() && bytes->as_int64()<=0))return {};
        item.bytes=bytes->is_uint64()?bytes->as_uint64():static_cast<std::uint64_t>(bytes->as_int64());
        if(!item.bytes || !hex(item.sha256,64) || !trustedUrl(item.url,extraHosts))return {};
        result.assets.push_back(std::move(item));
    }
    return result;
}
}
std::vector<Release> decodeFeed(const std::string& body,const std::string& extraHosts,std::string& error)
{
    error.clear();std::vector<Release> result;
    if(body.size()>MaxFeedBytes){error="The update response is too large.";return result;}
    try
    {
        boost::json::parse_options options;options.max_depth=24;
        const auto root=boost::json::parse(body,{},options);
        if(root.is_object())
        {
            const auto* schema=root.as_object().if_contains("schema");
            const auto* releases=root.as_object().if_contains("releases");
            if(!schema || !schema->is_int64() || schema->as_int64()!=1 || !releases || !releases->is_array() || releases->as_array().size()>MaxReleases)
                throw std::runtime_error("Invalid update manifest");
            for(const auto& entry:releases->as_array())
            {
                auto release=releaseFrom(entry,extraHosts);if(release.version.empty())throw std::runtime_error("Invalid release");
                result.push_back(std::move(release));
            }
        }
        else if(root.is_array() && root.as_array().size()<=MaxReleases)
        {
            const std::string marker="<!-- folderstorm-release-v1\n",end="\n-->";
            for(const auto& entry:root.as_array())
            {
                if(!entry.is_object())continue;
                const auto& object=entry.as_object();const auto* draft=object.if_contains("draft");
                if(!draft || !draft->is_bool() || draft->as_bool())continue;
                const auto notes=text(object,"body",256*1024);const auto start=notes.find(marker);
                if(start==std::string::npos)continue;
                const auto stop=notes.find(end,start+marker.size());
                if(stop==std::string::npos || notes.find(marker,start+marker.size())!=std::string::npos)continue;
                auto release=releaseFrom(boost::json::parse(notes.substr(start+marker.size(),stop-start-marker.size()),{},options),extraHosts);
                const auto* prerelease=object.if_contains("prerelease");
                if(release.version.empty() || !prerelease || !prerelease->is_bool() ||
                    prerelease->as_bool()!=(release.channel=="nightly") || text(object,"html_url")!=release.url)continue;
                const auto* uploaded=object.if_contains("assets");
                release.assets.erase(std::remove_if(release.assets.begin(),release.assets.end(),[&](const Artifact& asset)
                {
                    if(host(asset.url)!="github.com")return false;
                    if(!uploaded || !uploaded->is_array() || uploaded->as_array().size()>128)return true;
                    for(const auto& file:uploaded->as_array())if(file.is_object() && text(file.as_object(),"browser_download_url")==asset.url)
                    {
                        const auto* size=file.as_object().if_contains("size");
                        return !size || (!size->is_uint64() && !size->is_int64()) ||
                            (size->is_int64() && size->as_int64()<0) ||
                            (size->is_uint64()?size->as_uint64():static_cast<std::uint64_t>(size->as_int64()))!=asset.bytes;
                    }
                    return true;
                }),release.assets.end());
                result.push_back(std::move(release));
            }
        }
        else throw std::runtime_error("Invalid update response");
    }
    catch(const std::exception&){result.clear();error="The update response is invalid. Open the releases page to check manually.";}
    return result;
}
}
