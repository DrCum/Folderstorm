/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_POSE_XML_H
#define FS_POSE_XML_H
#include "fsposepolicy.h"
#ifdef LL_USESYSTEMLIBS
# include <expat.h>
#else
# include <expat/expat.h>
#endif
#include <cctype>
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>
namespace fs_pose {
// LLSDXMLParser may stop at the first root and ignores its depth argument.
// Preflight the entire bounded byte string with an independent XML parser,
// including duplicate keys and the supported rotation-only LLSD shape.
class PresetXML
{
 struct Frame {std::string tag,text,key,kind;std::set<std::string> keys;unsigned children=0;bool value=false;};
 XML_Parser mParser=nullptr;std::vector<Frame> mStack;bool mFailed=false;unsigned mNodes=0,mRoots=0;
 void fail(){mFailed=true;XML_StopParser(mParser,XML_FALSE);}
 static bool whitespace(const std::string& value)
 {for(unsigned char c:value)if(c!=' ' && c!='\t' && c!='\r' && c!='\n')return false;return true;}
 static void XMLCALL start(void* data,const XML_Char* tag,const XML_Char** attrs)
 {static_cast<PresetXML*>(data)->start(tag,attrs);}
 static void XMLCALL end(void* data,const XML_Char*){static_cast<PresetXML*>(data)->end();}
 static void XMLCALL text(void* data,const XML_Char* value,int length)
 {
  auto& self=*static_cast<PresetXML*>(data);if(self.mFailed)return;
  if(self.mStack.empty()){if(!whitespace(std::string(value,length)))self.fail();return;}
  auto& frame=self.mStack.back();
  const std::size_t limit=frame.tag=="key"?64:frame.tag=="real"?64:16;
  if(frame.tag=="llsd" || frame.tag=="map" || frame.tag=="array")
  {if(!whitespace(std::string(value,length)))self.fail();return;}
  if(frame.text.size()+static_cast<std::size_t>(length)>limit){self.fail();return;}frame.text.append(value,length);
 }
 static void XMLCALL doctype(void* data,const XML_Char*,const XML_Char*,const XML_Char*,int)
 {static_cast<PresetXML*>(data)->fail();}
 static void XMLCALL processing(void* data,const XML_Char*,const XML_Char*){static_cast<PresetXML*>(data)->fail();}
 static void XMLCALL comment(void* data,const XML_Char*){static_cast<PresetXML*>(data)->fail();}
 void start(const std::string& tag,const XML_Char** attrs)
 {
  if(mFailed)return;
  if(*attrs || mStack.size()>=8 || ++mNodes>1024){fail();return;}
  Frame next;next.tag=tag;
  if(mStack.empty())
  {if(tag!="llsd" || ++mRoots!=1){fail();return;}}
  else
  {
   auto& parent=mStack.back();++parent.children;
   if(parent.tag=="llsd")
   {if(tag!="map" || parent.children!=1){fail();return;}next.kind="pose";}
   else if(parent.tag=="map")
   {
    if(!parent.value)
    {if(tag!="key"){fail();return;}}
    else
    {
     if(parent.kind=="pose")
     {
      if(tag!="map"){fail();return;}
      next.kind=parent.key=="version"?"version":parent.key=="startFromTeePose"?"start":"joint";
     }
     else if(parent.kind=="version") {if(tag!="integer"){fail();return;}}
     else if(parent.kind=="start") {if(tag!="boolean"){fail();return;}}
     else if(parent.kind=="joint")
     {if(tag!=(parent.key=="rotation"?"array":"boolean")){fail();return;}}
     else {fail();return;}
     parent.value=false;
    }
   }
   else if(parent.tag=="array") {if(tag!="real" && tag!="integer"){fail();return;}if(parent.children>3){fail();return;}}
   else {fail();return;}
  }
  mStack.push_back(std::move(next));
 }
 void end()
 {
  if(mFailed || mStack.empty())return;
  const auto frame=std::move(mStack.back());mStack.pop_back();
  if(frame.tag=="key")
  {
   if(mStack.empty() || mStack.back().tag!="map"){fail();return;}auto& map=mStack.back();
   if(frame.text.empty() || !map.keys.insert(frame.text).second){fail();return;}
   if(map.kind=="pose")
   {if(map.keys.size()>jointLimit+2 || (frame.text!="version" && frame.text!="startFromTeePose" && !jointName(frame.text))){fail();return;}}
   else if(map.kind=="joint")
   {if(frame.text!="rotation" && frame.text!="enabled" && frame.text!="jointBaseRotationIsZero" && frame.text!="userSetBaseRotationToZero"){fail();return;}}
   else if(frame.text!="value"){fail();return;}
   map.key=frame.text;map.value=true;
  }
  else if(frame.tag=="boolean" || frame.tag=="integer" || frame.tag=="real")
  {
   const auto first=frame.text.find_first_not_of(" \t\r\n"),last=frame.text.find_last_not_of(" \t\r\n");
   if(first==std::string::npos){fail();return;}const auto scalar=frame.text.substr(first,last-first+1);
   if(frame.tag=="boolean")
   {if(scalar!="true" && scalar!="false" && scalar!="0" && scalar!="1"){fail();return;}}
   else if(frame.tag=="integer")
   {int value=0;const auto parsed=std::from_chars(scalar.data(),scalar.data()+scalar.size(),value);if(parsed.ec!=std::errc() || parsed.ptr!=scalar.data()+scalar.size()){fail();return;}}
   else
   {char* end=nullptr;errno=0;const auto value=std::strtod(scalar.c_str(),&end);if(errno || end!=scalar.c_str()+scalar.size() || !std::isfinite(value)){fail();return;}}
  }
  else if(frame.tag=="map")
  {
   if(frame.value || frame.keys.empty()){fail();return;}
   if(frame.kind=="pose" && (!frame.keys.count("version") || !frame.keys.count("startFromTeePose"))){fail();return;}
   if(frame.kind=="joint" && (!frame.keys.count("rotation") || !frame.keys.count("jointBaseRotationIsZero"))){fail();return;}
  }
  else if(frame.tag=="array" && frame.children!=3){fail();return;}
  else if(frame.tag=="llsd" && frame.children!=1){fail();return;}
 }
public:
 bool valid(const std::string& bytes)
 {
  if(bytes.empty() || bytes.size()>presetLimit || bytes.find('\0')!=std::string::npos)return false;
  mParser=XML_ParserCreate("UTF-8");if(!mParser)return false;
  XML_SetUserData(mParser,this);XML_SetElementHandler(mParser,start,end);XML_SetCharacterDataHandler(mParser,text);
  XML_SetStartDoctypeDeclHandler(mParser,doctype);XML_SetProcessingInstructionHandler(mParser,processing);XML_SetCommentHandler(mParser,comment);
  const bool parsed=XML_Parse(mParser,bytes.data(),static_cast<int>(bytes.size()),XML_TRUE)==XML_STATUS_OK;
  XML_ParserFree(mParser);mParser=nullptr;return parsed && !mFailed && mRoots==1 && mStack.empty();
 }
};
inline bool presetXML(const std::string& bytes){PresetXML parser;return parser.valid(bytes);}
}
#endif
