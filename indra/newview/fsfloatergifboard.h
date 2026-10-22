/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_FLOATER_GIF_BOARD_H
#define FS_FLOATER_GIF_BOARD_H
#include "llfloater.h"
#include "fsgifboardmodel.h"
#include <chrono>
#include <map>
class LLScrollContainer;
class LLPanel;
class FSFloaterGifBoard final : public LLFloater
{
public:
    explicit FSFloaterGifBoard(const LLSD& key) : LLFloater(key) {}
    ~FSFloaterGifBoard() override;
    static void open(const LLSD& target = LLSD());
    static void saveLink(const std::string& url);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    void reload(bool force = false);
    void editItem(bool create);
    void choose();
    void insert();
    bool current() const;
private:
    void layoutTiles();
    void clearTiles();
    void options(S32 x = -1, S32 y = -1);
    void popOut();
    void preview();
    void transfer(bool save);
    void sourceChanged();
    void providerSearch(bool append = false);
    void updateProvider();
    LLSD mTarget;
    LLUUID mAccount, mLogin;
    std::string mGrid, mSelected, mStatus;
    bool mExpanded = false, mValid = false;
    bool mProvider = false;
    bool mOpened = false;
    bool mProviderConfigured = false;
    unsigned mProviderRevision = ~0u, mProviderPage = 1;
    std::string mMyQuery,mProviderQuery;
    std::map<std::string,std::string> mShareURLs;
    std::vector<std::string> mProviderCategories;
    std::vector<fs_gif_board::Item> mLibrary, mItems;
    LLScrollContainer* mScroll = nullptr;
    LLPanel* mDocument = nullptr;
    std::map<std::size_t, LLPanel*> mTiles;
    LLHandle<class LLContextMenu> mMenu;
    std::chrono::steady_clock::time_point mLoaded{};
};
class FSFloaterGifItem final : public LLFloater
{
public:
    explicit FSFloaterGifItem(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
private:
    void save();
    LLUUID mAccount, mLogin;
    std::string mGrid, mID;
};
class FSFloaterGifReport final : public LLFloater
{
public:
    explicit FSFloaterGifReport(const LLSD& key):LLFloater(key){}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
private:
    LLUUID mAccount,mLogin;std::string mGrid,mSlug;
};
#endif
