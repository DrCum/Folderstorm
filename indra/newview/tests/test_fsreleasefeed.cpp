#include "fsreleasepolicy.h"
#include <boost/json.hpp>
#include <cassert>
#include <stdexcept>
using namespace fs_release;
namespace json=boost::json;
namespace
{
json::object release(std::size_t index)
{
    const auto version=index==31?"7.2.6.1":"7.2.5."+std::to_string(100-index);
    const auto page=ReleasesPage+"/tag/v"+std::to_string(index);
    const auto url=ReleasesPage+"/download/v"+std::to_string(index)+"/setup.exe";
    return {{"schema",1},{"version",version},{"channel",index==31?"stable":"nightly"},
        {"commit",std::string(40,'a')},{"url",page},{"notes",std::string(MaxNotes,'x')},
        {"assets",json::array{json::object{{"platform","windows"},{"architecture","x86_64"},
        {"cpu","avx2"},{"flavor","opensim"},{"installer","nsis"},{"url",url},{"sha256",std::string(64,'b')},{"bytes",42}}}}};
}
json::object github(json::object manifest,bool legacy)
{
    const auto notes=std::string(manifest.at("notes").as_string());
    if(!legacy)manifest.erase("notes");
    return {{"draft",false},{"prerelease",manifest.at("channel").as_string()=="nightly"},
        {"html_url",manifest.at("url")},{"body",notes+"\n\n<!-- folderstorm-release-v1\n"+json::serialize(manifest)+"\n-->\n"},
        {"assets",json::array{json::object{{"browser_download_url",manifest.at("assets").as_array()[0].as_object().at("url")},{"size",42}}}}};
}
}
int main()
{
    // Windows publisher output and GitHub edits may use CRLF, including
    // mixed delimiters. Visible notes must have the same normalized text.
    for(const bool legacy:{true,false})
    {
        auto manifest=release(0);manifest["notes"]="First line\nSecond line\n";
        const auto original=github(manifest,legacy);
        const auto lf=std::string(original.at("body").as_string());
        for(unsigned mask=0;mask<8;++mask)
        {
            auto row=original;std::string body;
            const auto opening=lf.find("<!-- folderstorm-release-v1\n");
            const auto closing=lf.find("\n-->",opening);
            for(std::size_t i=0;i<lf.size();++i)
            {
                const unsigned section=i<opening?1U:i<closing?2U:4U;
                if(lf[i]=='\n' && (mask&section))body+='\r';
                body+=lf[i];
            }
            row["body"]=body;std::string error;
            const auto decoded=decodeFeed(json::serialize(json::array{row}),"",error);
            assert(error.empty() && decoded.size()==1);
            assert(decoded[0].version=="7.2.5.100" && decoded[0].assets.size()==1);
            assert(decoded[0].notes=="First line\nSecond line\n");
        }
        auto duplicate=original;
        duplicate["body"]=lf+"<!-- folderstorm-release-v1\r\n"+json::serialize(manifest)+"\r\n-->\r\n";
        std::string error;
        assert(decodeFeed(json::serialize(json::array{duplicate}),"",error).empty() && error.empty());
    }
    const Build build{"7.2.5.1","windows","x86_64","avx2","opensim","nsis"};Preferences prefs;
    const auto requests=feedPages(GitHubFeed+"?per_page=32");
    for(const bool legacy:{true,false})
    {
        json::array all;
        for(std::size_t i=0;i<MaxReleases;++i)all.push_back(github(release(i),legacy));
        std::string error;
        if(legacy)
        {
            assert(json::serialize(all).size()>MaxFeedBytes);
            assert(decodeFeed(json::serialize(all),"",error).empty() && !error.empty());
        }
        std::size_t called=0;
        auto fetch=[&](const std::string& url)
        {
            assert(called<requests.size() && url==requests[called]);
            json::array page;
            const auto start=called++*GitHubPageEntries;
            for(std::size_t i=start;i<start+GitHubPageEntries;++i)page.push_back(all[i]);
            auto bytes=json::serialize(page);assert(bytes.size()<=MaxFeedBytes);return bytes;
        };
        auto releases=fetchFeed(GitHubFeed+"?per_page=32","",fetch,error);
        assert(error.empty() && called==4 && releases.size()==MaxReleases);
        const auto chosen=choose(releases,build,prefs,true);
        assert(chosen.state==State::Available && chosen.artifact && chosen.release.version=="7.2.6.1");
        assert(chosen.release.notes==std::string(MaxNotes,'x'));
        // Do not stop at an unrecognized/draft-only page.
        for(std::size_t i=0;i<GitHubPageEntries;++i)all[i].as_object()["draft"]=true;
        called=0;releases=fetchFeed(GitHubFeed,"",fetch,error);
        assert(error.empty() && called==4 && releases.size()==24);
        assert(choose(releases,build,prefs,true).release.version=="7.2.6.1");
    }
    std::string error;std::size_t called=0;
    auto shortPage=[&](const std::string& url)
    {assert(url==requests[0]);++called;return json::serialize(json::array{github(release(31),false)});};
    assert(fetchFeed(GitHubFeed,"",shortPage,error).size()==1 && called==1 && error.empty());
    const std::string custom="https://downloads.example.test/releases.json";called=0;
    auto staticFeed=[&](const std::string& url)
    {assert(url==custom);++called;return json::serialize(json::object{{"schema",1},{"releases",json::array{release(31)}}});};
    assert(fetchFeed(custom,"",staticFeed,error).size()==1 && called==1 && error.empty());
    auto firstPage=[]()
    {json::array page;for(std::size_t i=0;i<GitHubPageEntries;++i)page.push_back(github(release(i),false));return json::serialize(page);};
    for(const auto& invalid:{std::string("bad JSON"),std::string(MaxFeedBytes+1,'x')})
    {
        called=0;
        auto broken=[&](const std::string&){return called++==0?firstPage():invalid;};
        assert(fetchFeed(GitHubFeed,"",broken,error).empty() && !error.empty() && called==2);
    }
    called=0;
    auto failed=[&](const std::string&)
    {if(called++==0)return firstPage();throw std::runtime_error("transport failed");};
    try{fetchFeed(GitHubFeed,"",failed,error);assert(false);}catch(const std::runtime_error&){}
    assert(called==2);
}
