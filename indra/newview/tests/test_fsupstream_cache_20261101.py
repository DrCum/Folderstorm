"""Production cache constructor/purge with real temporary files and API fixtures.

Boost path/error/time APIs are adapted to std::filesystem for standalone tests.
Removal failure and after-scan disappearance are injected. Windows sharing and
native code-page conversion still need acceptance on Windows.
"""
from pathlib import Path
import os,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[2]
STUBS=r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <iostream>
using F32=float;using S32=int;using S64=long long;
#define LL_WINDOWS 0
#define LL_PROFILE_ZONE_SCOPED
std::vector<std::string> logs;std::function<void(const std::string&)> observe;
struct Log:std::ostringstream {~Log(){logs.push_back(str());if(observe)observe(str());}};
#define LL_INFOS(...) Log()
#define LL_DEBUGS(...) Log()
#define LL_WARNS(...) Log()
#define LL_ENDL ""
namespace fs=std::filesystem;
std::set<fs::path> failRemoval;
namespace boost {namespace system {struct error_code:std::error_code {bool failed()const{return value()!=0;}};}
namespace filesystem {
 using fs::path;using fs::recursive_directory_iterator;using fs::directory_iterator;using fs::is_directory;using fs::is_regular_file;using fs::file_size;
 std::time_t last_write_time(const fs::path& p,system::error_code& ec){return static_cast<std::time_t>(std::chrono::duration_cast<std::chrono::seconds>(fs::last_write_time(p,ec).time_since_epoch()).count());}
 void last_write_time(const fs::path& p,std::time_t when,system::error_code& ec){fs::last_write_time(p,fs::file_time_type(std::chrono::seconds(when)),ec);}
 bool remove(const fs::path& p,system::error_code& ec){if(failRemoval.count(p)){ec.assign(static_cast<int>(std::errc::permission_denied),std::generic_category());return false;}return fs::remove(p,ec);}
}}
struct LLApp{static bool isRunning(){return true;}};
struct Dir {std::string getDirDelimiter()const{return "/";}std::string getBaseFileName(const std::string& path,bool strip)const{return strip?fs::path(path).stem().string():fs::path(path).filename().string();}}dirs;Dir* gDirUtilp=&dirs;
struct LLFile{static void mkdir(const std::string& path){fs::create_directories(path);}};
template<class T>T llclamp(T value,T low,T high){return std::clamp(value,low,high);}
struct LLDiskCache {
 static std::string sCacheDir;uintmax_t mMaxSizeBytes;F32 mHighPercent=95.f,mLowPercent=70.f;bool mEnableCacheDebugInfo;uintmax_t stored=0;std::vector<std::string> mSkipList;
 LLDiskCache(const std::string&,uintmax_t,bool,F32,F32);void purge();void prepopulateCacheWithStatic(){}
 uintmax_t updateCacheSize(uintmax_t n){return stored=n;}uintmax_t dirFileSize(const std::string& path){uintmax_t n=0;for(const auto& e:fs::recursive_directory_iterator(path))if(e.is_regular_file())n+=e.file_size();return n;}
};
std::string LLDiskCache::sCacheDir;
static const char* subdirs="0123456789abcdef";
static const std::string CACHE_FILENAME_PREFIX("sl_cache");
'''
CASES=r'''
std::string protectedID="00000000-0000-0000-0000-000000000001";
fs::path file(const fs::path& root,int id){return root/"0"/("sl_cache_00000000-0000-0000-0000-"+std::string(11,'0')+std::to_string(id)+"_0.asset");}
void write(const fs::path& p,int bytes,int age){std::ofstream out(p);out<<std::string(bytes,'x');out.close();fs::last_write_time(p,fs::file_time_type::clock::now()-std::chrono::hours(age));}
bool logged(const std::string& text){return std::any_of(logs.begin(),logs.end(),[&](const auto& s){return s.find(text)!=std::string::npos;});}
int main(int argc,char** argv){assert(argc==2);const auto root=fs::path(argv[1])/u8"Région-日本語";
 for(bool debug:{false,true}){
  fs::remove_all(root);LLDiskCache disk(root.string(),1024,debug,95.f,70.f);disk.mSkipList={protectedID};
  write(file(root,1),600,4);write(file(root,2),600,3);write(file(root,3),600,2);logs.clear();disk.purge();
  assert(fs::exists(file(root,1))&&!fs::exists(file(root,2))&&!fs::exists(file(root,3))&&disk.stored==600);
  assert(logged("Deleted: 2 Skipped: 1 Kept: 0 Failed: 0 Missing: 0")&&logged("Total of 1200 bytes removed."));
  disk.mMaxSizeBytes=512;logs.clear();disk.purge();assert(disk.stored==600&&fs::exists(file(root,1)));assert(logged("Deleted: 0 Skipped: 1 Kept: 0 Failed: 0 Missing: 0"));
  disk.mMaxSizeBytes=1024;write(file(root,2),600,3);write(file(root,3),600,2);failRemoval.insert(file(root,2));logs.clear();disk.purge();
  assert(disk.stored==1200&&fs::exists(file(root,2))&&!fs::exists(file(root,3)));assert(logged("Deleted: 1 Skipped: 1 Kept: 0 Failed: 1 Missing: 0"));failRemoval.clear();disk.purge();assert(disk.stored==600);
  write(file(root,1),600,4);write(file(root,2),600,3);write(file(root,3),600,2);bool removed=false;logs.clear();observe=[&](const auto& text){if(!removed&&text.find("Purging cache to a maximum")!=std::string::npos){removed=fs::remove(file(root,2));}};
  disk.purge();observe={};assert(removed&&disk.stored==600&&logged("Deleted: 1 Skipped: 1 Kept: 0 Failed: 0 Missing: 1"));assert(logged("Total of 600 bytes removed."));
 }
 fs::remove_all(root);LLDiskCache disk(root.string(),1000,false,60.f,40.f);write(file(root,2),200,2);write(file(root,3),399,1);logs.clear();disk.purge();assert(fs::exists(file(root,2))&&!logged("Deleted:"));
 write(file(root,3),400,1);logs.clear();disk.purge();assert(!fs::exists(file(root,2))&&fs::exists(file(root,3))&&disk.stored==400);assert(logged("Deleted: 1 Skipped: 0 Kept: 1 Failed: 0 Missing: 0"));
 LLDiskCache clipped(root.string(),1000,false,120.f,-10.f);assert(clipped.mHighPercent==100.f&&clipped.mLowPercent==0.f);fs::remove_all(root);
}
'''
class CacheTests(unittest.TestCase):
 def test_real_files_protected_failures_missing_debug_and_startup_watermarks(self):
  text=(ROOT/'llfilesystem/lldiskcache.cpp').read_text()
  source=STUBS+function(text,'boost::filesystem::path cacheNativePath(const std::string& value)')+function(text,'std::string cacheUTF8Path(const boost::filesystem::path& value)')
  start=text.index('LLDiskCache::LLDiskCache(');end=text.index('// WARNING: purge()',start);source+=text[start:end]
  source+=function(text,'void LLDiskCache::purge()')+CASES
  with tempfile.TemporaryDirectory(prefix='fs-upstream-cache-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(source);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe),directory],check=True)
 def test_native_windows_paths_keep_wide_api_and_utf8_boundaries(self):
  cache=(ROOT/'llfilesystem/lldiskcache.cpp').read_text();dirs=(ROOT/'llfilesystem/lldir.cpp').read_text()
  self.assertIn('ll_convert<std::wstring>(value)',cache);self.assertIn('ll_convert<std::string>(value.wstring())',cache)
  self.assertIn('cacheUTF8Path((*iter).path().filename())',cache)
  self.assertIn('ll_convert<std::string>(dir_itr->path().filename().wstring())',dirs)

if __name__=='__main__':unittest.main()
