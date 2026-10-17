#include "fsreleasepolicy.h"
#include "../../llcorehttp/httpbodylimit.h"
#include <cassert>
using namespace fs_release;
int main()
{
    const auto pages=feedPages(GitHubFeed+"?per_page=32");
    assert(pages.size()==4 && pages.front()==GitHubFeed+"?per_page=8&page=1");
    assert(pages.back()==GitHubFeed+"?per_page=8&page=4");
    assert(feedPages("https://downloads.example.test/releases.json").size()==1);
    assert(feedPages(GitHubFeed+"-other?per_page=32").size()==1);
    assert(LLCore::bodyChunkFits(0,1,MaxFeedBytes,MaxFeedBytes));
    assert(!LLCore::bodyChunkFits(MaxFeedBytes,1,1,MaxFeedBytes));
    assert(!LLCore::bodyChunkFits(MaxFeedBytes-4,1,5,MaxFeedBytes));
    assert(LLCore::bodyChunkFits(0,1,MaxFeedBytes+1,0));
    assert(!LLCore::bodyChunkFits(0,2,std::numeric_limits<std::size_t>::max(),0));
    assert(Version::parse("7.2.5.82125") && !Version::parse("7.2.5") && !Version::parse("07.2.5.1"));
    assert(!Version::parse("7.2.5.18446744073709551616") && !Version::parse("7.2.5.1junk"));
    assert(*Version::parse("7.2.5.9")<*Version::parse("7.2.5.10"));
    assert(*Version::parse("7.2.5.999999")<*Version::parse("7.3.0.1"));
    assert(!trustedUrl("https://github.com.evil.test/DrCum/Folderstorm/releases"));
    assert(!trustedUrl("https://github.com/other/repo/releases") && !trustedUrl("http://github.com/DrCum/Folderstorm/releases"));
    assert(!trustedUrl("https://github.com@evil.test/file") && !trustedUrl("https://example.test/file", "ample.test"));
    assert(trustedUrl("https://downloads.example.test/file","downloads.example.test"));
    Build build{"7.2.5.9","windows","x86_64","avx2","opensim","nsis"};Preferences prefs;
    Artifact asset{"windows","x86_64","avx2","opensim","nsis",ReleasesPage+"/download/v10/setup.exe",std::string(64,'a'),100};
    Release stable{"7.2.5.10","stable",std::string(40,'b'),ReleasesPage+"/tag/v10","Notes",{asset}};
    auto nightly=stable;nightly.channel="nightly";nightly.version="7.2.5.11";
    ReleaseHistory history;
    assert(history.append({},GitHubPageEntries,GitHubPageEntries)); // Draft-only first page still counts.
    assert(history.append({nightly},GitHubPageEntries,GitHubPageEntries));
    assert(history.append({nightly},GitHubPageEntries,GitHubPageEntries)); // Pagination movement deduplicates.
    assert(history.append({stable},GitHubPageEntries,GitHubPageEntries));
    assert(history.entries==MaxReleases && history.releases.size()==2);
    assert(choose(history.releases,build,prefs,true).release.version==stable.version); // Eligible result on page four.
    assert(!history.append({},1,GitHubPageEntries));
    ReleaseHistory invalid;assert(!invalid.append({},GitHubPageEntries+1,GitHubPageEntries));
    assert(!invalid.append({stable},0,GitHubPageEntries));
    auto result=choose({nightly,stable},build,prefs,false);assert(result.state==State::Available && result.release.version==stable.version);
    prefs.channel="nightly";assert(choose({stable,nightly},build,prefs,false).release.version==nightly.version);
    prefs.skipped=nightly.version;assert(choose({nightly},build,prefs,false).state==State::Current);
    assert(choose({nightly},build,prefs,true).state==State::Available);
    build.cpu="baseline";assert(choose({nightly},build,prefs,true).state==State::NoArtifact);
    build.cpu="avx2";build.installer="velopack";assert(choose({nightly},build,prefs,true).state==State::NoArtifact);
    build.installer="nsis";build.version="8.0.0.1";assert(choose({stable,nightly},build,prefs,true).state==State::Current);
    build.version="7.2.5.9";stable.assets.push_back(asset);result=choose({stable},build,prefs,true);assert(!result.artifact && !result.error.empty());
    prefs.lastCheck=100;assert(!prefs.due(101) && prefs.due(100+CheckInterval) && prefs.due(99));
    prefs.automatic=false;assert(!prefs.due(100+CheckInterval));
    Preferences decoded;assert(Preferences::decode(prefs.encode(),decoded) && !decoded.automatic && decoded.channel==prefs.channel && decoded.skipped==prefs.skipped);
    assert(!Preferences::decode("1\n1\nstable\n\n18446744073709551616\n",decoded));
    assert(!Preferences::decode(prefs.encode()+"extra",decoded));
}
