#include "fschatmediahttp.h"
#include <cassert>
using namespace fs_chat_media;
int main()
{
    std::atomic<bool> cancelled{false};
    for (const auto* url : {"file:///etc/passwd", "http://127.0.0.1/a.png", "https://localhost/a.png", "https://printer.local/a.png", "https://user@cdn.example.org/a.png"})
        assert(fetch(url, "", MaxImageBytes, cancelled).status == 0);
    cancelled = true;
    assert(fetch("https://i.gyazo.com/image.png", "", MaxImageBytes, cancelled).body.empty());
    assert(fetch("https://i.gyazo.com/image.png", "", MaxImageBytes + 1, cancelled).body.empty());
    assert(klipyRequest("https://api.klipy.com/api/v1/test/gifs/trending", "", "", cancelled).status == 0);
    cancelled = false;
    for (const auto* url : {"https://api.klipy.com.evil.example/api/v1/key", "https://api.klipy.com/other", "https://static.klipy.com/api/v1/key", "http://api.klipy.com/api/v1/key"})
        assert(klipyRequest(url, "", "", cancelled).status == 0);
    assert(klipyRequest("https://api.klipy.com/api/v1/test/gifs/share/id", "", std::string(2049, 'x'), cancelled).status == 0);
}
