#include "fschatuploadpolicy.h"
#include "fschatmediacodec.h"
#include "fixtures/chat_media_images.inc"
#include <cassert>
using namespace fs_chat_upload;
int main()
{
    assert(quotaFailure(R"({"code":"E5002"})") && quotaFailure(R"({"error":{"code":5002}})"));
    assert(!quotaFailure(R"({"message":"E5002"})") && !quotaFailure("error E5002"));
    Config config; assert(config.valid() && config.base() == "https://images.sodie.online");
    assert(mime(pngBytes) == "image/png" && mime(jpegBytes) == "image/jpeg" && mime(gifBytes) == "image/gif" && mime(webpBytes) == "image/webp");
    assert(mime({}).empty() && mime({'<','h','t','m','l','>'}).empty());
    const std::string valid = R"({"files":[{"id":"file1","name":"hello world.png","type":"image/png","url":"https://images.sodie.online/view/hello%20world.png"}],"deletesAt":"2027-04-20T00:00:00.000Z"})";
    auto reply = parseReply(config, valid, "image/png");
    assert(reply.state == State::Complete && reply.id == "file1" && reply.url == "https://images.sodie.online/raw/hello%20world.png" && !reply.expiry.empty());
    assert(!blocksSend(reply.state));
    assert(blocksSend(State::Preparing) && blocksSend(State::Review) && blocksSend(State::Queued) && blocksSend(State::Uploading) && blocksSend(State::Processing));
    for (auto source : {valid + "junk", std::string("{}"), std::string("{\"files\":[]}"), std::string(MaxReplyBytes + 1, ' '), std::string("<html>error</html>")})
        assert(parseReply(config, source, "image/png").state == State::Failed);
    const auto replace = [&](const std::string& old, const std::string& next)
    { auto value = valid; const auto at = value.find(old); assert(at != std::string::npos); value.replace(at, old.size(), next); return value; };
    assert(parseReply(config, replace("https://images.sodie.online/view/", "https://attacker.example.org/"), "image/png").state == State::Failed);
    assert(parseReply(config, replace("hello world.png", "../private.png"), "image/png").state == State::Failed);
    assert(parseReply(config, replace("image/png", "text/html"), "image/png").state == State::Failed);
    assert(parseReply(config, replace("\"type\"", "\"pending\":true,\"type\""), "image/png").state == State::Processing);
    auto other = config; other.origin = "https://other.example.org/"; assert(other.valid() && !sameDestination(config, other));
    for (const auto* host : {"http://images.sodie.online", "https://user:secret@images.sodie.online", "https://images.sodie.online/path", "https://images.sodie.online?token=secret", "https://localhost"})
    { other = config; other.origin = host; assert(!other.valid()); }
    other = config; other.folder = "folder\r\nInjected: yes"; assert(!other.valid());
    // Upload previews need just one small frame; original GIF/WebP bytes are unchanged.
    for (const auto& bytes : {pngBytes, jpegBytes, gifBytes, webpBytes})
    { const auto preview = fs_chat_media::decode(bytes, 2, true); assert(preview && preview->width <= 2 && preview->height <= 2 && preview->frames.size() == 1); }
    assert(!fs_chat_media::decode(pngBytes, 0, true));
}
