"""Regressions for native JPEG recovery and managed-file texture routing.

Production declarations, handlers and texture factories are source-extracted.
The default JPEG fixture mocks libjpeg's destruction boundary. Set
FS_BACKGROUND_JPEG_INCLUDE and FS_BACKGROUND_JPEG_LIBRARY to also drive the
handler from real libjpeg malformed-header errors. No viewer/GL link is needed.
"""
import os
import re
import unittest

import test_fsworkspacebackground as portable
from test_fsgestureboardsounds import ROOT, VIEWER, function


class NativeBackgroundBoundaries(unittest.TestCase):
    cpp=portable.WorkspaceBackgrounds.cpp

    def test_jpeg_error_context_is_thread_local(self):
        header=(ROOT/'indra/llimage/llimagejpeg.h').read_text()
        codec=(ROOT/'indra/llimage/llimagejpeg.cpp').read_text()
        declaration=re.search(r'#if !LL_ARM64\s+[^#]*sSetjmpBuffer[^#]*#endif',header).group()
        definition=re.search(r'#if !LL_ARM64\s+[^#]*LLImageJPEG::sSetjmpBuffer[^#]*#endif',codec).group()
        handler=function(codec,'void LLImageJPEG::errorExit(')
        include=os.environ.get('FS_BACKGROUND_JPEG_INCLUDE','')
        library=os.environ.get('FS_BACKGROUND_JPEG_LIBRARY','')
        flags=['-pthread']
        if include and library:
            flags+=['-DFS_REAL_JPEG=1','-I'+include,library]
        prelude=r'''
#include <cassert>
#include <csetjmp>
#include <cstddef>
#include <cstdio>
#include <atomic>
#include <array>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
#if FS_REAL_JPEG
extern "C" {
#include <jpeglib.h>
}
#else
struct jpeg_common_struct;
using j_common_ptr=jpeg_common_struct*;
struct jpeg_error_mgr {void (*output_message)(j_common_ptr)=nullptr;};
struct jpeg_common_struct {jpeg_error_mgr* err=nullptr;bool destroyed=false;};
using jpeg_decompress_struct=jpeg_common_struct;
void jpeg_destroy(j_common_ptr info){assert(!info->destroyed);info->destroyed=true;}
#endif
struct Gate {
    std::mutex mutex;std::condition_variable ready;int arrivals=0;
    void wait(){std::unique_lock<std::mutex> lock(mutex);if(++arrivals==2)ready.notify_all();else ready.wait(lock,[this]{return arrivals==2;});}
};
class LLImageJPEG {
public:
    static void errorExit(j_common_ptr cinfo);
#if !LL_ARM64
    static void worker(int slot);
#endif
private:
'''
        body=r'''
};
'''+definition+'\n'+handler+r'''
#if !LL_ARM64
constexpr int attempts=32;
std::array<Gate,attempts> gates;
std::array<std::atomic<bool>,attempts> firstSaved{};
std::array<std::atomic<const void*>,2> buffers{};
std::array<std::atomic<int>,2> recovered{};
std::atomic<bool> shared=false;
void LLImageJPEG::worker(int slot){
    for(int attempt=0;attempt<attempts;++attempt){
        jpeg_decompress_struct info{};jpeg_error_mgr error{};
#if FS_REAL_JPEG
        info.err=jpeg_std_error(&error);
        error.error_exit=&LLImageJPEG::errorExit;
        error.output_message=[](j_common_ptr){};
        jpeg_create_decompress(&info);
#else
        info.err=&error;error.output_message=[](j_common_ptr){};
#endif
        // Serialize the saves so even the old-buffer negative control has no write race.
        if(slot==1)while(!firstSaved[attempt].load())std::this_thread::yield();
        if(setjmp(sSetjmpBuffer)){
            ++recovered[slot];
            continue;
        }
        buffers[slot]=static_cast<const void*>(&sSetjmpBuffer);
        if(slot==0)firstSaved[attempt]=true;
        gates[attempt].wait();
        // Detect the old shared-buffer bug before risking a cross-thread longjmp.
        if(buffers[0]==buffers[1]){
            shared=true;
            jpeg_destroy(reinterpret_cast<j_common_ptr>(&info));
            return;
        }
#if FS_REAL_JPEG
        const unsigned char malformed[]={0,1,2,3,4};
        jpeg_mem_src(&info,malformed,sizeof(malformed));
        jpeg_read_header(&info,true); // Dispatches the production errorExit callback.
        assert(false&&"Malformed JPEG unexpectedly accepted");
#else
        errorExit(reinterpret_cast<j_common_ptr>(&info));
#endif
    }
}
#endif
int main(){
#if !LL_ARM64
    std::thread first(LLImageJPEG::worker,0),second(LLImageJPEG::worker,1);
    first.join();second.join();
    if(shared)return 42;
    assert(recovered[0]==attempts&&recovered[1]==attempts);
    std::cout<<"Concurrent JPEG error recovery passed\n";
#else
    std::cout<<"Existing ARM64 error-handler guards preserved\n";
#endif
}
'''
        program=prelude+declaration+'\n'+body
        self.cpp(program,flags=flags+['-DLL_ARM64=0'])
        # The previous declarations fail deterministically without invoking undefined behavior.
        self.cpp(program.replace('thread_local ',''),flags=flags+['-DLL_ARM64=0'],expected=42)
        self.cpp(program,flags=flags+['-DLL_ARM64=1'])

    def test_managed_image_uses_native_file_url_factory(self):
        runtime=(VIEWER/'fsworkspacebackground.cpp').read_text()
        factory=(VIEWER/'llviewertexture.cpp').read_text()
        textures=(VIEWER/'llviewertexturelist.cpp').read_text()
        dirs=(ROOT/'indra/llfilesystem/lldir.cpp').read_text()
        key=function(runtime,'std::string key(')
        demand=function(runtime,'    void demand(').replace('    void demand(', 'void Service::demand(')
        from_file=function(factory,'LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromFile(')
        from_url=function(factory,'LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTextureFromUrl(')
        lookup=function(textures,'LLViewerFetchedTexture* LLViewerTextureList::getImageFromFile(')
        cache=function(textures,'LLViewerFetchedTexture* LLViewerTextureList::getImageFromUrl(')
        append=function(dirs,'void LLDir::append(')
        separator=function(dirs,'LLDir::SepOff LLDir::needSep(')
        program=r'''
#include "fsworkspacebackgroundmodel.h"
#include "lluuid.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE
#define LL_PROFILE_ZONE_TEXT(a,b)
#define LL_WARNS() nativeLog
#define LL_DEBUGS(category) nativeLog
#define LL_ERRS() nativeLog
#define LL_ENDL '\n'
std::ostringstream nativeLog;
std::ostream& operator<<(std::ostream& out,const LLUUID& id){return out<<id.asString();}
using namespace FSWorkspaceBackground;
using S8=signed char;using LLGLint=int;using LLGLenum=unsigned;
enum FTType{FTT_DEFAULT,FTT_LOCAL_FILE};
struct LLViewerTexture {
    enum EBoostLevel{BOOST_NONE,BOOST_UI,BOOST_ICON,BOOST_THUMBNAIL};
    enum {FETCHED_TEXTURE,LOD_TEXTURE};
};
using LLGLTexture=LLViewerTexture;
struct LLViewerFetchedTexture:LLViewerTexture {
    std::string url;FTType type;
    LLViewerFetchedTexture(std::string u,FTType t,LLUUID,bool):url(std::move(u)),type(t){}
    const std::string& getUrl(){return url;}
    void setExplicitFormat(int,unsigned){}void dontDiscard(){}void forceActive(){}
    void setBoostLevel(EBoostLevel){}void setGLTextureCreated(bool){}
};
struct LLViewerLODTexture:LLViewerFetchedTexture{using LLViewerFetchedTexture::LLViewerFetchedTexture;};
template<class T>struct LLPointer{
    T* value=nullptr;LLPointer()=default;LLPointer(T* v):value(v){}
    LLPointer& operator=(T* v){value=v;return *this;}T* operator->()const{return value;}
    T* get(){return value;}bool isNull(){return !value;}explicit operator bool()const{return value;}
    operator T*()const{return value;}
};
struct LLDir {
    using SepOff=std::pair<bool,unsigned short>;
    std::string mDirDelimiter="/",skin;int skinLookups=0;
    std::string getDirDelimiter(){return mDirDelimiter;}
    void append(std::string&,const std::string&)const;
    SepOff needSep(const std::string&,const std::string&)const;
    std::string findSkinnedFilename(const std::string& subdir,const std::string& file){
        ++skinLookups;std::string path=skin;append(path,subdir);append(path,file);
        return std::filesystem::is_regular_file(path)?path:"";
    }
}dir,*gDirUtilp=&dir;
std::string profile;
std::string directory(){return profile;}
struct LLFile{static bool isfile(const std::string& path){return std::filesystem::is_regular_file(path);}};
const LLUUID IMG_DEFAULT{"default"};
struct LLViewerTextureManager {
    static LLViewerFetchedTexture* getFetchedTexture(const LLUUID&,FTType=FTT_DEFAULT,bool=true,LLViewerTexture::EBoostLevel=LLViewerTexture::BOOST_NONE);
    static LLViewerFetchedTexture* getFetchedTextureFromFile(const std::string&,FTType=FTT_LOCAL_FILE,bool=true,LLViewerTexture::EBoostLevel=LLViewerTexture::BOOST_NONE,S8=LLViewerTexture::FETCHED_TEXTURE,int=0,unsigned=0,const LLUUID& =LLUUID::null);
    static LLViewerFetchedTexture* getFetchedTextureFromUrl(const std::string&,FTType,bool=true,LLViewerTexture::EBoostLevel=LLViewerTexture::BOOST_NONE,S8=LLViewerTexture::FETCHED_TEXTURE,int=0,unsigned=0,const LLUUID& =LLUUID::null);
};
int get_element_type(LLViewerTexture::EBoostLevel){return 0;}
struct LLViewerTextureList {
    bool mInitialized=true;std::map<std::string,std::unique_ptr<LLViewerFetchedTexture>> images;
    LLPointer<LLViewerFetchedTexture> findImage(const LLUUID& id,int){auto it=images.find(id.asString());return it==images.end()?nullptr:it->second.get();}
    void addImage(LLPointer<LLViewerFetchedTexture> image,int){images[image->url]=std::unique_ptr<LLViewerFetchedTexture>(image.get());}
    LLViewerFetchedTexture* getImageFromFile(const std::string&,FTType,bool,LLViewerTexture::EBoostLevel,S8,int,unsigned,const LLUUID&);
    LLViewerFetchedTexture* getImageFromUrl(const std::string&,FTType,bool,LLViewerTexture::EBoostLevel,S8,int,unsigned,const LLUUID&);
}gTextureList;
LLViewerFetchedTexture fallback("fallback",FTT_DEFAULT,IMG_DEFAULT,true);
int defaults=0;
LLViewerFetchedTexture* LLViewerTextureManager::getFetchedTexture(const LLUUID&,FTType,bool,LLViewerTexture::EBoostLevel){++defaults;return &fallback;}
struct Service {
    std::string grid="secondlife";
    std::map<std::string,LLPointer<LLViewerFetchedTexture>> textures;
    std::map<std::string,std::string> errors;
    void demand(const Image&,std::set<std::string>&);
};
'''
        # Hashing is a cache boundary fixture; the production URL lookup/cache branch is retained.
        cache=cache.replace('new_id.generate(url);','new_id.value=url;')
        program+='\n'.join([append,separator,from_file,from_url,lookup,cache,key,demand])+r'''
int main(){
    auto root=std::filesystem::path(__FILE__).parent_path()/"folderstorm managed image boundary";
    std::filesystem::remove_all(root);
    dir.skin=(root/"skins/default").string();
    profile=(root/"profiles/Márty/workspace_backgrounds").string();
    std::filesystem::create_directories(profile);
    Image image;image.source=Source::Local;image.asset="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa.png";image.width=image.height=1;
    const auto path=profile+"/"+image.asset;
    {std::ofstream file(path);file<<"fixture";}
    assert(LLFile::isfile(path));
    // Confirm why the old factory fails even though the managed file exists.
    assert(LLViewerTextureManager::getFetchedTextureFromFile(path)==&fallback&&dir.skinLookups==1);
    defaults=0;dir.skinLookups=0;
    Service first,second;std::set<std::string> needed;
    first.demand(image,needed);
    const auto id=key(image);const auto expected="file://"+path;
    assert(first.textures.at(id)->getUrl()==expected&&first.textures.at(id)->type==FTT_LOCAL_FILE);
    assert(first.errors.empty()&&defaults==0&&dir.skinLookups==0);
    second.demand(image,needed);first.demand(image,needed);
    assert(first.textures.at(id).get()==second.textures.at(id).get()&&gTextureList.images.size()==1);
    assert(std::filesystem::is_regular_file(first.textures.at(id)->getUrl().substr(7)));
    image.asset="bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb.png";
    first.demand(image,needed);assert(first.errors.size()==1&&first.textures.size()==1);
    image.source=Source::Texture;image.asset="cccccccc-cccc-cccc-cccc-cccccccccccc";image.grid="another-grid";
    first.demand(image,needed);assert(first.errors.size()==2&&defaults==0);
    std::filesystem::remove_all(root);
}
'''
        self.cpp(program,'fsworkspacebackgroundmodel.cpp')


if __name__=='__main__':
    unittest.main()
