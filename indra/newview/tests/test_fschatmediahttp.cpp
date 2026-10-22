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
}
