/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsposefile.h"
#include <cassert>
#include <fstream>
#include <iostream>
#ifndef _WIN32
#include <thread>
#include <atomic>
#endif
namespace fs=std::filesystem;
void file(const fs::path& path,const std::string& bytes){std::ofstream out(path,std::ios::binary);out<<bytes;}
int main()
{
 using fs_pose::NativePoseStore;
 const std::string unicodeName="caf\xc3\xa9.xml";
 assert(fs_pose::pathUTF8(fs_pose::pathFromUTF8(unicodeName))==unicodeName);
#ifndef _WIN32
 char pattern[]="/tmp/pose-store-test-XXXXXX";const auto root=fs::path(::mkdtemp(pattern));assert(!root.empty());
 const auto settings=root/"settings",outside=root/"outside";fs::create_directory(settings);fs::create_directory(outside);
 std::string error,bytes;NativePoseStore absent;assert(absent.open(settings,false,error,true));assert(!absent.exists("new.xml"));
 NativePoseStore store;assert(store.open(settings,true,error));assert(store.writeNew("my_pose.xml","original",error).saved);
 assert(store.read("my_pose.xml",bytes,error) && bytes=="original");assert(!store.writeNew("my_pose.xml","overwrite",error).created);
 assert(!store.writeNew("../outside.xml","escape",error).created);assert(!store.read("../outside.xml",bytes,error));
 file(outside/"my_pose.xml","outside secret");
 // Substitute the complete poses directory after checking/opening the store.
 // Both reads and writes remain attached to the original directory handle.
 fs::rename(settings/"poses",settings/"approved-inode");fs::create_directory_symlink(outside,settings/"poses");
 assert(store.read("my_pose.xml",bytes,error) && bytes=="original");assert(store.writeNew("after_swap.xml","pinned",error).saved);
 assert(fs::exists(settings/"approved-inode/after_swap.xml") && !fs::exists(outside/"after_swap.xml"));
 NativePoseStore redirected;assert(!redirected.open(settings,false,error));
 fs::remove(settings/"poses");fs::rename(settings/"approved-inode",settings/"poses");
 fs::create_symlink(outside/"my_pose.xml",settings/"poses/link.xml");assert(!store.read("link.xml",bytes,error));assert(!store.writeNew("link.xml","escape",error).created);
 fs::create_hard_link(outside/"my_pose.xml",settings/"poses/hard.xml");assert(!store.read("hard.xml",bytes,error));
 file(settings/"poses/large.xml",std::string(fs_pose::presetLimit+1,'x'));assert(!store.read("large.xml",bytes,error));
 ::mkfifo((settings/"poses/pipe.xml").c_str(),0600);assert(!store.read("pipe.xml",bytes,error));
 // The final file can also be replaced after enumeration. Actual open refuses
 // the symlink rather than trusting the enumerated entry's earlier type.
 const auto names=store.names();assert(std::find(names.begin(),names.end(),"my_pose.xml")!=names.end());
 fs::remove(settings/"poses/my_pose.xml");fs::create_symlink(outside/"my_pose.xml",settings/"poses/my_pose.xml");assert(!store.read("my_pose.xml",bytes,error));
 // Ancestor symlinks are rejected as well, not just the final poses component.
 fs::create_directory_symlink(settings,root/"redirected-settings");NativePoseStore ancestor;assert(!ancestor.open(root/"redirected-settings",false,error));
 // Exercise the actual open race repeatedly while another writer substitutes
 // the poses pathname. Successful opens can only write into the pinned store.
 fs::remove(settings/"poses/my_pose.xml");std::atomic<bool> done{false};
 std::thread swapper([&](){for(unsigned i=0;i<150;++i){fs::rename(settings/"poses",settings/"moving");fs::create_directory_symlink(outside,settings/"poses");fs::remove(settings/"poses");fs::rename(settings/"moving",settings/"poses");}done=true;});
 unsigned i=0;while(!done || i<100)
 {
  NativePoseStore raced;std::string reason;if(raced.open(settings,false,reason))raced.writeNew("race_"+std::to_string(i)+".xml","inside",reason);
  ++i;
 }
 swapper.join();for(const auto& entry:fs::directory_iterator(outside))assert(entry.path().filename()=="my_pose.xml");
 fs::remove_all(root);
#endif
 std::cout<<"handle-confined pose file checks passed\n";
}
