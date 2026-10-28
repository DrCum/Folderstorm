#include "fschatmediaresolver.h"
#include <cassert>
#include <map>
using namespace fs_chat_media;
namespace
{
Response result(std::string body, std::string type, std::string url = "")
{ Response response; response.body.assign(body.begin(), body.end()); response.type = std::move(type); response.status = 200; response.url = std::move(url); return response; }
}
int main()
{
    std::map<std::string, Response> fixtures;
    std::vector<std::string> calls;
    const auto fetcher = [&](const std::string& url, std::size_t limit)
    {
        assert(parse(url) && limit <= MaxImageBytes); calls.push_back(url);
        const auto found = fixtures.find(url); return found == fixtures.end() ? Response{} : found->second;
    };
    fixtures["https://i.snipboard.io/actual.png?token=a&x=b"] = result("PNG fixture", "image/png");
    fixtures["https://snipboard.io/abc.jpg"] = result("<head><meta property='og:image' content='https://i.snipboard.io/actual.png?token=a&amp;x=b'></head>", "text/html");
    assert(resolveMedia("https://snipboard.io/abc.jpg", DefaultSites, fetcher).status == 200);
    assert(calls.size() == 2 && calls.back() == "https://i.snipboard.io/actual.png?token=a&x=b");

    calls.clear();
    fixtures["https://api.gyazo.com/api/oembed?url=https%3A%2F%2Fgyazo.com%2Fabc"] = result("{\"type\":\"photo\",\"url\":\"https://i.gyazo.com/actual.gif\"}", "application/json");
    fixtures["https://i.gyazo.com/actual.gif"] = result("GIF fixture", "image/gif");
    assert(resolveMedia("https://gyazo.com/abc", DefaultSites, fetcher).type == "image/gif" && calls.size() == 2);
    fixtures["https://api.gyazo.com/api/oembed?url=https%3A%2F%2Fgyazo.com%2Fvideo"] = result("{\"type\":\"video\",\"html\":\"<script>never execute</script>\",\"thumbnail_url\":\"https://i.gyazo.com/actual.gif\"}", "application/json");
    assert(resolveMedia("https://gyazo.com/video", DefaultSites, fetcher).status == 200);
    calls.clear();
    fixtures["https://api.gyazo.com/api/oembed?url=https%3A%2F%2Fgyazo.com%2Fbad"] = result("{\"url\":\"file:///private\"}", "application/json");
    assert(!resolveMedia("https://gyazo.com/bad", DefaultSites, fetcher).status && calls.size() == 1);

    calls.clear();
    fixtures["https://jmp.sh/abc+?token=123"] = result("GIF fixture", "image/gif");
    assert(resolveMedia("https://jmp.sh/abc?token=123", DefaultSites, fetcher).status == 200);
    assert(calls.size() == 1 && calls[0] == "https://jmp.sh/abc+?token=123");
    calls.clear();
    fixtures["https://jmp.sh/fallback"] = result("<meta property='og:image' content='https://i.gyazo.com/actual.gif'>", "text/html");
    assert(resolveMedia("https://jmp.sh/fallback", DefaultSites, fetcher).status == 200 && calls.size() == 3);

    for (const auto* siteUrl : {"https://imgur.com/a/album", "https://ibb.co/abc", "https://imgchest.com/p/abc", "https://uploads.folderstorm.com/view/abc"})
    {
        calls.clear(); fixtures[siteUrl] = result("<meta content='https://i.gyazo.com/actual.gif' name='twitter:image'>", "text/html");
        assert(resolveMedia(siteUrl, std::string(DefaultSites) + ",uploads.folderstorm.com", fetcher).status == 200 && calls.size() == 2);
    }
    calls.clear(); fixtures["https://uploads.folderstorm.com/view/moved"] = result("<meta property=og:image content='image.png'>", "text/html", "https://cdn.folderstorm.com/path/page");
    fixtures["https://cdn.folderstorm.com/path/image.png"] = result("PNG", "image/png");
    assert(resolveMedia("https://uploads.folderstorm.com/view/moved", "uploads.folderstorm.com", fetcher).status == 200);
    assert(calls.back() == "https://cdn.folderstorm.com/path/image.png");

    // Zipline's verified view routes are script-only pages, even with a .png
    // extension. Use the instance's public raw route without executing HTML.
    calls.clear();
    fixtures["https://images.sodie.online/raw/sodie-sample.png"] = result("PNG fixture", "image/png");
    assert(resolveMedia("https://images.sodie.online/view/sodie-sample.png", DefaultSites, fetcher).type == "image/png");
    assert(calls.size() == 1 && calls[0] == "https://images.sodie.online/raw/sodie-sample.png");
    calls.clear();
    fixtures["https://images.sodie.online/raw/sodie-sample.gif?token=a&x=b"] = result("GIF fixture", "image/gif");
    assert(resolveMedia("https://images.sodie.online/view/sodie-sample.gif?token=a&x=b", DefaultSites, fetcher).type == "image/gif");
    assert(calls.size() == 1 && calls[0] == "https://images.sodie.online/raw/sodie-sample.gif?token=a&x=b");
    calls.clear();
    assert(resolveMedia("https://images.sodie.online/raw/sodie-sample.png", DefaultSites, fetcher).type == "image/png" && calls.size() == 1);
    calls.clear();
    assert(!resolveMedia("https://images.sodie.online.attacker.example.org/view/sodie-sample.png", DefaultSites, fetcher).status);
    assert(calls.size() == 1 && calls[0] == "https://images.sodie.online.attacker.example.org/view/sodie-sample.png");
    calls.clear();
    assert(!resolveMedia("https://images.sodie.online/view/deleted.png", DefaultSites, fetcher).status && calls.size() == 1);

    calls.clear(); fixtures["https://i.gyazo.com/not-an-image.png"] = result("<meta property='og:image' content='https://i.gyazo.com/actual.gif'>", "text/html");
    assert(!resolveMedia("https://i.gyazo.com/not-an-image.png", DefaultSites, fetcher).status && calls.size() == 1);
    calls.clear(); assert(!resolveMedia("https://unknown.example.org/share/abc", DefaultSites, fetcher).status && calls.empty());
    fixtures["https://snipboard.io/deleted"] = Response{};
    assert(!resolveMedia("https://snipboard.io/deleted", DefaultSites, fetcher).status);
    fixtures["https://snipboard.io/private"] = result("Login required", "text/html");
    assert(!resolveMedia("https://snipboard.io/private", DefaultSites, fetcher).status);
}
