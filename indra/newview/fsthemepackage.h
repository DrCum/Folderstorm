/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_THEME_PACKAGE_H
#define FS_THEME_PACKAGE_H
#include <boost/json.hpp>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs_theme
{
inline constexpr std::uint64_t ManifestLimit=4*1024*1024, ExpandedLimit=512ULL*1024*1024;
struct Error final : std::runtime_error
{
    std::string code;
    Error(std::string id,const std::string& detail):std::runtime_error(id+": "+detail),code(std::move(id)){}
};
struct File {std::string path,sha256,kind;std::uint64_t size=0;};
struct Manifest
{
    std::string id,version,name,skin,scheme,fingerprint;
    std::vector<File> files;
    boost::json::object document;
};
struct Revision
{
    Manifest manifest;
    std::string revision;
    std::filesystem::path directory;
    std::filesystem::path viewer()const{return directory/"viewer";}
};
void require(bool condition,const std::string& code,const std::string& detail);
void pathName(const std::string& path);
bool validHash(const std::string& value);
std::string hash(const std::string& bytes);
std::string readFile(const std::filesystem::path& path,std::uint64_t limit=ManifestLimit);
std::string hashFile(const std::filesystem::path& path);
boost::json::value parse(const std::string& bytes);
Manifest manifest(const std::string& bytes);
Revision load(const std::filesystem::path& directory,const std::filesystem::path& base);
}
#endif
