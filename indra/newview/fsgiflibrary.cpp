/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsgiflibrary.h"
#include "llagent.h"
#include "lldir.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llstartup.h"
#include <sstream>
#include <functional>
#include <set>
#if LL_WINDOWS
#include "llwin32headers.h"
#include <io.h>
#else
#include <sys/file.h>
#endif
namespace
{
using namespace fs_gif_board;
std::string filename()
{ return !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED && gAgent.getID().notNull() ? gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, "gif_library.xml") : ""; }
class Lock
{
public:
    explicit Lock(const std::string& path)
    {
        file = LLFile::fopen(path + ".lock", "a+b"); if (!file) return;
#if LL_WINDOWS
        handle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(file)));
        locked = LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &overlap) != FALSE;
#else
        locked = flock(fileno(file), LOCK_EX | LOCK_NB) == 0;
#endif
    }
    ~Lock()
    {
        if (!file) return;
#if LL_WINDOWS
        if (locked) UnlockFileEx(handle, 0, 1, 0, &overlap);
#else
        if (locked) flock(fileno(file), LOCK_UN);
#endif
        fclose(file);
    }
    bool locked = false;
private:
    LLFILE* file = nullptr;
#if LL_WINDOWS
    HANDLE handle = INVALID_HANDLE_VALUE; OVERLAPPED overlap{};
#endif
};
bool read(const std::string& path, std::vector<Item>& items, bool optional)
{
    llstat info{}; if (LLFile::stat(path, &info)) { if (optional && errno == ENOENT) { items.clear(); return true; } return false; }
    if (info.st_size < 1 || static_cast<std::uint64_t>(info.st_size) > MaxLibraryBytes) return false;
    llifstream input(path, std::ios::binary); if (!input) return false;
    std::string document(static_cast<std::size_t>(info.st_size), '\0'); input.read(document.data(), static_cast<std::streamsize>(document.size()));
    if (input.gcount() != static_cast<std::streamsize>(document.size()) || input.peek() != std::char_traits<char>::eof() || document.find("<!") != std::string::npos) return false;
    if (!boundedXML(document)) return false;
    LLSD data; std::istringstream stream(document);
    if (LLSDSerialize::fromXML(data, stream, false) <= 0 || !data.isMap() || data["version"].asInteger() != 1 || !data["items"].isArray() || static_cast<std::size_t>(data["items"].size()) > MaxBookmarks) return false;
    std::vector<Item> result;
    std::set<std::string> ids, sources;
    for (const auto& entry : llsd::inArray(data["items"]))
    {
        Item item{entry["id"].asString(), entry["url"].asString(), entry["title"].asString(), entry["tags"].asString(), entry["collection"].asString(), entry["provider"].asString(), entry["slug"].asString(), entry["favourite"].asBoolean()};
        if (!valid(item) || !ids.insert(item.id).second || !sources.insert(item.provider+"\n"+(item.provider=="klipy"?item.slug:item.url)).second) return false;
        result.push_back(std::move(item));
    }
    items = std::move(result); return true;
}
bool write(const std::string& path, const std::vector<Item>& items)
{
    LLSD data; data["version"] = 1; data["items"] = LLSD::emptyArray();
    for (const auto& item : items)
    {
        LLSD entry; entry["id"] = item.id; entry["url"] = item.url; entry["title"] = item.title; entry["tags"] = item.tags; entry["collection"] = item.collection;
        entry["provider"] = item.provider; entry["slug"] = item.slug; entry["favourite"] = item.favourite; data["items"].append(entry);
    }
    std::ostringstream output; LLSDSerialize::toPrettyXML(data, output); const auto document = output.str(); if (document.size() > MaxLibraryBytes) return false;
    const auto temporary = path + "." + LLUUID::generateNewID().asString() + ".tmp";
    llofstream file(temporary, std::ios::binary); file.write(document.data(), static_cast<std::streamsize>(document.size())); file.flush(); const bool good = file.good(); file.close();
    if (!good || LLFile::rename(temporary, path)) { LLFile::remove(temporary); return false; }
    return true;
}
bool edit(const std::function<bool(std::vector<Item>&)>& action, std::string& error)
{
    const auto path = filename(); if (path.empty()) { error = "Log in to use My GIFs."; return false; }
    Lock lock(path); if (!lock.locked) { error = "My GIFs is busy. Try again."; return false; }
    std::vector<Item> items;
    if (!read(path, items, true)) { error = "My GIFs could not be read; the original was kept."; return false; }
    if (!action(items)) { error = "Invalid GIF link or library capacity reached."; return false; }
    // Preserve a validated last-good snapshot before atomically replacing the main file.
    std::vector<Item> prior; if (read(path, prior, false) && !write(path + ".bak", prior)) { error = "Could not back up My GIFs."; return false; }
    if (!write(path, items)) { error = "Could not save My GIFs."; return false; }
    error.clear(); return true;
}
}
bool FSGifLibrary::load(std::vector<Item>& items, std::string& error)
{
    const auto path = filename(); if (path.empty()) { items.clear(); error = "Log in to use My GIFs."; return false; }
    if (read(path, items, true)) { error.clear(); return true; }
    // Recovery is read-only until the user fixes/replaces the corrupt main file.
    items.clear(); read(path + ".bak", items, false); error = "My GIFs could not be read; showing the last backup if available."; return false;
}
bool FSGifLibrary::save(Item item, std::string& error)
{
    const bool fresh=item.id.empty();if(fresh)item.id=LLUUID::generateNewID().asString();
    return edit([&](auto& items)
    {
        if(fresh)for(const auto& old:items)if(item.provider==old.provider&&(item.provider=="klipy"?item.slug==old.slug:item.url==old.url))return true;
        return upsert(items,std::move(item));
    },error);
}
bool FSGifLibrary::remove(const std::string& id, std::string& error)
{ return edit([&](auto& items) { items.erase(std::remove_if(items.begin(), items.end(), [&](const auto& item) { return item.id == id; }), items.end()); return true; }, error); }
bool FSGifLibrary::importFile(const std::string& file, std::string& error)
{
    std::vector<Item> imported; if (!read(file, imported, false)) { error = "Invalid GIF library file."; return false; }
    return edit([&](auto& items) { for (const auto& item : imported) if (!upsert(items, item)) return false; return true; }, error);
}
bool FSGifLibrary::exportFile(const std::string& file, std::string& error)
{ std::vector<Item> items; if (!load(items, error)) return false; if (!write(file, items)) { error = "Could not export My GIFs."; return false; } return true; }
