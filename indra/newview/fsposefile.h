/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_POSE_FILE_H
#define FS_POSE_FILE_H
#include "fsposepolicy.h"
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#else
# include <dirent.h>
# include <fcntl.h>
# include <sys/stat.h>
# include <unistd.h>
#endif
namespace fs_pose {
inline std::string pathUTF8(const std::filesystem::path& path)
{const auto bytes=path.u8string();return std::string(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
inline std::filesystem::path pathFromUTF8(const std::string& bytes)
{
#ifdef _WIN32
 if(bytes.empty() || bytes.size()>static_cast<std::size_t>((std::numeric_limits<int>::max)()))return {};
 const auto length=static_cast<int>(bytes.size());
 const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),length,nullptr,0);
 if(count<=0)return {};
 std::wstring wide(static_cast<std::size_t>(count),L'\0');
 if(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),length,wide.data(),count)!=count)return {};
 return std::filesystem::path(wide);
#else
 return std::filesystem::path(bytes);
#endif
}
inline bool presetFileName(const std::string& name)
{return name.size()>4 && name.size()<=255 && name.substr(name.size()-4)==".xml" && name.find_first_of("/\\:\0",0,4)==std::string::npos;}
// All I/O is relative to pinned non-symlink directories on POSIX. Windows has
// no public openat equivalent: keep non-reparse handles to every directory,
// denying write/delete sharing until the actual file handle is open and used.
// Pathname checks followed by a normal fopen would allow parent substitution.
class NativePoseStore
{
#ifdef _WIN32
 using Handle=HANDLE;
 static inline const Handle invalid=INVALID_HANDLE_VALUE;
 std::vector<Handle> mDirectories;std::filesystem::path mPath;
 static void close(Handle handle){if(handle!=invalid)CloseHandle(handle);}
 static std::wstring nativePath(const std::filesystem::path& path)
 {auto value=path.native();std::replace(value.begin(),value.end(),L'/',L'\\');return L"\\\\?\\"+value;}
 static bool directory(Handle handle)
 {BY_HANDLE_FILE_INFORMATION info{};return handle!=invalid && GetFileInformationByHandle(handle,&info) &&
  (info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT);}
 static bool regular(Handle handle)
 {BY_HANDLE_FILE_INFORMATION info{};return GetFileType(handle)==FILE_TYPE_DISK && GetFileInformationByHandle(handle,&info) &&
  !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) && info.nNumberOfLinks==1 &&
  !info.nFileSizeHigh && info.nFileSizeLow<=presetLimit;}
 bool pin(const std::filesystem::path& path,bool* missing=nullptr)
 {
  const auto handle=CreateFileW(nativePath(path).c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
   FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
  if(missing)*missing=handle==invalid && (GetLastError()==ERROR_FILE_NOT_FOUND || GetLastError()==ERROR_PATH_NOT_FOUND);
  if(!directory(handle)){close(handle);return false;}mDirectories.push_back(handle);return true;
 }
#else
 using Handle=int;static constexpr Handle invalid=-1;
 Handle mDirectory=invalid,mParent=invalid;
 static void close(Handle handle){if(handle!=invalid)::close(handle);}
 static bool regular(Handle handle)
 {struct stat info{};return ::fstat(handle,&info)==0 && S_ISREG(info.st_mode) && info.st_nlink==1 && info.st_size>=0 && static_cast<unsigned long long>(info.st_size)<=presetLimit;}
#endif
 bool mOpen=false,mMissing=false,mAttempted=false;
public:
 struct Write {bool created=false,saved=false;};
 NativePoseStore()=default;NativePoseStore(const NativePoseStore&)=delete;NativePoseStore& operator=(const NativePoseStore&)=delete;
 ~NativePoseStore()
 {
#ifdef _WIN32
  for(auto handle:mDirectories)close(handle);
#else
  close(mDirectory);close(mParent);
#endif
 }
 bool open(const std::filesystem::path& settings,bool create,std::string& error,bool allowMissing=false)
 {
  if(mAttempted){error="Native pose store has already been opened";return false;}mAttempted=true;
  if(!settings.is_absolute()){error="Native pose store requires an absolute settings directory";return false;}
#ifdef _WIN32
  // Only ordinary drive paths are supported. UNC/device/reparse paths fail
  // closed instead of being resolved through another filesystem namespace.
  const auto root=settings.root_name().native();
  if(root.size()!=2 || root[1]!=L':' || !((root[0]>=L'A' && root[0]<=L'Z') || (root[0]>=L'a' && root[0]<=L'z')))
  {error="Native pose store requires an ordinary local drive path";return false;}
  auto current=settings.root_path();if(!pin(current)){error="Native pose store root could not be pinned";return false;}
  for(const auto& component:settings.relative_path())
  {
   if(component=="." || component==".." || component.empty()){error="Native pose directory contains unsupported components";return false;}
   current/=component;if(!pin(current)){error="Native pose directory is busy, unavailable or a reparse point";return false;}
  }
  mPath=current/L"poses";
  if(create && !CreateDirectoryW(nativePath(mPath).c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS)
  {error="Native pose directory could not be created";return false;}
  bool missing=false;
  if(!pin(mPath,&missing)){if(allowMissing && !create && missing){mMissing=true;return true;}error="Native pose directory is busy, unavailable or a reparse point";return false;}
#else
  auto current=::open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(current==invalid){error="Native pose store root could not be pinned";return false;}
  for(const auto& component:settings.relative_path())
  {
   if(component=="." || component==".." || component.empty()){close(current);error="Native pose directory contains unsupported components";return false;}
   const auto next=::openat(current,component.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(current);current=next;
   if(current==invalid){error="Native settings directory is unavailable or contains a symbolic link";return false;}
  }
  mParent=current;
  if(create && ::mkdirat(mParent,"poses",0700)!=0 && errno!=EEXIST){error="Native pose directory could not be created";return false;}
  mDirectory=::openat(mParent,"poses",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
  if(mDirectory==invalid){if(allowMissing && !create && errno==ENOENT){mMissing=true;return true;}error="Native pose directory is unavailable or a symbolic link";return false;}
#endif
  mOpen=true;return true;
 }
 bool exists(const std::string& name) const
 {
  if(!presetFileName(name))return true;
  if(mMissing)return false;
  if(!mOpen)return true;
#ifdef _WIN32
  return GetFileAttributesW(nativePath(mPath/pathFromUTF8(name)).c_str())!=INVALID_FILE_ATTRIBUTES;
#else
  struct stat info{};return ::fstatat(mDirectory,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)==0 || errno!=ENOENT;
#endif
 }
 std::vector<std::string> names() const
 {
  std::vector<std::string> result;if(!mOpen)return result;
#ifdef _WIN32
  WIN32_FIND_DATAW info{};const auto search=FindFirstFileW(nativePath(mPath/L"*.xml").c_str(),&info);
  if(search==INVALID_HANDLE_VALUE)return result;
  unsigned scanned=0;do
  {
   ++scanned;const auto name=pathUTF8(std::filesystem::path(info.cFileName));
   if(presetFileName(name) && !(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))result.push_back(name);
  }while(result.size()<256 && scanned<4096 && FindNextFileW(search,&info));FindClose(search);
#else
  // A fresh open file description avoids changing the pinned descriptor's
  // enumeration offset when listing more than once.
  const auto copy=::openat(mDirectory,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(copy==invalid)return result;
  auto* directory=::fdopendir(copy);if(!directory){close(copy);return result;}
  unsigned scanned=0;while(result.size()<256 && ++scanned<=4096)
  {
   const auto* entry=::readdir(directory);if(!entry)break;const std::string name=entry->d_name;
   struct stat info{};
   if(presetFileName(name) && ::fstatat(mDirectory,name.c_str(),&info,AT_SYMLINK_NOFOLLOW)==0 && S_ISREG(info.st_mode) && info.st_nlink==1)result.push_back(name);
  }
  ::closedir(directory);
#endif
  std::sort(result.begin(),result.end());return result;
 }
 bool read(const std::string& name,std::string& bytes,std::string& error) const
 {
  if(!mOpen || !presetFileName(name)){error="Preset is outside the native pose store";return false;}
#ifdef _WIN32
  const auto file=CreateFileW(nativePath(mPath/pathFromUTF8(name)).c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
#else
  const auto file=::openat(mDirectory,name.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
#endif
  if(file==invalid){error="Preset could not be opened without following links";return false;}
  if(!regular(file)){close(file);error="Preset is not a bounded single-link regular file";return false;}
  bytes.clear();char buffer[4096];bool okay=true;
  for(;;)
  {
#ifdef _WIN32
   DWORD count=0;if(!ReadFile(file,buffer,sizeof(buffer),&count,nullptr)){okay=false;break;}
#else
   const auto count=::read(file,buffer,sizeof(buffer));if(count<0){if(errno==EINTR)continue;okay=false;break;}
#endif
   if(!count)break;
   if(bytes.size()+static_cast<std::size_t>(count)>presetLimit){okay=false;break;}bytes.append(buffer,static_cast<std::size_t>(count));
  }
  close(file);if(!okay){bytes.clear();error="Preset read failed or exceeded its bound";}return okay;
 }
 Write writeNew(const std::string& name,const std::string& bytes,std::string& error) const
 {
  Write result;if(!mOpen || !presetFileName(name) || bytes.empty() || bytes.size()>presetLimit){error="Preset save is outside the bounded native store";return result;}
#ifdef _WIN32
  const auto file=CreateFileW(nativePath(mPath/pathFromUTF8(name)).c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
#else
  const auto file=::openat(mDirectory,name.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
#endif
  if(file==invalid){error="Preset collision or confined native file creation failed";return result;}result.created=true;
  bool okay=regular(file);std::size_t offset=0;
  while(okay && offset<bytes.size())
  {
#ifdef _WIN32
   DWORD count=0;okay=WriteFile(file,bytes.data()+offset,static_cast<DWORD>(bytes.size()-offset),&count,nullptr)!=FALSE;
#else
   const auto count=::write(file,bytes.data()+offset,bytes.size()-offset);if(count<0 && errno==EINTR)continue;okay=count>0;
#endif
   if(!okay || !count){okay=false;break;}offset+=static_cast<std::size_t>(count);
  }
#ifdef _WIN32
  if(okay)okay=FlushFileBuffers(file)!=FALSE;
  const bool closed=CloseHandle(file)!=FALSE;
#else
  if(okay)okay=::fsync(file)==0;
  const bool closed=::close(file)==0;
  if(okay && closed)okay=::fsync(mDirectory)==0;
#endif
  result.saved=okay && closed;if(!result.saved)error="Native preset was created but saving is uncertain";return result;
 }
};
}
#endif
