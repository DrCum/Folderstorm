/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemepackage.h"
#include "fsthemerules.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#else
#include <openssl/sha.h>
#endif

namespace fs_theme
{
namespace fs=std::filesystem;
using Object=boost::json::object;
using Tree=boost::property_tree::ptree;
fs::path fromUtf8(const std::string& value)
{
#if defined(__cpp_char8_t)
    std::u8string encoded;encoded.reserve(value.size());
    for(unsigned char byte:value)encoded.push_back(static_cast<char8_t>(byte));
    return fs::path(encoded);
#else
    return fs::u8path(value);
#endif
}
std::string toUtf8(const fs::path& path,bool generic)
{
    const auto encoded=generic?path.generic_u8string():path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()),encoded.size());
}
void require(bool condition,const std::string& code,const std::string& detail)
{if(!condition)throw Error(code,detail);}
namespace
{
fs::path nativePath(const fs::path& input)
{
    auto path=fs::absolute(input).lexically_normal().make_preferred();
#if defined(_WIN32)
    const auto wide=path.wstring();
    if(wide.size()>=240 && wide.rfind(L"\\\\?\\",0)!=0)
        return fs::path(wide.rfind(L"\\\\",0)==0?L"\\\\?\\UNC\\"+wide.substr(2):L"\\\\?\\"+wide);
#endif
    return path;
}
std::string text(const Object& object,const char* name)
{
    const auto* value=object.if_contains(name);
    require(value && value->is_string(),"schema",name);
    return std::string(value->as_string());
}
std::int64_t integer(const Object& object,const char* name)
{
    const auto* value=object.if_contains(name);
    require(value && (value->is_int64() || value->is_uint64() && value->as_uint64()<=INT64_MAX),"schema",name);
    return value->is_int64()?value->as_int64():static_cast<std::int64_t>(value->as_uint64());
}
const boost::json::array& array(const Object& object,const char* name)
{const auto* value=object.if_contains(name);require(value && value->is_array(),"schema",name);return value->as_array();}
void fields(const Object& object,const std::set<std::string>& names)
{for(const auto& field:object)require(names.count(std::string(field.key())),"schema",std::string(field.key()));}
struct CasePathLess
{
bool operator()(const std::string& a,const std::string& b) const
{
#if defined(_WIN32)
    const auto left=fromUtf8(a).wstring(),right=fromUtf8(b).wstring();
    return CompareStringOrdinal(left.data(),static_cast<int>(left.size()),right.data(),static_cast<int>(right.size()),TRUE)==CSTR_LESS_THAN;
#else
    return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end(),[](unsigned char l,unsigned char r){return std::toupper(l)<std::toupper(r);});
#endif
}
};
bool linked(const fs::path& path)
{
#if defined(_WIN32)
    const auto attributes=GetFileAttributesW(nativePath(path).c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return fs::is_symlink(fs::symlink_status(path));
#endif
}
fs::path contained(const fs::path& root,const std::string& relative)
{
    pathName(relative);require(!linked(root),"link",toUtf8(root));auto path=root;
    for(const auto& part:fromUtf8(relative)){path/=part;require(!linked(path),"link",relative);}
    return nativePath(path);
}
std::set<std::string> ruleNames(const char* field)
{
    const auto rules=boost::json::parse(ThemeRulesJson).as_object();std::set<std::string> names;
    for(const auto& name:rules.at(field).as_array())names.emplace(name.as_string());return names;
}
const auto xuiPaths=ruleNames("paths"),xuiTextElements=ruleNames("editable_text_elements");
bool xuiAttribute(const std::string& element,const std::string& name)
{
    static const auto rules=boost::json::parse(ThemeRulesJson).as_object();
    const auto* groups=rules.at("elements").as_object().if_contains(element);
    if(!groups)return false;
    const auto* exclusions=rules.at("element_exclusions").as_object().if_contains(element);
    if(exclusions)for(const auto& excluded:exclusions->as_array())if(excluded.as_string()==name)return false;
    for(const auto& group:groups->as_array())for(const auto& attribute:rules.at("attribute_groups").as_object().at(group.as_string()).as_array())if(attribute.as_string()==name)return true;
    return false;
}
std::string extension(std::string path)
{path=toUtf8(fromUtf8(path).extension());for(auto& c:path)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return path;}
std::string kind(const std::string& path)
{
    pathName(path);
    if(path=="base-dependencies.json")return "dependencies";
    if(path=="viewer/colors.xml")return "colors";
    if(path=="viewer/textures/textures.xml")return "textures";
    if(path=="viewer/fonts/fonts.xml")return "font_config";
    if(path=="host/palette.json")return "host_palette";
    if(path.rfind("viewer/xui/en/",0)==0 && xuiPaths.count(path.substr(7)))return "xui";
    const auto ext=extension(path);
    if(path=="preview.png" || path.rfind("viewer/textures/",0)==0 && (ext==".png" || ext==".tga" || ext==".jpg" || ext==".jpeg" || ext==".j2c" || ext==".bmp"))return "image";
    if(path.rfind("viewer/fonts/",0)==0 && (ext==".ttf" || ext==".otf" || ext==".ttc" || ext==".otc"))return "font";
    if(path.rfind("licenses/",0)==0 && (ext==".txt" || ext==".md"))return "license";
    throw Error("kind","Unsupported payload: "+path);
}
std::uint64_t limit(const std::string& type)
{return type=="font"?64ULL*1024*1024:type=="image"?32ULL*1024*1024:ManifestLimit;}
Tree xml(const std::string& bytes)
{
    require(bytes.find("<!DOCTYPE")==std::string::npos && bytes.find("<!ENTITY")==std::string::npos,"xml","DTD/entity declarations are prohibited");
    try {std::istringstream input(bytes);Tree result;boost::property_tree::read_xml(input,result,boost::property_tree::xml_parser::no_comments|boost::property_tree::xml_parser::trim_whitespace);return result;}
    catch(const boost::property_tree::xml_parser_error& e){throw Error("xml",e.message());}
}
std::vector<const Tree::value_type*> children(const Tree& tree)
{std::vector<const Tree::value_type*> result;for(const auto& child:tree)if(child.first!="<xmlattr>")result.push_back(&child);return result;}
void color(const std::string& value);
void xuiValue(const std::string& name,const std::string& value,const std::map<std::string,std::string>& colors,const std::map<std::string,std::string>& textures)
{
    static const auto rules=boost::json::parse(ThemeRulesJson).as_object().at("value_rules").as_object();
    const auto in=[&](const char* group){const auto& names=rules.at(group).as_array();return std::any_of(names.begin(),names.end(),[&](const auto& item){return item.as_string()==name;});};
    require(value.size()<=static_cast<std::size_t>(rules.at("text_limit").as_int64()),"xui_value",name);
    if(in("nonnegative_integer") || in("signed_integer"))
    {
        std::istringstream input(value);input.imbue(std::locale::classic());long long number;std::string extra;const auto maximum=rules.at("integer_limit").as_int64();
        require(static_cast<bool>(input>>number) && !(input>>extra) && number<=maximum && number>=(in("signed_integer")?-maximum:0),"xui_value",name);
    }
    else if(in("color") && !colors.count(value)){try{color(value);}catch(const Error&){throw Error("reference","Unknown color: "+value);}}
    else if(in("image"))require(textures.count(value),"reference","Unknown image: "+value);
    else if(in("boolean"))require(value=="true" || value=="false","xui_value",name);
    else if(const auto* choices=rules.at("enum").as_object().if_contains(name))
        require(std::any_of(choices->as_array().begin(),choices->as_array().end(),[&](const auto& choice){return choice.as_string()==value;}),"xui_value",name);
    else if(const auto* flags=rules.at("flags").as_object().if_contains(name))
    {
        std::size_t begin=0;
        while(begin<value.size())
        {
            const auto end=value.find('|',begin);const auto token=value.substr(begin,end==std::string::npos?end:end-begin);
            require(std::any_of(flags->as_array().begin(),flags->as_array().end(),[&](const auto& flag){return flag.as_string()==token;}),"xui_value",name);
            if(end==std::string::npos)break;
            require(end+1<value.size(),"xui_value",name);begin=end+1;
        }
    }
    else if(const auto* range=rules.at("number").as_object().if_contains(name))
    {
        std::istringstream input(value);input.imbue(std::locale::classic());double number;std::string extra;
        const auto bound=[](const boost::json::value& v){return v.is_double()?v.as_double():static_cast<double>(v.as_int64());};
        require(static_cast<bool>(input>>number) && !(input>>extra) && std::isfinite(number) && number>=bound(range->as_array()[0]) && number<=bound(range->as_array()[1]),"xui_value",name);
    }
}
void compareXui(const Tree::value_type& baseline,const Tree::value_type& candidate,const std::string& selector,const std::map<std::string,std::string>& colors,const std::map<std::string,std::string>& textures)
{
    const auto left=children(baseline.second),right=children(candidate.second);
    require(baseline.first==candidate.first && left.size()==right.size(),"xui_structure",selector);
    std::map<std::string,std::string> first,second;
    if(const auto attributes=baseline.second.get_child_optional("<xmlattr>"))for(const auto& a:*attributes)first[a.first]=a.second.data();
    if(const auto attributes=candidate.second.get_child_optional("<xmlattr>"))for(const auto& a:*attributes)second[a.first]=a.second.data();
    std::set<std::string> names;for(const auto& a:first)names.insert(a.first);for(const auto& a:second)names.insert(a.first);
    for(const auto& name:names)
    {
        const auto before=first.find(name),after=second.find(name);
        if(before==first.end() || after==second.end() || before->second!=after->second)
        {
            require(xuiAttribute(baseline.first,name),"xui_attribute",selector+"/@"+name);
            if(after!=second.end())xuiValue(name,after->second,colors,textures);
        }
    }
    require(baseline.second.data()==candidate.second.data() || xuiTextElements.count(baseline.first),"xui_structure",selector+"/text()");
    static const auto textLimit=boost::json::parse(ThemeRulesJson).as_object().at("value_rules").as_object().at("text_limit").as_int64();
    require(candidate.second.data().size()<=static_cast<std::size_t>(textLimit),"xui_value",selector+"/text()");
    for(std::size_t i=0;i<left.size();++i)compareXui(*left[i],*right[i],selector+"/"+left[i]->first,colors,textures);
}
std::string resolve(const fs::path& base,const std::vector<std::string>& roots,const std::string& name)
{std::string found;for(const auto& root:roots){const auto path=contained(base,root+"/"+name);if(fs::is_regular_file(path))found=toUtf8(path);}return found;}
void color(const std::string& value)
{
    std::istringstream input(value);input.imbue(std::locale::classic());double number;
    for(int i=0;i<4;++i)require(static_cast<bool>(input>>number) && std::isfinite(number) && number>=0 && number<=1,"color",value);
    std::string extra;require(!(input>>extra),"color",value);
}
}
bool validHash(const std::string& value)
{return value.size()==64 && value.find_first_not_of("0123456789abcdef")==std::string::npos;}
void pathName(const std::string& path)
{
    require(!path.empty() && fromUtf8(path).wstring().size()<=240 && path.front()!='/' && path.find('\\')==std::string::npos,"path",path);
    std::size_t begin=0;
    do
    {
        const auto end=path.find('/',begin);const auto part=path.substr(begin,end==std::string::npos?end:end-begin);
        require(!part.empty() && part!="." && part!=".." && part.back()!='.' && part.back()!=' ',"path",path);
        require(std::none_of(part.begin(),part.end(),[](unsigned char c){return c<32 || std::string("<>:\"|?*").find(c)!=std::string::npos;}),"path",path);
        auto stem=part.substr(0,part.find('.'));for(auto& c:stem)c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        require(stem!="CON" && stem!="PRN" && stem!="AUX" && stem!="NUL" && !(stem.size()==4 && (stem.rfind("COM",0)==0 || stem.rfind("LPT",0)==0) && stem[3]>='1' && stem[3]<='9'),"path",path);
        require(stem!="COM\xC2\xB9" && stem!="COM\xC2\xB2" && stem!="COM\xC2\xB3" && stem!="LPT\xC2\xB9" && stem!="LPT\xC2\xB2" && stem!="LPT\xC2\xB3","path",path);
        if(end==std::string::npos)break;begin=end+1;
    }while(true);
}
std::string hash(const std::string& bytes)
{
    std::array<unsigned char,32> digest{};
#if defined(_WIN32)
    struct Provider
    {
        BCRYPT_ALG_HANDLE value=nullptr;
        Provider(){require(BCryptOpenAlgorithmProvider(&value,BCRYPT_SHA256_ALGORITHM,nullptr,0)==0,"integrity","SHA-256 provider unavailable");}
        ~Provider(){if(value)BCryptCloseAlgorithmProvider(value,0);}
    };
    static Provider provider;
    std::vector<unsigned char> object;
    struct HashHandle {BCRYPT_HASH_HANDLE value=nullptr;~HashHandle(){if(value)BCryptDestroyHash(value);}} handle;
    DWORD objectSize=0,resultSize=0;
    require(BCryptGetProperty(provider.value,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&resultSize,0)==0,"integrity","SHA-256 object size unavailable");
    object.resize(objectSize);
    require(BCryptCreateHash(provider.value,&handle.value,object.data(),objectSize,nullptr,0,0)==0 &&
        BCryptHashData(handle.value,reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),static_cast<ULONG>(bytes.size()),0)==0 &&
        BCryptFinishHash(handle.value,digest.data(),static_cast<ULONG>(digest.size()),0)==0,"integrity","SHA-256 failed");
#else
    SHA256(reinterpret_cast<const unsigned char*>(bytes.data()),bytes.size(),digest.data());
#endif
    static constexpr char hex[]="0123456789abcdef";std::string result;result.reserve(64);
    for(auto byte:digest){result+=hex[byte>>4];result+=hex[byte&15];}return result;
}
std::string readFile(const fs::path& inputPath,std::uint64_t maximum)
{
    const auto path=nativePath(inputPath);
    std::error_code error;const auto size=fs::file_size(path,error);require(!error && size<=maximum,"size",toUtf8(path));
    std::ifstream input(path,std::ios::binary);require(static_cast<bool>(input),"integrity",toUtf8(path));
    std::string result(static_cast<std::size_t>(size),'\0');input.read(result.data(),static_cast<std::streamsize>(size));
    require(static_cast<std::uint64_t>(input.gcount())==size && input.peek()==std::char_traits<char>::eof(),"integrity",toUtf8(path));return result;
}
std::string hashFile(const fs::path& path)
{return hash(readFile(path,ExpandedLimit));}
boost::json::value parse(const std::string& bytes)
{
    require(bytes.size()<=ManifestLimit,"size","JSON exceeds 4 MiB");boost::system::error_code error;
    boost::json::parse_options options;options.max_depth=64;
    auto value=boost::json::parse(bytes,error,{},options);require(!error,"json",error.message());
    std::vector<std::set<std::string>> stack;
    for(std::size_t i=0;i<bytes.size();++i)
    {
        if(bytes[i]=='{')stack.emplace_back();else if(bytes[i]=='}'){require(!stack.empty(),"json","Unbalanced object");stack.pop_back();}
        else if(bytes[i]=='"')
        {
            const auto start=i++;for(;i<bytes.size();++i){if(bytes[i]=='\\')++i;else if(bytes[i]=='"')break;}
            auto after=i+1;while(after<bytes.size() && std::isspace(static_cast<unsigned char>(bytes[after])))++after;
            if(after<bytes.size() && bytes[after]==':')
            {const auto key=boost::json::parse(bytes.substr(start,i-start+1)).as_string();require(!stack.empty() && stack.back().emplace(key).second,"duplicate_key",std::string(key));}
        }
    }
    return value;
}
HostPalette hostPalette(const std::string& bytes)
{
    require(bytes.size()<=4096,"size","Host palette exceeds4 KiB");
    require(bytes.rfind("\xef\xbb\xbf",0)!=0,"json","Host palette must not contain a UTF-8 BOM");
    const auto value=parse(bytes);require(!value.is_null(),"json","Null host palette");require(value.is_object(),"schema","Expected host palette object");
    const auto& object=value.as_object();fields(object,{"format_version","colors"});
    const auto version=integer(object,"format_version");require(version>=INT32_MIN && version<=INT32_MAX,"schema","format_version");
    const auto* colors=object.if_contains("colors");require(colors && (colors->is_object() || colors->is_null()),"schema","colors");
    if(colors->is_object())for(const auto& field:colors->as_object())require(field.value().is_string() || field.value().is_null(),"schema",std::string(field.key()));
    require(version==1,"host_palette_version","Unsupported host palette format");
    static const std::set<std::string> roles{"background","panel","input","text","muted","accent","border"};
    require(colors->is_object() && colors->as_object().size()==roles.size(),"host_palette","Define the seven host color roles");
    HostPalette result;
    for(const auto& field:colors->as_object())
    {
        require(roles.count(std::string(field.key())) && field.value().is_string(),"host_palette","Invalid host color role/value");
        const auto hex=field.value().as_string();require(hex.size()==7 && hex[0]=='#',"host_palette","Host colors use #RRGGBB");
        std::uint32_t rgb=0;
        for(std::size_t index=1;index<hex.size();++index)
        {
            const auto digit=hex[index];const int part=digit>='0' && digit<='9'?digit-'0':digit>='a' && digit<='f'?digit-'a'+10:digit>='A' && digit<='F'?digit-'A'+10:-1;
            require(part>=0,"host_palette","Host colors use #RRGGBB");rgb=(rgb<<4)|static_cast<std::uint32_t>(part);
        }
        result.colors.emplace(std::string(field.key()),rgb);
    }
    return result;
}
CaptureRequest captureRequest(const std::string& bytes,const std::string& requestId,std::int64_t generation,const std::string& revision)
{
    require(bytes.size()<=4096,"size","Capture request exceeds 4096 bytes");
    const auto value=parse(bytes);require(value.is_object(),"capture_request","Expected capture object");const auto& object=value.as_object();
    fields(object,{"capture_version","request_id","generation","revision","capture_id","max_width","max_height"});
    const auto owner=text(object,"request_id"),selected=text(object,"revision"),id=text(object,"capture_id");
    const auto version=integer(object,"capture_version"),counter=integer(object,"generation"),width=integer(object,"max_width"),height=integer(object,"max_height");
    require(version==1 && validHash(owner) && validHash(id) && (selected.empty() || validHash(selected)) && counter>0 && width>0 && width<=2048 && height>0 && height<=2048,"capture_request","Invalid capture request");
    require(owner==requestId && counter==generation && selected==revision,"capture_ownership","Capture belongs to a different preview");
    return {id,static_cast<int>(width),static_cast<int>(height)};
}
fs::path capturePath(const fs::path& directory,const std::string& leaf)
{
    const auto name=leaf.size()>4 && leaf.substr(leaf.size()-4)==".tmp"?leaf.substr(0,leaf.size()-4):leaf;
    const bool image=name.size()==76 && name.substr(0,8)=="capture-" && name.substr(72)==".png" && validHash(name.substr(8,64));
    require(name=="capture-request.json" || name=="capture-status.json" || image,"capture_request","Unsupported capture filename");
    return contained(directory,leaf);
}
Manifest manifest(const std::string& bytes)
{
    auto parsed=parse(bytes);require(parsed.is_object(),"schema","Manifest must be an object");const auto& object=parsed.as_object();
    fields(object,{"format_version","theme_id","package_version","display_name","base_skin","base_scheme","adapter_id","adapter_version","required_capabilities","base_fingerprint","files","font_dependencies","author","description","extensions"});
    Manifest result;
    result.id=text(object,"theme_id");result.version=text(object,"package_version");result.name=text(object,"display_name");
    require(result.version.size()<=128,"identity","Version is too long");
    result.skin=text(object,"base_skin");result.scheme=text(object,"base_scheme");const auto adapter=text(object,"adapter_id");result.fingerprint=text(object,"base_fingerprint");
    const auto& files=array(object,"files");const auto& capabilities=array(object,"required_capabilities");const auto& fonts=array(object,"font_dependencies");
    for(const auto& value:files)
    {
        require(value.is_object(),"schema","Invalid payload entry");const auto& item=value.as_object();fields(item,{"path","size","sha256","kind"});
        text(item,"path");text(item,"sha256");text(item,"kind");integer(item,"size");
    }
    for(const auto& value:fonts)
    {
        require(value.is_object(),"schema","Invalid font entry");const auto& item=value.as_object();fields(item,{"kind","file","face"});
        text(item,"kind");text(item,"file");if(item.contains("face")){const auto face=integer(item,"face");require(face>=INT32_MIN && face<=INT32_MAX,"schema","face");}
    }
    for(const auto* field:{"author","description"})if(object.contains(field))text(object,field);
    if(object.contains("extensions"))require(object.at("extensions").is_object(),"schema","Invalid metadata");
    integer(object,"adapter_version");
    require(integer(object,"format_version")==1,"format_version","Unsupported format");
    require(std::regex_match(result.id,std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}")),"identity","Canonical UUID required");
    require(std::regex_match(result.version,std::regex("(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?(\\+[0-9A-Za-z-]+(\\.[0-9A-Za-z-]+)*)?")),"identity","Invalid package version");
    const auto release=result.version.substr(0,result.version.find('+'));const auto prerelease=release.find('-');
    if(prerelease!=std::string::npos){std::istringstream parts(release.substr(prerelease+1));std::string part;
        while(std::getline(parts,part,'.'))require(!(part.size()>1 && part[0]=='0' && part.find_first_not_of("0123456789")==std::string::npos),"identity","Invalid numeric prerelease");}
    const auto scalars=std::count_if(result.name.begin(),result.name.end(),[](unsigned char c){return (c&0xc0)!=0x80;});
    require(scalars>0 && scalars<=128 && std::none_of(result.name.begin(),result.name.end(),[](unsigned char c){return c<32 || c==127;}),"identity","Invalid display name");
    for(std::size_t i=1;i<result.name.size();++i)require(!(static_cast<unsigned char>(result.name[i-1])==0xc2 && static_cast<unsigned char>(result.name[i])>=0x80 && static_cast<unsigned char>(result.name[i])<=0x9f),"identity","Invalid display name");
    require(result.skin=="ansastorm" && adapter=="ansastorm-classic" && integer(object,"adapter_version")==1,"base","Unsupported base adapter");
    if(!result.scheme.empty())pathName(result.scheme);require(result.scheme.find('/')==std::string::npos,"base","Invalid scheme ID");
    std::set<std::string> seenCapabilities;
    for(const auto& capability:capabilities)
    {require(capability.is_string(),"capability","Invalid capability");const std::string name(capability.as_string());require((name=="viewer.overlay.v1" || name=="viewer.preview.v1" || name=="viewer.fonts.v1" || name=="viewer.xui.v1" || name=="host.palette.v1") && seenCapabilities.insert(name).second,"capability",name);}
    require(validHash(result.fingerprint),"schema","Invalid base fingerprint");
    require(!files.empty() && files.size()<10000,"size","Invalid payload count");std::uint64_t expanded=0;std::string previous;std::set<std::string,CasePathLess> filePaths;
    for(const auto& value:files)
    {
        require(value.is_object(),"schema","Invalid payload entry");const auto& item=value.as_object();fields(item,{"path","size","sha256","kind"});
        File file;file.path=text(item,"path");file.sha256=text(item,"sha256");file.kind=text(item,"kind");pathName(file.path);
        require(filePaths.insert(file.path).second,"collision",file.path);
        require(previous.empty() || previous<file.path,"schema","Payload list must be path-sorted");previous=file.path;
        const auto size=integer(item,"size");require(size>=0 && static_cast<std::uint64_t>(size)<=limit(file.kind) && validHash(file.sha256),"size",file.path);file.size=static_cast<std::uint64_t>(size);
        require(file.kind==kind(file.path),"kind",file.path);expanded+=file.size;result.files.push_back(std::move(file));
    }
    require(expanded<=ExpandedLimit && std::any_of(result.files.begin(),result.files.end(),[](const File& f){return f.path=="base-dependencies.json";}),"size","Invalid expanded package or missing dependencies");
    for(const auto& value:fonts)
    {
        require(value.is_object(),"schema","Invalid font entry");const auto& item=value.as_object();fields(item,{"kind","file","face"});
        const auto type=text(item,"kind"),file=text(item,"file");const auto face=item.contains("face")?integer(item,"face"):0;
        require((type=="embedded" || type=="installed") && face>=0 && face<=128,"font","Invalid dependency");pathName(file);
        require(type!="installed" || file.find('/')==std::string::npos,"font","Installed font must be a filename");
        if(type=="embedded")require(std::any_of(result.files.begin(),result.files.end(),[&](const File& f){return f.path=="viewer/fonts/"+file && f.kind=="font";}),"font","Missing embedded font: "+file);
    }
    for(const auto& field:std::vector<std::pair<const char*,std::size_t>>{{"author",1024},{"description",32768}})
        if(object.contains(field.first)){const auto value=text(object,field.first);require(static_cast<std::size_t>(std::count_if(value.begin(),value.end(),[](unsigned char c){return (c&0xc0)!=0x80;}))<=field.second,"schema","Invalid metadata");}
    if(object.contains("extensions"))require(object.at("extensions").is_object(),"schema","Invalid metadata");
    result.document=object;return result;
}
Revision load(const fs::path& directoryInput,const fs::path& baseInput)
{
    const auto directory=nativePath(directoryInput),base=nativePath(baseInput);
    const auto manifestBytes=readFile(contained(directory,"theme.json"));auto data=manifest(manifestBytes);
    std::set<std::string> expected{"theme.json"};
    for(const auto& file:data.files)
    {
        const auto path=contained(directory,file.path);require(fs::is_regular_file(path) && fs::file_size(path)==file.size,"integrity",file.path+" (size)");
        const auto actualHash=hashFile(path);require(actualHash==file.sha256,"integrity",file.path+" (hash "+actualHash+" != "+file.sha256+")");expected.insert(file.path);
    }
    std::size_t count=0;
    for(const auto& entry:fs::recursive_directory_iterator(directory))
    {require(!linked(entry.path()),"link",toUtf8(entry.path()));if(entry.is_regular_file()){++count;require(expected.count(toUtf8(entry.path().lexically_relative(directory),true)),"coverage","Unlisted revision payload");}}
    require(count==expected.size(),"coverage","Missing revision payload");
    const auto dependencyBytes=readFile(contained(directory,"base-dependencies.json"));require(hash(dependencyBytes)==data.fingerprint,"integrity","Base fingerprint");
    const auto dependencyValue=parse(dependencyBytes);require(dependencyValue.is_object(),"base","Invalid dependency set");const auto& dependencies=dependencyValue.as_object();
    require(integer(dependencies,"format_version")==1 && text(dependencies,"base_skin")==data.skin && text(dependencies,"base_scheme")==data.scheme,"base","Invalid dependency set");
    std::vector<std::string> roots{"skins/default","skins/ansastorm"};if(!data.scheme.empty())roots.push_back("skins/ansastorm/themes/"+data.scheme);
    const auto& recordedRoots=array(dependencies,"roots");require(recordedRoots.size()==roots.size(),"base","Invalid roots");
    for(std::size_t i=0;i<roots.size();++i)require(recordedRoots[i].is_string() && recordedRoots[i].as_string()==roots[i],"base","Invalid roots");
    std::vector<std::string> dependencyPaths;std::set<std::string,CasePathLess> dependencyNames;
    for(const auto& value:array(dependencies,"files"))
    {
        require(value.is_object(),"base","Invalid dependency record");const auto& item=value.as_object();const auto path=text(item,"path");pathName(path);
        require(dependencyNames.insert(path).second,"base",path);dependencyPaths.push_back(path);
        require(path.rfind("fonts/",0)==0 || std::any_of(roots.begin(),roots.end(),[&](const std::string& root){return path.rfind(root+"/",0)==0;}),"base",path);
        const auto* present=item.if_contains("present");require(present && present->is_bool(),"base",path);const auto size=integer(item,"size"),layer=integer(item,"layer");const auto digest=text(item,"sha256");
        require(size>=0 && layer>=0 && (present->as_bool()?validHash(digest):size==0 && digest.empty()),"base",path);
        const auto actual=contained(base,path);const bool exists=fs::is_regular_file(actual);
        require(exists==present->as_bool() && (!exists || fs::file_size(actual)==static_cast<std::uint64_t>(size) && hashFile(actual)==digest),"base_changed",path);
    }
    std::set<std::string> logical,closure;
    for(const auto& root:roots)
    {
        const auto rootPath=contained(base,root);
        if(fs::is_regular_file(rootPath/"colors.xml"))logical.insert("colors.xml");
        for(const auto* prefix:{"textures","xui/en"})
        {
            const auto folder=contained(base,root+"/"+prefix);if(!fs::is_directory(folder))continue;
            for(const auto& entry:fs::recursive_directory_iterator(folder))
            {
                require(!linked(entry.path()),"link",toUtf8(entry.path()));if(!entry.is_regular_file())continue;
                logical.insert(toUtf8(entry.path().lexically_relative(rootPath),true));
            }
        }
    }
    for(const auto& name:logical)for(const auto& root:roots)closure.insert(root+"/"+name);
    if(fs::is_directory(base/"fonts"))for(const auto& entry:fs::recursive_directory_iterator(base/"fonts"))
        if(entry.is_regular_file())closure.insert(toUtf8(entry.path().lexically_relative(base),true));
    require(closure==std::set<std::string>(dependencyPaths.begin(),dependencyPaths.end()),"base_changed","Dependency closure differs");
    std::map<std::string,std::string> baseColors,baseTextures;
    for(const auto& root:roots)
    {
        const auto colors=contained(base,root+"/colors.xml");
        if(fs::is_regular_file(colors)){const auto tree=xml(readFile(colors));for(const auto& node:tree.get_child("colors"))
            if(node.first=="color")baseColors[node.second.get<std::string>("<xmlattr>.name","")]=node.second.get<std::string>("<xmlattr>.reference","");}
        const auto textures=contained(base,root+"/textures/textures.xml");
        if(fs::is_regular_file(textures)){const auto tree=xml(readFile(textures));for(const auto& node:tree.get_child("textures"))
            if(node.first=="texture")baseTextures[node.second.get<std::string>("<xmlattr>.name","")]=node.second.get<std::string>("<xmlattr>.file_name","");}
    }
    for(const auto& file:data.files)
    {
        const auto path=contained(directory,file.path);
        if(file.kind=="colors")
        {
            const auto tree=xml(readFile(path));require(tree.size()==1 && tree.begin()->first=="colors","xml","Expected colors");std::map<std::string,std::string> refs;
            for(const auto& node:tree.begin()->second)
            {
                require(node.first=="color","color","Invalid color entry");const auto name=node.second.get<std::string>("<xmlattr>.name","");
                const auto value=node.second.get_optional<std::string>("<xmlattr>.value"),reference=node.second.get_optional<std::string>("<xmlattr>.reference");
                require(baseColors.count(name) && !refs.count(name),"color",name);require(static_cast<bool>(value)!=static_cast<bool>(reference),"color","Specify a value or reference");
                refs[name]=reference?*reference:"";if(value)color(*value);
                for(const auto& a:node.second.get_child("<xmlattr>"))require(a.first=="name" || a.first=="value" || a.first=="reference","color",a.first);
                require(children(node.second).empty(),"color","Nested color payload");
            }
            for(const auto& entry:refs)
            {std::set<std::string> visited{entry.first};auto current=entry.second;while(!current.empty())
                {require(visited.insert(current).second,"color_cycle",entry.first);const auto found=refs.find(current);if(found==refs.end()){require(baseColors.count(current),"reference",current);break;}current=found->second;}}
        }
        else if(file.kind=="xui")
        {
            const auto relative=file.path.substr(7);const auto resolved=resolve(base,roots,relative);require(!resolved.empty(),"reference",relative);
            const auto left=xml(readFile(fromUtf8(resolved))),right=xml(readFile(path));require(left.size()==1 && right.size()==1,"xui_structure",relative);compareXui(*left.begin(),*right.begin(),relative,baseColors,baseTextures);
        }
        else if(file.kind=="image")
        {
            require(extension(file.path)==".png","image","Native dimension inspection required before accepting: "+file.path);const auto bytes=readFile(path,limit(file.kind));
            require(bytes.size()>=33 && bytes.compare(0,8,"\x89PNG\r\n\x1a\n",8)==0 && bytes.compare(12,4,"IHDR")==0,"image","Invalid PNG");
            auto dimension=[&](int offset){std::uint32_t result=0;for(int i=0;i<4;++i)result=(result<<8)|static_cast<unsigned char>(bytes[static_cast<std::size_t>(offset+i)]);return result;};
            const auto width=dimension(16),height=dimension(20);require(width && height && static_cast<std::uint64_t>(width)*height<=16000000,"image","Image exceeds 16 megapixels");
        }
        else if(file.kind=="host_palette")throw Error("capability","Host palette activation is not implemented yet");
        else if(file.kind=="textures")
        {
            const auto tree=xml(readFile(path));require(tree.size()==1 && tree.begin()->first=="textures","xml","Expected textures");std::set<std::string> names;
            for(const auto& node:tree.begin()->second)
            {
                if(node.first=="<xmlattr>")continue;
                const auto name=node.second.get<std::string>("<xmlattr>.name","");const auto filename=node.second.get<std::string>("<xmlattr>.file_name","");
                require(node.first=="texture" && !name.empty() && !filename.empty() && names.insert(name).second && children(node.second).empty(),"texture","Invalid registration");
                pathName(filename);require(baseTextures.count(name),"texture","Unknown texture: "+name);
                require(expected.count("viewer/textures/"+filename) || !resolve(base,roots,"textures/"+filename).empty(),"reference","Missing image: "+filename);
                for(const auto& attribute:node.second.get_child("<xmlattr>"))
                {
                    const auto& key=attribute.first;
                    require(key=="name" || key=="file_name" || key=="preload" || key=="scale.left" || key=="scale.top" || key=="scale.right" || key=="scale.bottom" || key=="clip.left" || key=="clip.top" || key=="clip.right" || key=="clip.bottom","texture",key);
                    if(key.rfind("scale.",0)==0 || key.rfind("clip.",0)==0)
                    {std::istringstream input(attribute.second.data());input.imbue(std::locale::classic());double number;std::string extra;require(static_cast<bool>(input>>number) && !(input>>extra) && std::isfinite(number) && number>=0 && number<=16384,"texture",key);}
                }
                for(const auto* prefix:{"scale.","clip."})for(const auto& edges:{std::pair{"left","right"},std::pair{"bottom","top"}})
                {
                    // Attribute names contain dots, so read them directly rather than as property-tree paths.
                    const auto& attributes=node.second.get_child("<xmlattr>");
                    const auto low=attributes.find(std::string(prefix)+edges.first),high=attributes.find(std::string(prefix)+edges.second);
                    if(low!=attributes.not_found() && high!=attributes.not_found())require(std::stod(low->second.data())<=std::stod(high->second.data()),"texture","Reversed slice bounds");
                }
            }
        }
        else if(file.kind=="font_config")
        {
            const auto tree=xml(readFile(path));require(tree.size()==1 && tree.begin()->first=="fonts","font","Expected fonts");
            std::function<void(const Tree&,const std::string&)> inspect;
            inspect=[&](const Tree& parent,const std::string& tag)
            {
                for(const auto& node:parent)
                {
                    if(node.first=="<xmlattr>")continue;
                    require(node.first=="font" || node.first=="font_size" || node.first=="file" || node.first=="os","font","Unsupported font structure");
                    if(tag=="fonts")require(node.first=="font" || node.first=="font_size","font","Unsupported font structure");
                    if(const auto attributes=node.second.get_child_optional("<xmlattr>"))for(const auto& attribute:*attributes)
                    {
                        const auto& name=attribute.first;
                        if(node.first=="font_size" && name=="size" || node.first=="file" && (name=="size_delta" || name=="font_weight"))
                        {
                            std::istringstream input(attribute.second.data());input.imbue(std::locale::classic());double number;std::string extra;
                            const auto minimum=name=="size"?1.0:name=="size_delta"?-64.0:1.0,maximum=name=="size"?256.0:name=="size_delta"?64.0:1000.0;
                            require(static_cast<bool>(input>>number) && !(input>>extra) && std::isfinite(number) && number>=minimum && number<=maximum && (name!="font_weight" || number==std::trunc(number)),"font","Invalid "+name);
                        }
                    }
                    if(node.first=="file")
                    {
                        const auto& filename=node.second.data();pathName(filename);
                        require(filename.find('/')==std::string::npos,"font","Font must be a filename");
                        bool declared=fs::is_regular_file(contained(base,"fonts/"+filename));
                        for(const auto& dependency:array(data.document,"font_dependencies"))if(text(dependency.as_object(),"file")==filename)declared=true;
                        require(declared,"font","Undeclared font: "+filename);
                    }
                    inspect(node.second,node.first);
                }
            };
            inspect(tree.begin()->second,"fonts");
        }
    }
    return {std::move(data),hash(manifestBytes),directory};
}
}
