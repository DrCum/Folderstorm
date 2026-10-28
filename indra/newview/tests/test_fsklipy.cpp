#include "fsklipy.h"
#include <cassert>
using namespace fs_klipy;
fs_chat_media::Response reply(std::string json,long status=200)
{fs_chat_media::Response response;response.status=status;response.type="application/json";response.body.assign(json.begin(),json.end());return response;}
int main()
{
    Request r;r.key="public_test_key";r.customer="random-client-id";r.kind=Kind::Search;r.query="cat & café";
    const auto url=requestURL(r);assert(url.find("q=cat%20%26%20caf%C3%A9")!=std::string::npos&&url.find("customer_id=random-client-id")!=std::string::npos);
    r.key="key/../other";assert(requestURL(r).empty());r.key="test_key";r.kind=Kind::Items;r.slugs={"cat-1","dog_2"};assert(requestURL(r).find("slugs=cat-1,dog_2")!=std::string::npos);
    r.page=2;assert(requestURL(r).find("page=1&")!=std::string::npos); // Locally chunked IDs must not be offset twice.
    r.slugs={"../bad"};assert(requestURL(r).empty());
    assert(mediaURL("https://static.klipy.com/a.gif?tracking=DoNotRewrite")&&!mediaURL("https://static.klipy.com.evil.example/a.gif")&&!mediaURL("https://api.klipy.com/key"));
    assert(shareURL("key","cat")=="https://api.klipy.com/api/v1/key/gifs/share/cat");assert(shareBody("random-id","hi \"there\"").find("\\\"there\\\"")!=std::string::npos);
    assert(reportBody("random-id","broken").find("broken")!=std::string::npos&&reportBody("id","unknown").empty());
    const auto response=reply(R"({"result":true,"data":{"current_page":1,"per_page":24,"has_next":true,"data":[
      {"type":"gif","slug":"cat-wave","title":"Cat","file":{"xs":{"gif":{"url":"https://static.klipy.com/tiny.gif?Keep=THIS"}},"md":{"gif":{"url":"https://static1.klipy.com/full.gif?Tracking=Exact"}}}},
      {"type":"gif","slug":"missing","title":"Gone","file":{}},
      {"type":"gif","slug":"dog","file":{"sm":{"gif":{"url":"https://static2.klipy.com/dog.gif"}}}}
    ]}})");
    auto result=parse(response);assert(result.error.empty()&&result.items.size()==3&&result.more);
    assert(result.items[0].available&&result.items[0].preview=="https://static.klipy.com/tiny.gif?Keep=THIS"&&result.items[0].share=="https://static1.klipy.com/full.gif?Tracking=Exact");
    assert(!result.items[1].available&&result.items[2].slug=="dog"); // Missing media does not reorder/filter result slots.
    assert(!parse(reply(R"({"result":true,"data":{"data":[{"type":"ad","content":"<script>bad</script>"}]}})")).error.empty());
    auto limited=reply("",429);limited.retryAfter=120;assert(parse(limited).retryAfter==120);
    assert(!parse(reply("<html>")).error.empty()&&!parse(reply(std::string(fs_chat_media::MaxMetadataBytes+1,'x'))).error.empty());
    result=parse(reply(R"({"result":true,"data":{"categories":[{"category":"hello"},{"category":"good night"}]}})"),Kind::Categories);assert(result.categories.size()==2&&result.categories[1]=="good night");
}
