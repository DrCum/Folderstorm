/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CONTACTS_H
#define FS_SESSION_CONTACTS_H
#include "fssessionchatmodel.h"
#include <set>
#include <sstream>
#include <vector>
namespace fs_session
{
constexpr std::size_t MaxContacts = 2048, MaxSearchContacts = 100;
struct Contact
{
    std::string id, display, username;
    bool online = false, allowed = false;
    bool valid() const
    { return !id.empty() && validAccount(id) && !display.empty() && display.size() <= 255 && username.size() <= 127 &&
        validUtf8(display) && validUtf8(username); }
};
inline std::string contactFold(std::string text)
{ for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c-'A'+'a'); return text; }
inline std::vector<Contact> sortedContacts(std::vector<Contact> rows, const std::string& filter = {})
{
    const auto folded = contactFold(filter);
    rows.erase(std::remove_if(rows.begin(), rows.end(), [&](const Contact& c)
        { return !c.valid() || (!folded.empty() && contactFold(c.display+" "+c.username).find(folded)==std::string::npos); }),rows.end());
    std::sort(rows.begin(),rows.end(),[](const Contact& a,const Contact& b)
    {
        if (a.online != b.online) return a.online;
        const auto x=contactFold(a.display),y=contactFold(b.display);
        return x != y ? x < y : a.username != b.username ? a.username < b.username : a.id < b.id;
    });
    return rows;
}
// Printable UTF-8 keeps the fixed protocol's text validation intact. Names
// cannot contain delimiters/control characters; every page is independently bounded.
inline std::string contactPage(const std::vector<Contact>& rows, std::size_t start, std::size_t& next)
{
    std::string text="FSC1\n"; next=start;
    for (; next<rows.size(); ++next)
    {
        const auto& c=rows[next]; if (!c.valid()) break;
        const auto line=c.id+'\t'+(c.online?"1":"0")+'\t'+(c.allowed?"1":"0")+'\t'+c.display+'\t'+c.username+'\n';
        if (text.size()+line.size()>3071) break;
        text+=line;
    }
    return text;
}
inline bool readContactPage(const std::string& text,std::vector<Contact>& rows)
{
    if (text.size()>3071 || text.compare(0,5,"FSC1\n")!=0 || text.back()!='\n') return false;
    std::istringstream input(text.substr(5)); std::string line; std::vector<Contact> result;
    while(std::getline(input,line))
    {
        std::vector<std::string> fields;std::size_t at=0;
        for(;;){const auto end=line.find('\t',at);fields.push_back(line.substr(at,end==std::string::npos?end:end-at));if(end==std::string::npos)break;at=end+1;}
        if(fields.size()!=5 || (fields[1]!="0" && fields[1]!="1") || (fields[2]!="0" && fields[2]!="1"))return false;
        Contact c{fields[0],fields[3],fields[4],fields[1]=="1",fields[2]=="1"};if(!c.valid())return false;result.push_back(std::move(c));
    }
    rows=std::move(result);return true;
}
struct ContactTransfer
{
    std::uint64_t token=0,offset=0,revision=0;
    bool loading=false,pending=false;
    std::string error;
    std::vector<Contact> rows,staging;
    void begin(std::uint64_t value){token=value;offset=0;loading=true;pending=false;error.clear();staging.clear();}
    bool accept(const Message& request,const Message& response,std::size_t limit)
    {
        if(!loading || request.event!=token || request.cursor!=offset)return false;
        if(response.flags&Error){loading=false;error=response.detail;return true;}
        if(response.event!=token)return false;
        if(response.height==1){pending=true;return true;}
        pending=false;std::vector<Contact> page;
        if(response.unread>limit || response.cursor>response.unread || response.cursor<offset ||
            !readContactPage(response.text,page) || response.cursor-offset!=page.size())return false;
        if((page.empty() && offset<response.unread) || staging.size()+page.size()>limit)return false;
        std::set<std::string> seen;for(const auto& c:staging)seen.insert(c.id);
        for(const auto& c:page)if(!seen.insert(c.id).second)return false;
        staging.insert(staging.end(),page.begin(),page.end());offset=response.cursor;
        if(offset==response.unread){rows=sortedContacts(std::move(staging));staging.clear();loading=false;++revision;}
        return true;
    }
};
struct ContactDirectory
{
    ChatKey source;
    ContactTransfer friends,search;
    std::uint64_t serial=0;
    bool bind(const Message& message)
    {
        if(source.owns(message) && message.state==State::Ready && !(message.flags&ChatRestricted))return false;
        source={message.worker,message.generation,message.account,message.grid,{}};friends={};search={};++serial;return true;
    }
    bool owns(const Message& message) const
    {return source.owns(message) && message.state==State::Ready && !message.account.empty() && !(message.flags&ChatRestricted);}
    void beginFriends(){friends.begin(++serial);}
    void beginSearch(){search.rows.clear();search.begin(++serial);}
    bool accept(const Message& request,const Message& response)
    {return owns(response) && source.owns(request) && request.width<=1 &&
        (request.width?search:friends).accept(request,response,request.width?MaxSearchContacts:MaxContacts);}
    bool selected(const Message& message,const Contact& contact,unsigned int channel,std::uint64_t token) const
    {
        if(!owns(message) || !contact.valid() || !contact.allowed || channel>1)return false;
        const auto& data=channel?search:friends;
        return token==data.token && std::any_of(data.rows.begin(),data.rows.end(),[&](const Contact& c){return c.id==contact.id && c.allowed;});
    }
};
}
#endif
