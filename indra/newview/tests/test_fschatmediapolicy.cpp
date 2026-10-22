#include "fschatmediapolicy.h"
#include <cassert>
using namespace fs_chat_media;
int main()
{
    assert(!parse("http://i.gyazo.com/p.png"));
    assert(!parse("https://user:pass@cdn.example.org/p.png"));
    assert(!parse("https://cdn.example.org:8443/p.png"));
    assert(!parse("https://cdn.example.org\\@127.0.0.1/p.png"));
    assert(!parse("https://printer.local/p.png") && !parse("https://localhost/p.png"));
    const auto signedUrl = parse("https://cdn.example.org:443/image.png?token=A%2FB&expires=4#part");
    assert(signedUrl && signedUrl->path == "/image.png?token=A%2FB&expires=4");
    assert(imagePath(*signedUrl));
    assert(provider(*parse("https://gyazo.com/abcd")) == Provider::Gyazo);
    assert(!provider(*parse("https://gyazo.com.attacker.org/abcd")));
    assert(provider(*parse("https://snipboard.io/AbC123.jpg")) == Provider::Snipboard);
    assert(provider(*parse("https://i.snipboard.io/AbC123.jpg")) == Provider::Direct);
    assert(provider(*parse("https://snipboard.io/AbC123")) == Provider::Snipboard);
    assert(provider(*parse("https://uploads.folderstorm.com/view/hello"), "uploads.folderstorm.com") == Provider::Custom);
    assert(provider(*parse("https://images.sodie.online/view/sodie-sample.png")) == Provider::Zipline);
    assert(provider(*parse("https://images.sodie.online/raw/sodie-sample.png")) == Provider::Direct);
    assert(!selectedSite(*parse("https://notgyazo.com/x.gif"), "gyazo.com"));
    assert(selectedSite(*parse("https://i.gyazo.com/x.gif"), " snipboard.io, gyazo.com "));
    const auto found = discover("Hi (https://cdn.example.org/a.gif), https://gyazo.com/abc https://gyazo.com/abc https://jmp.sh/foo https://imgur.com/extra");
    assert(found.size() == 3 && found[0] == "https://cdn.example.org/a.gif" && found[2] == "https://jmp.sh/foo");
    assert(discover("[https://i.gyazo.com/a.png image label]").size() == 1);
    const auto origin = *parse("https://snipboard.io/abc");
    assert(metadataImage(origin, "<head><meta content='https://i.snipboard.io/actual.webp?t=1&amp;x=2' property='og:image'></head>") == "https://i.snipboard.io/actual.webp?t=1&x=2");
    assert(metadataImage(origin, "<META NAME=\"twitter:image\" CONTENT=\"//i.snipboard.io/actual.png\">") == "https://i.snipboard.io/actual.png");
    assert(metadataImage(origin, "<meta property='og:image' content='/a.png'><meta property='og:image:secure_url' content='https://cdn.example.org/b.gif'>") == "https://cdn.example.org/b.gif");
    assert(metadataImage(origin, "<meta property='og:image' content='file:///secret'>").empty());
    assert(metadataImage(origin, "<meta property='og:image' content='http://example.org/a.png'>").empty());
    assert(metadataImage(origin, "</head><body><meta property='og:image' content='/a.png'>").empty());
    assert(metadataImage(origin, std::string(MaxMetadataBytes + 1, 'a')).empty());
    const unsigned char loopback[]{127,0,0,1}, privateIp[]{10,1,2,3}, cgnat[]{100,64,0,1}, lan[]{192,168,1,1}, publicIp[]{8,8,8,8};
    assert(!publicV4(loopback) && !publicV4(privateIp) && !publicV4(cgnat) && !publicV4(lan) && publicV4(publicIp));
    const unsigned char public6[16]{0x26,0x06,0x47,0x00}, local6[16]{0xfe,0x80}, mapped[16]{0,0,0,0,0,0,0,0,0,0,255,255,127,0,0,1};
    assert(publicV6(public6) && !publicV6(local6) && !publicV6(mapped));
    assert(frameDelay(0) == 40 && frameDelay(60000) == 10000);
    assert(textureExtent(3) == 4 && textureExtent(1000) == 1024 && textureExtent(1024) == 1024 && textureExtent(1025) == 0);
    const auto size = fit(1920,1080,320,240); assert(size.width == 320 && size.height == 180);
    assert(fit(4,8,320,240).width == 4 && fit(0,8,320,240).width == 0);
}
