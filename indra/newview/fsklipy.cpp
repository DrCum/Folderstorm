/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsklipy.h"
#include <boost/json.hpp>
namespace fs_klipy
{
namespace
{
bool identifier(const std::string& s, std::size_t limit)
{ return !s.empty() && s.size() <= limit && std::all_of(s.begin(),s.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_';}); }
std::string encode(const std::string& value)
{
    std::string out; constexpr char hex[]="0123456789ABCDEF";
    for(unsigned char c:value) { if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=static_cast<char>(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];} }
    return out;
}
const boost::json::value* field(const boost::json::value& v,const char* name)
{ return v.is_object()?v.as_object().if_contains(name):nullptr; }
std::string string(const boost::json::value* v,std::size_t limit)
{ return v&&v->is_string()&&v->as_string().size()<=limit?std::string(v->as_string()):""; }
std::string rendition(const boost::json::value& object,const char* size)
{
    const auto* files=field(object,"file");if(!files)return {};
    const auto* variant=field(*files,size);if(!variant)return {};
    const auto* gif=field(*variant,"gif");if(!gif)return {};
    const auto url=string(field(*gif,"url"),4096);return mediaURL(url)?url:"";
}
}
bool keyValid(const std::string& key) { return identifier(key,256); }
bool slugValid(const std::string& slug) { return identifier(slug,256); }
bool mediaURL(const std::string& address)
{
    const auto url=fs_chat_media::parse(address);if(!url)return false;
    return url->host=="static.klipy.com"||url->host=="static1.klipy.com"||url->host=="static2.klipy.com"||url->host=="static.klipy.co"||url->host=="static1.klipy.co"||url->host=="static2.klipy.co";
}
std::string requestURL(const Request& r)
{
    if(!keyValid(r.key)||!identifier(r.customer,64)||r.page<1||r.page>10000||!fs_gif_board::text(r.query,256)||!fs_gif_board::text(r.locale,16))return {};
    std::string path;
    switch(r.kind)
    {
        case Kind::Trending:path="trending";break;
        case Kind::Search:if(r.query.empty())return {};path="search";break;
        case Kind::Recent:path="recent/"+r.customer;break;
        case Kind::Categories:path="categories";break;
        case Kind::Items:
            if(r.slugs.empty()||r.slugs.size()>8)return {};
            path="items?slugs=";for(const auto& slug:r.slugs){if(!slugValid(slug))return {};if(path.back()!='=')path+=',';path+=slug;}
            break;
    }
    auto url="https://api.klipy.com/api/v1/"+r.key+"/gifs/"+path;
    // Favourites already selects one bounded chunk of IDs. Do not paginate
    // that chunk again on the server when the board advances to its next chunk.
    url+=r.kind==Kind::Items?'&':'?';url+="page="+std::to_string(r.kind==Kind::Items?1:r.page)+"&per_page="+std::to_string(PageSize)+"&customer_id="+r.customer;
    if(r.kind==Kind::Search)url+="&q="+encode(r.query);
    if(!r.locale.empty())url+="&locale="+encode(r.locale);
    return fs_chat_media::parse(url)?url:"";
}
std::string shareURL(const std::string& key,const std::string& slug)
{ return keyValid(key)&&slugValid(slug)?"https://api.klipy.com/api/v1/"+key+"/gifs/share/"+slug:""; }
std::string shareBody(const std::string& customer,const std::string& query)
{
    if(!identifier(customer,64)||!fs_gif_board::text(query,256))return {};
    boost::json::object data;data["customer_id"]=customer;if(!query.empty())data["q"]=query;return boost::json::serialize(data);
}
std::string reportURL(const std::string& key,const std::string& slug)
{return keyValid(key)&&slugValid(slug)?"https://api.klipy.com/api/v1/"+key+"/gifs/report/"+slug:"";}
std::string reportBody(const std::string& customer,const std::string& reason)
{
    if(!identifier(customer,64))return {};
    bool allowed=false;for(const auto* value:{"nudity","violence","hate_speech","harassment","spam","misinformation","copyright","offensive","illegal","broken","low_quality","not_relevant","impersonation","other"})if(reason==value)allowed=true;
    if(!allowed)return {};
    boost::json::object body;body["customer_id"]=customer;body["reason"]=reason;return boost::json::serialize(body);
}
Result parse(const fs_chat_media::Response& response,Kind kind)
{
    Result result;
    if(response.status==429){result.error="KLIPY is rate limited. Try again later.";result.retryAfter=response.retryAfter?response.retryAfter:60;return result;}
    if(response.status!=200){result.error=response.status==401||response.status==403?"KLIPY key is unavailable or not authorized.":"KLIPY is unavailable. Try again later.";return result;}
    if(response.body.size()>fs_chat_media::MaxMetadataBytes){result.error="KLIPY response exceeded its size limit.";return result;}
    boost::system::error_code error;boost::json::parse_options options;options.max_depth=12;
    const auto data=boost::json::parse(std::string(response.body.begin(),response.body.end()),error,{},options);
    const auto* success=field(data,"result");const auto* payload=field(data,"data");const boost::json::value* rows=payload?field(*payload,"data"):nullptr;
    if(!rows&&payload&&kind==Kind::Categories)rows=field(*payload,"categories");
    if(payload&&payload->is_array())rows=payload;
    if(error||!success||!success->is_bool()||!success->as_bool()||!rows||!rows->is_array()||rows->as_array().size()>100)
    {result.error="KLIPY returned an unsupported response.";return result;}
    for(const auto& entry:rows->as_array())
    {
        if(kind==Kind::Categories)
        {
            auto category=entry.is_string()?string(&entry,64):string(field(entry,"category"),64);
            if(category.empty())category=string(field(entry,"query"),64);
            if(!category.empty()&&fs_gif_board::text(category,64))result.categories.push_back(std::move(category));
            continue;
        }
        const auto type=string(field(entry,"type"),32);
        if(type=="ad"){result.items.clear();result.error="KLIPY returned ads. Configure this app key with ads off.";return result;}
        Item item;item.slug=string(field(entry,"slug"),256);item.title=string(field(entry,"title"),256);
        if(!fs_gif_board::text(item.title,256))item.title.clear();
        for(const auto* size:{"xs","sm","md","hd"}){item.preview=rendition(entry,size);if(!item.preview.empty())break;}
        for(const auto* size:{"md","hd","sm","xs"}){item.share=rendition(entry,size);if(!item.share.empty())break;}
        item.available=(type.empty()||type=="gif")&&slugValid(item.slug)&&!item.preview.empty()&&!item.share.empty();
        if(!item.available){item.preview.clear();item.share.clear();}
        result.items.push_back(std::move(item)); // Keep unsupported items in their result slots.
    }
    if(const auto* more=payload?field(*payload,"has_next"):nullptr;more&&more->is_bool())result.more=more->as_bool();
    else result.more=result.items.size()==PageSize;
    return result;
}
}
