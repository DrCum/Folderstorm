/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsposexml.h"
#include <cassert>
#include <iostream>
std::string pose(const std::string& fields="<key>rotation</key><array><real>0</real><real>1</real><real>-0.5</real></array><key>jointBaseRotationIsZero</key><boolean>true</boolean>")
{return "<?xml version=\"1.0\"?><llsd><map><key>version</key><map><key>value</key><integer>8</integer></map><key>startFromTeePose</key><map><key>value</key><boolean>false</boolean></map><key>mHead</key><map>"+fields+"</map></map></llsd>";}
int main()
{
 using fs_pose::presetXML;const auto valid=pose();assert(presetXML(valid));assert(presetXML(valid+" \n\t"));
 assert(!presetXML(valid+"<llsd><map><key>position</key><real>42</real></map></llsd>"));
 assert(!presetXML(valid+"garbage"));assert(!presetXML("<!DOCTYPE llsd [<!ENTITY a 'oops'>]>"+valid));
 assert(!presetXML(pose("<key>rotation</key><array><real>0</real><real>0</real><real>0</real></array><key>rotation</key><array><real>1</real><real>1</real><real>1</real></array>")));
 assert(!presetXML(pose("<key>position</key><array><real>0</real><real>0</real><real>0</real></array>")));
 assert(!presetXML(pose("<key>rotation</key><array><real>"+std::string(4096,'1')+"</real><real>0</real><real>0</real></array>")));
 assert(!presetXML(pose("<key>rotation</key><array><real>nan</real><real>0</real><real>0</real></array>")));
 std::string deep;for(int i=0;i<100;++i)deep+="<map>";for(int i=0;i<100;++i)deep+="</map>";assert(!presetXML("<llsd>"+deep+"</llsd>"));
 assert(!presetXML(pose("<key>rotation</key><array><real>1oops</real><real>0</real><real>0</real></array>")));
 assert(!presetXML(pose("<key>rotation</key><array><real>0</real><real>0</real><real>0</real></array><key>jointBaseRotationIsZero</key><boolean>oops</boolean>")));
 auto dup=valid;dup.insert(dup.rfind("</map></llsd>"),"<key>version</key><map><key>value</key><integer>8</integer></map>");assert(!presetXML(dup));
 assert(!presetXML("<llsd attr='ignored'><map/></llsd>"));
 assert(!presetXML(valid+std::string("\0",1)));
 std::cout<<"strict whole-document pose XML checks passed\n";
}
