/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_FILE_UPLOAD_MODEL_H
#define FS_FILE_UPLOAD_MODEL_H
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
namespace fs_file_upload
{
inline constexpr std::size_t maxFiles=32, maxFileBytes=64*1024*1024, maxBatchBytes=128*1024*1024;
inline bool folder(const std::string& value)
{
    if(value.empty())return true;
    std::size_t begin=0,depth=0;
    while(begin<value.size())
    {
        const auto end=value.find('/',begin);
        const auto part=value.substr(begin,end==std::string::npos?value.size()-begin:end-begin);
        if(part.empty() || part=="." || part==".." || part.size()>63 || (part.find_first_of("\\:")!=std::string::npos || std::any_of(part.begin(),part.end(),[](unsigned char ch){return ch<32 || ch==127;})) || ++depth>8)return false;
        if(end==std::string::npos)return true;
        begin=end+1;
    }
    return false;
}
inline bool name(const std::string& value)
{return !value.empty() && value.size()<=63 && std::none_of(value.begin(),value.end(),[](unsigned char ch){return ch<32 || ch==127;});}
inline bool hash(const std::string& value)
{return value.size()==64 && std::all_of(value.begin(),value.end(),[](char ch){return (ch>='0' && ch<='9') || (ch>='a' && ch<='f');});}
inline bool quoteAccepted(int approved,int current)
{return approved>=0 && current>=0 && current<=approved;}
inline bool serverQuoteAccepted(bool required,bool present,bool integer,int approved,int current)
{return !present ? !required : integer && quoteAccepted(approved,current);}
enum class State { Prepared, Running, Complete, Stopped };
struct Progress
{
    State state=State::Prepared;
    std::size_t next=0;
    bool sourceSubmitted=false,submissionStarted=false;
    bool start(){if(state!=State::Prepared)return false;state=State::Running;return true;}
    bool running()const{return state==State::Running;}
    bool acknowledge(std::size_t index){if(!running() || next!=index)return false;++next;sourceSubmitted=false;submissionStarted=false;return true;}
    void stop(){if(state!=State::Complete)state=State::Stopped;}
    bool complete(std::size_t count){if(!running() || next!=count)return false;state=State::Complete;return true;}
};
}
#endif
