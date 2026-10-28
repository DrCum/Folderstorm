/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "linden_common.h"
#include "lldir.h"
#include "lluicolor.h"
#include "lluicolortable.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <cmath>
#include <sstream>

namespace
{
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::filesystem::path utf8Path(const std::string& value)
{
    std::u8string encoded;encoded.reserve(value.size());
    for(unsigned char byte:value)encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
}
// Only directory resolution is isolated. The color table, serialization and reset
// methods under test are linked from the matching production llui library.
class FixtureDir final : public LLDir
{
public:
    FixtureDir(const std::string& profile,const std::string& base)
    {mOSUserAppDir=profile;mOSUserDir=profile;mAppRODataDir=base;mDirDelimiter="\\";setSkinFolder("default","","en");}
    void initAppDirs(const std::string&,const std::string&) override {}
    U32 countFilesInDir(const std::string&,const std::string&) override {return 0;}
    bool getNextFileInDir(const std::string&,const std::string&,std::string&) override {return false;}
    std::string getCurPath() override {return mOSUserAppDir;}
    bool fileExists(const std::string& path) const override {return std::filesystem::exists(utf8Path(path));}
    std::string getLLPluginLauncher() override {return {};}
    std::string getLLPluginFilename(std::string) override {return {};}
};
struct UseDir
{
    LLDir* before=gDirUtilp;
    explicit UseDir(LLDir& value){gDirUtilp=&value;}
    ~UseDir(){gDirUtilp=before;}
};
std::map<std::string,std::string> saved(const std::filesystem::path& file)
{
    boost::property_tree::ptree xml;boost::property_tree::read_xml(file.string(),xml);
    std::map<std::string,std::string> result;
    for(const auto& child:xml.get_child("colors"))if(child.first=="color")result.emplace(child.second.get<std::string>("<xmlattr>.name"),child.second.get<std::string>("<xmlattr>.value"));
    return result;
}
bool equalColor(const std::string& value,const LLColor4& expected)
{
    std::istringstream input(value);F32 channel=0;
    for(int i=0;i<4;++i)if(!(input>>channel) || std::fabs(channel-expected.mV[i])>0.000001f)return false;
    std::string extra;return !(input>>extra);
}
}
int main(int count,char** args)
{
    try
    {
        check(count==2,"Pass a new isolated fixture directory with redirected APPDATA/LOCALAPPDATA");
        const auto root=std::filesystem::absolute(utf8Path(args[1])).lexically_normal();
        const auto roaming=LLStringUtil::getoptenv("APPDATA"),local=LLStringUtil::getoptenv("LOCALAPPDATA");
        check(roaming && local && std::filesystem::absolute(utf8Path(*roaming)).parent_path()==root && std::filesystem::absolute(utf8Path(*local)).parent_path()==root,"Fixture environment is not isolated");
        const auto profile=root/"profile";std::filesystem::create_directories(profile/"user_settings");
        const auto base=root/"base";std::filesystem::create_directories(base/"skins/default");
        {std::ofstream output(base/"skins/default/colors.xml");output<<"<colors><color name=\"ThemeAccent\" value=\"0.25 0.5 0.75 1\"/><color name=\"InheritedOnly\" value=\"0.8 0.2 0.4 1\"/></colors>\n";check(static_cast<bool>(output),"Could not create fixture colors");}
        FixtureDir directory(profile.string(),base.string());UseDir use(directory);
        const auto file=utf8Path(gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS,"colors.xml"));
        check(file.lexically_normal()==profile/"user_settings/colors.xml","Color serializer escaped its fixture profile");
        auto& table=LLUIColorTable::instance();table.clear();
        const LLColor4 accent(0.25f,0.5f,0.75f,1.f),personal(0.8f,0.2f,0.4f,1.f);
        check(table.loadFromSettings() && table.getColor("ThemeAccent").get()==accent && table.getLoadedColors().count("InheritedOnly")==1,"The production base-color reader did not load its fixture");
        table.setColor("ThemeAccent",accent);auto live=table.getColor("ThemeAccent");
        table.saveUserSettings(true);check(saved(file).count("ThemeAccent")==1 && equalColor(saved(file).at("ThemeAccent"),accent),"An equal personal override was discarded or changed");
        table.setColor("ThemeAccent",personal);check(live.get()==personal,"A live handle did not follow an explicit color change");
        table.resetToDefault("ThemeAccent");table.saveUserSettings(true);
        check(saved(file).count("ThemeAccent")==0 && live.get()==accent,"Reset failed to restore inheritance or invalidated a live handle");
        table.setColor("ThemeAccent",accent);table.setColor("PersonalOnly",personal);table.saveUserSettings(true);
        auto rows=saved(file);check(rows.count("ThemeAccent")==1 && rows.count("PersonalOnly")==1 && rows.count("InheritedOnly")==0,"Explicit Set did not clear reset intent or promoted inherited theme colors");
        table.saveUserSettings(false);rows=saved(file);
        check(rows.count("ThemeAccent")==0 && rows.count("PersonalOnly")==1,"Built-in save behavior changed");
        std::filesystem::create_directory(root/"custom");gDirUtilp->setCustomThemeDir((root/"custom").string());table.saveUserSettings();
        check(saved(file).count("ThemeAccent")==1,"Ordinary startup saves ignored an active custom layer");
        gDirUtilp->setCustomThemeDir("");table.saveUserSettings();check(saved(file).count("ThemeAccent")==0,"Built-in no-argument saves lost legacy behavior");
        std::cout<<"PASS production color serialization: equal personal overrides, explicit Reset/Set, live handles, inherited-color separation and active/built-in save policy\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
