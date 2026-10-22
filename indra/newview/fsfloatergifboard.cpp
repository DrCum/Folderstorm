/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsfloatergifboard.h"
#include "fsgiflibrary.h"
#include "fsklipyclient.h"
#include "fschatmedia.h"
#include "fschatupload.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llfocusmgr.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llscrollcontainer.h"
#include "llstartup.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "llviewermenu.h"
#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llviewermenufile.h"
#include "llwindow.h"
#include <set>
namespace
{
using namespace fs_gif_board;
using Clock = std::chrono::steady_clock;
std::string tr(const char* key) { return LLTrans::getString(key); }
bool login(const LLUUID& account, const LLUUID& session, const std::string& grid)
{ return !gDisconnected && LLStartUp::getStartupState() == STATE_STARTED && account == gAgent.getID() && session == gAgent.getSessionID() && grid == LLGridManager::getInstance()->getGrid(); }
void notice(const std::string& message)
{ LLSD args; args["MESSAGE"] = message; LLNotificationsUtil::add("SystemMessage", args); }
Item selected(const std::vector<Item>& items, const std::string& id)
{ for (const auto& item : items) if (item.id == id) return item; return {}; }
class GifTile final : public LLPanel
{
public:
    GifTile(const Item& item, std::function<void()> select, std::function<void()> insert, std::function<void(S32,S32)> context) : LLPanel(Params()), id(item.id)
    {
        setMouseOpaque(true);setName("gif_"+id);onSelect=select;onInsert=insert;onContext=context;card = FSChatMedia::createBoardTile(item.url, std::move(select), std::move(insert), std::move(context)); if (card) addChild(card);
        LLTextBox::Params p; p.name = "title"; p.rect = LLRect(2, 20, 140, 0); p.use_ellipses = true;
        title = LLUICtrlFactory::create<LLTextBox>(p); title->setText(item.title.empty() ? tr("FSGifUntitled") : item.title); addChild(title);
        setToolTip(item.title + (item.tags.empty() ? "" : "\n" + item.tags));
    }
    void reshape(S32 w, S32 h, bool parent = true) override
    { LLPanel::reshape(w, h, parent); title->reshape((std::max)(20,w-4),20); if (card) { card->setOrigin(1,22); card->reshape((std::max)(20,w-2),(std::max)(24,h-24)); } }
    void draw() override
    { LLPanel::draw(); if (chosen) gl_rect_2d(0,getRect().getHeight(),getRect().getWidth(),0,LLUIColorTable::instance().getColor("FocusColor").get(),false); }
    bool handleMouseDown(S32 x,S32 y,MASK mask) override
    { if(childrenHandleMouseDown(x,y,mask))return true;if(onSelect)onSelect();return true; }
    bool handleDoubleClick(S32,S32,MASK) override { if(onInsert)onInsert();return true; }
    bool handleKeyHere(KEY key,MASK mask) override
    {if(mask==MASK_NONE&&key==KEY_RETURN){if(onInsert)onInsert();return true;}return LLPanel::handleKeyHere(key,mask);}
    bool handleRightMouseDown(S32 x,S32 y,MASK) override
    {if(onSelect)onSelect();S32 sx,sy;localPointToScreen(x,y,&sx,&sy);if(onContext)onContext(sx,sy);return true;}
    std::string id; bool chosen = false; LLPanel* card = nullptr; LLTextBox* title = nullptr;
    std::function<void()> onSelect,onInsert;
    std::function<void(S32,S32)> onContext;
    Clock::time_point visibleSince{};
};
LLSD itemKey(const Item& item)
{ LLSD key; key["id"]=item.id;key["url"]=item.url;key["title"]=item.title;key["tags"]=item.tags;key["collection"]=item.collection;key["favourite"]=item.favourite;return key; }
}
void FSFloaterGifBoard::open(const LLSD& target)
{ LLSD key; key["target"] = target; LLFloaterReg::showInstance("gif_board", key); }
FSFloaterGifBoard::~FSFloaterGifBoard() { if(auto* menu=mMenu.get())menu->die(); }
void FSFloaterGifBoard::saveLink(const std::string& url)
{
    Item item; item.url = url; std::string error;
    if (!FSGifLibrary::save(std::move(item), error)) notice(error);
    else if (auto* board = LLFloaterReg::findTypedInstance<FSFloaterGifBoard>("gif_board")) board->reload(true);
}
bool FSFloaterGifBoard::postBuild()
{
    mScroll = getChild<LLScrollContainer>("scroll"); LLPanel::Params p; p.name = "gif_document";
    p.rect = LLRect(0,100,300,0); mDocument = LLUICtrlFactory::create<LLPanel>(p); mScroll->addChild(mDocument);
    getChild<LLFilterEditor>("search")->setCommitCallback([this](LLUICtrl*,const LLSD&) { if(mProvider)providerSearch();else reload(true); });
    getChild<LLComboBox>("collection")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    { if(mProvider){const auto mode=getChild<LLComboBox>("collection")->getValue().asString();if(mode.compare(0,9,"category:")==0)getChild<LLFilterEditor>("search")->setText(mode.substr(9));else if(mode=="trending"||mode=="recent")getChild<LLFilterEditor>("search")->setText(LLStringExplicit(""));providerSearch();}else reload(true); });
    getChild<LLCheckBoxCtrl>("favourites")->setCommitCallback([this](LLUICtrl*,const LLSD&) { reload(true); });
    getChild<LLButton>("insert")->setCommitCallback([this](LLUICtrl*,const LLSD&) { insert(); });
    getChild<LLButton>("preview")->setCommitCallback([this](LLUICtrl*,const LLSD&) { preview(); });
    getChild<LLButton>("options")->setCommitCallback([this](LLUICtrl*,const LLSD&) { options(); });
    getChild<LLButton>("pop_out")->setCommitCallback([this](LLUICtrl*,const LLSD&) { popOut(); });
    getChild<LLComboBox>("tile_size")->setValue(FSChatMedia::boardOptions().size);
    getChild<LLComboBox>("tile_size")->setCommitCallback([this](LLUICtrl* control,const LLSD&) { FSChatMedia::boardSize(control->getValue().asInteger()); clearTiles(); });
    getChild<LLTabContainer>("sources")->setCommitCallback([this](LLUICtrl*,const LLSD&) { sourceChanged(); });
    getChild<LLButton>("more")->setCommitCallback([this](LLUICtrl*,const LLSD&) { providerSearch(true); });
    mExpanded=gSavedSettings.getBOOL("FSGifBoardExpanded");
    getChild<LLTabContainer>("sources")->selectTab(std::clamp(gSavedSettings.getS32("FSGifBoardSource"),0,1));
    return true;
}
bool FSFloaterGifBoard::current() const { return login(mAccount,mLogin,mGrid); }
void FSFloaterGifBoard::onOpen(const LLSD& key)
{
    if(auto* menu=mMenu.get())menu->die();
    if(mOpened&&current()){mTarget=key["target"];return;}
    clearTiles(); mAccount = gAgent.getID(); mLogin = gAgent.getSessionID(); mGrid = LLGridManager::getInstance()->getGrid();
    mOpened=true;mTarget = key["target"]; mSelected.clear(); sourceChanged();
    if (!mExpanded) reshape(380,440);
}
void FSFloaterGifBoard::onClose(bool quitting)
{ mOpened=false;FSKlipy::cancelBrowse();if(auto* menu=mMenu.get())menu->die();gSavedSettings.setS32("FSGifBoardSource",getChild<LLTabContainer>("sources")->getCurrentPanelIndex());clearTiles(); mTarget = LLSD(); mItems.clear(); mLibrary.clear(); LLFloater::onClose(quitting); }
void FSFloaterGifBoard::clearTiles()
{ if (mDocument) mDocument->deleteAllChildren(); mTiles.clear(); }
void FSFloaterGifBoard::reload(bool force)
{
    if (!current()) { clearTiles(); mItems.clear(); mLibrary.clear(); mValid=false; mStatus=tr("FSGifLoggedOut"); return; }
    if (!force && Clock::now()-mLoaded<std::chrono::seconds(2)) return; mLoaded=Clock::now();
    std::vector<Item> library; std::string error; mValid=FSGifLibrary::load(library,error);
    const bool changed=library!=mLibrary; mLibrary=std::move(library);
    auto* collections=getChild<LLComboBox>("collection"); const auto collection=collections->getValue().asString();
    if (!mProvider && (changed || collections->getItemCount()==0))
    {
        collections->removeall();collections->add(tr("FSGifAllCollections"),"");std::set<std::string> names;
        for(const auto& item:mLibrary) if(item.provider=="my"&&!item.collection.empty()) names.insert(item.collection);
        for(const auto& name:names) collections->add(name,name); collections->setValue(collection);
    }
    const bool personal=getChild<LLTabContainer>("sources")->getCurrentPanelIndex()==0;
    if(!personal){if(!error.empty())mStatus=error;return;}
    auto items=filter(mLibrary,getChild<LLFilterEditor>("search")->getValue().asString(),collection,getChild<LLCheckBoxCtrl>("favourites")->get());
    if(items!=mItems || force) { clearTiles();mItems=std::move(items);if(selected(mItems,mSelected).id.empty())mSelected.clear();if(force)mScroll->goToTop(); }
    mStatus=!error.empty()?error:personal?(mItems.empty()?tr("FSGifEmpty"):""):tr("FSGifKlipyUnavailable");
}
void FSFloaterGifBoard::sourceChanged()
{
    auto* search=getChild<LLFilterEditor>("search");
    if(mProvider)mProviderQuery=search->getValue().asString();else mMyQuery=search->getValue().asString();
    mProvider=getChild<LLTabContainer>("sources")->getCurrentPanelIndex()==1;
    search->setLabel(LLStringExplicit(mProvider?"Search KLIPY":"Search My GIFs"));search->setText(mProvider?mProviderQuery:mMyQuery);
    mProviderConfigured=FSKlipy::configured();
    getChild<LLCheckBoxCtrl>("favourites")->setVisible(!mProvider);
    getChild<LLTextBox>("provider")->setVisible(mProvider);
    auto* modes=getChild<LLComboBox>("collection");modes->removeall();mProviderCategories.clear();mSelected.clear();clearTiles();mItems.clear();mShareURLs.clear();
    if(mProvider)
    {
        modes->add("Trending","trending");modes->add("Search","search");modes->add("Recent","recent");modes->add("Favourites","favourites");modes->setValue("trending");
        reload(true);providerSearch();
    }
    else {FSKlipy::cancelBrowse();reload(true);}
}
void FSFloaterGifBoard::providerSearch(bool append)
{
    if(!current()||!mProvider)return;
    if(!append){mProviderPage=1;clearTiles();mItems.clear();mShareURLs.clear();mSelected.clear();mScroll->goToTop();}
    else mProviderPage=FSKlipy::snapshot().page+1;
    auto* modes=getChild<LLComboBox>("collection");auto mode=modes->getValue().asString();
    mProviderQuery=getChild<LLFilterEditor>("search")->getValue().asString();
    fs_klipy::Kind kind=fs_klipy::Kind::Trending;std::vector<std::string> slugs;
    if(mode=="favourites")
    {
        const auto query=fs_chat_media::lower(mProviderQuery);std::vector<std::string> saved;
        for(const auto& item:mLibrary)if(item.provider=="klipy"&&(query.empty()||fs_chat_media::lower(item.title+" "+item.slug).find(query)!=std::string::npos))saved.push_back(item.slug);
        const auto begin=static_cast<std::size_t>((mProviderPage-1)*8),end=(std::min)(saved.size(),begin+8);
        for(auto i=begin;i<end;++i)slugs.push_back(saved[i]);
        if(slugs.empty()){FSKlipy::cancelBrowse();mStatus="No saved KLIPY favourites.";return;}kind=fs_klipy::Kind::Items;
    }
    else if(mode.compare(0,9,"category:")==0||!mProviderQuery.empty())
    {kind=fs_klipy::Kind::Search;if(mode.compare(0,9,"category:")!=0)modes->setValue("search");}
    else if(mode=="recent")kind=fs_klipy::Kind::Recent;
    else if(mode=="search"){FSKlipy::cancelBrowse();mStatus="Type to search KLIPY.";return;}
    else modes->setValue("trending");
    FSKlipy::browse(kind,mProviderQuery,slugs,mProviderPage,append);mProviderRevision=~0u;
}
void FSFloaterGifBoard::updateProvider()
{
    const auto& snapshot=FSKlipy::snapshot();
    if(snapshot.revision!=mProviderRevision)
    {
        mProviderRevision=snapshot.revision;std::vector<Item> items;std::map<std::string,std::string> shares;std::size_t index=0;
        for(const auto& row:snapshot.items)
        {Item item;item.id="klipy_"+std::to_string(index++);item.provider="klipy";item.slug=row.slug;item.url=row.preview;item.title=row.available?row.title:"Unavailable GIF";shares[item.id]=row.share;items.push_back(std::move(item));}
        if(items!=mItems){clearTiles();mItems=std::move(items);if(selected(mItems,mSelected).id.empty())mSelected.clear();}mShareURLs=std::move(shares);
        if(snapshot.categories!=mProviderCategories)
        {
            mProviderCategories=snapshot.categories;auto* modes=getChild<LLComboBox>("collection");const auto mode=modes->getValue();
            modes->removeall();modes->add("Trending","trending");modes->add("Search","search");modes->add("Recent","recent");modes->add("Favourites","favourites");
            for(const auto& category:mProviderCategories)modes->add(category,"category:"+category);modes->setValue(mode);
        }
    }
    mStatus=!snapshot.error.empty()?snapshot.error:snapshot.loading?"Searching KLIPY…":"Powered by KLIPY";
    const auto mode=getChild<LLComboBox>("collection")->getValue().asString();
    if(snapshot.items.empty()&&snapshot.error.empty()&&!snapshot.loading)mStatus=mode=="favourites"?"No saved KLIPY favourites.":mode=="search"&&mProviderQuery.empty()?"Type to search KLIPY.":"No KLIPY results.";
    auto* more=getChild<LLButton>("more");bool hasMore=snapshot.more;
    if(getChild<LLComboBox>("collection")->getValue().asString()=="favourites")
    {std::size_t count=0;const auto query=fs_chat_media::lower(mProviderQuery);for(const auto& item:mLibrary)if(item.provider=="klipy"&&(query.empty()||fs_chat_media::lower(item.title+" "+item.slug).find(query)!=std::string::npos))++count;hasMore=count>static_cast<std::size_t>(snapshot.page*8);}
    more->setVisible(true);more->setEnabled(hasMore&&!snapshot.loading&&FSKlipy::configured());getChild<LLTextBox>("status")->setToolTip(LLStringExplicit("Powered by KLIPY. Searches and previews contact KLIPY directly."));
}
void FSFloaterGifBoard::layoutTiles()
{
    const auto width=(std::max)(120,mScroll->getContentWindowRect().getWidth()-4);
    const int wanted=std::clamp(FSChatMedia::boardOptions().size,100,240), columns=(std::max)(1,width/(wanted+8));
    const int cell=(width-8*(columns-1))/columns, height=cell+24, stride=height+8;
    const int rows=static_cast<int>((mItems.size()+columns-1)/columns), total=(std::max)(mScroll->getRect().getHeight(),rows*stride);
    if(mDocument->getRect().getWidth()!=width || mDocument->getRect().getHeight()!=total) { clearTiles();mDocument->reshape(width,total); }
    const auto top=(std::max)(0,mScroll->getDocPosVertical()/stride-1), bottom=(std::min)(rows,(mScroll->getDocPosVertical()+mScroll->getRect().getHeight())/stride+2);
    const auto first=static_cast<std::size_t>(top*columns), last=(std::min)(mItems.size(),static_cast<std::size_t>(bottom*columns));
    for(auto it=mTiles.begin();it!=mTiles.end();) if(it->first<first||it->first>=last) { delete it->second;it=mTiles.erase(it); } else ++it;
    const auto handle=getHandle();
    for(std::size_t index=first;index<last;++index)
    {
        if(!mTiles.count(index))
        {
            const auto id=mItems[index].id;
            auto* tile=new GifTile(mItems[index],[handle,id] { if(auto* board=dynamic_cast<FSFloaterGifBoard*>(handle.get())) { board->mSelected=id;board->choose(); } },
                [handle,id] { if(auto* board=dynamic_cast<FSFloaterGifBoard*>(handle.get())) { board->mSelected=id;board->insert(); } },
                [handle,id](S32 x,S32 y) { if(auto* board=dynamic_cast<FSFloaterGifBoard*>(handle.get())) { board->mSelected=id;board->options(x,y); } });
            mDocument->addChild(tile);mTiles[index]=tile;
        }
        auto* tile=static_cast<GifTile*>(mTiles[index]);tile->setOrigin(static_cast<int>(index%columns)*(cell+8),total-(static_cast<int>(index/columns)+1)*stride+8);tile->reshape(cell,height);tile->chosen=tile->id==mSelected;
        const auto rect=tile->calcScreenRect(),clip=mScroll->calcScreenRect(),window=gViewerWindow->getWindowRectScaled();
        const auto percent=visiblePercent({rect.mLeft,rect.mBottom,rect.mRight,rect.mTop},{{clip.mLeft,clip.mBottom,clip.mRight,clip.mTop},{window.mLeft,window.mBottom,window.mRight,window.mTop}});
        S32 x=0,y=0;tile->screenPointToLocal(gViewerWindow->getCurrentMouseX(),gViewerWindow->getCurrentMouseY(),&x,&y);
        const auto threshold=static_cast<unsigned>(std::clamp(FSChatMedia::boardOptions().threshold,1,100));
        if(percent>=threshold) { if(tile->visibleSince==Clock::time_point{})tile->visibleSince=Clock::now(); } else tile->visibleSince={};
        const bool play=autoplay(FSChatMedia::boardOptions().playback,percent,threshold,tile->pointInView(x,y),tile->chosen,!FSChatMedia::automaticMotionEnabled(),false);
        FSChatMedia::boardPlayback(tile->card,play&&(FSChatMedia::boardOptions().playback!=0||Clock::now()-tile->visibleSince>=std::chrono::milliseconds(120)),percent>0&&!gViewerWindow->getWindow()->getMinimized());
    }
}
void FSFloaterGifBoard::draw()
{
    reload();if(mProvider&&current())
    {
        const bool configured=FSKlipy::configured();
        if(configured&&!mProviderConfigured)providerSearch();
        mProviderConfigured=configured;updateProvider();
    }
    if(current())layoutTiles();
    getChild<LLTextBox>("target")->setText(FSChatUpload::targetLabel(mTarget));getChild<LLTextBox>("status")->setText(mStatus);
    getChild<LLButton>("insert")->setEnabled(current()&&!selected(mItems,mSelected).url.empty()&&mTarget.isMap());
    getChild<LLButton>("preview")->setEnabled(!selected(mItems,mSelected).url.empty()); if(!mProvider)getChild<LLButton>("more")->setVisible(false); LLFloater::draw();
}
void FSFloaterGifBoard::choose() { for(const auto& pair:mTiles)static_cast<GifTile*>(pair.second)->chosen=static_cast<GifTile*>(pair.second)->id==mSelected; }
void FSFloaterGifBoard::insert()
{
    const auto item=selected(mItems,mSelected);if(!current()||item.url.empty())return;
    const auto url=mProvider?mShareURLs[item.id]:item.url;
    if(mProvider)
    {
        const auto& snapshot=FSKlipy::snapshot();
        if(!FSKlipy::configured()||!std::any_of(snapshot.items.begin(),snapshot.items.end(),[&](const auto& row){return row.available&&row.slug==item.slug&&row.share==url;})){mStatus=tr("FSGifKlipyUnavailable");return;}
    }
    if(!FSChatUpload::insertLink(mTarget,url,mProvider?item.slug:"",mProvider?mProviderQuery:"")){mStatus=tr("FSGifNoTarget");return;}
    if(!mExpanded&&!getChild<LLCheckBoxCtrl>("keep_open")->get())closeFloater();
}
void FSFloaterGifBoard::preview()
{ for(const auto& pair:mTiles)if(static_cast<GifTile*>(pair.second)->id==mSelected)FSChatMedia::boardPreview(static_cast<GifTile*>(pair.second)->card); }
void FSFloaterGifBoard::popOut()
{ mExpanded=!mExpanded;gSavedSettings.setBOOL("FSGifBoardExpanded",mExpanded);reshape(mExpanded?680:380,mExpanded?580:440);getChild<LLButton>("pop_out")->setToolTip(tr(mExpanded?"FSGifCompact":"FSGifPopOut")); }
void FSFloaterGifBoard::editItem(bool create)
{ if(current()&&mValid)LLFloaterReg::showInstance("gif_item",create?LLSD():itemKey(selected(mItems,mSelected))); }
void FSFloaterGifBoard::transfer(bool save)
{
    const auto handle=getHandle(); const auto account=mAccount,session=mLogin;const auto grid=mGrid;
    const auto done=[handle,account,session,grid,save](const std::vector<std::string>& paths,LLFilePicker::ELoadFilter,LLFilePicker::ESaveFilter)
    { auto* board=dynamic_cast<FSFloaterGifBoard*>(handle.get());if(!board||!login(account,session,grid)||paths.size()!=1)return;
      std::string error;const bool ok=save?FSGifLibrary::exportFile(paths[0],error):FSGifLibrary::importFile(paths[0],error);board->reload(true);board->mStatus=ok?tr("FSGifSaved"):error; };
    if(save)LLFilePickerReplyThread::startPicker(done,LLFilePicker::FFSAVE_XML,"My GIFs.xml");else LLFilePickerReplyThread::startPicker(done,LLFilePicker::FFLOAD_XML,false);
}
void FSFloaterGifBoard::options(S32 x,S32 y)
{
    if(auto* prior=mMenu.get())prior->die();LLContextMenu::Params p;p.name="gif_options";auto* menu=LLUICtrlFactory::create<LLContextMenu>(p);gMenuHolder->addChild(menu);mMenu=menu->getHandle();
    const auto handle=getHandle();
    for(const auto& action:{std::pair<const char*,const char*>{"add","FSGifAdd"},{"edit","FSGifEdit"},{"favourite","FSGifFavourite"},{"remove","FSGifRemove"},{"copy","FSChatMediaCopyLink"},{"report","FSGifReport"},{"import","FSGifImport"},{"export","FSGifExport"}})
    {
        if(!mProvider&&std::string(action.first)=="report")continue;
        if(mProvider&&(std::string(action.first)=="add"||std::string(action.first)=="edit"||std::string(action.first)=="import"||std::string(action.first)=="export"))continue;
        LLMenuItemCallGL::Params item;item.name=action.first;item.label=tr(action.second);const std::string name=action.first;
        item.on_click.function([handle,name](LLUICtrl*,const LLSD&)
        { auto* board=dynamic_cast<FSFloaterGifBoard*>(handle.get());if(!board||!board->current())return;
          auto value=selected(board->mItems,board->mSelected);std::string error;
          if(name=="add"||name=="edit")board->editItem(name=="add");
          else if(name=="import"||name=="export")board->transfer(name=="export");
          else if(!value.id.empty())
          {
              if(board->mProvider)
              {
                  const auto old=std::find_if(board->mLibrary.begin(),board->mLibrary.end(),[&](const auto& item){return item.provider=="klipy"&&item.slug==value.slug;});
                  if(name=="report"&&fs_klipy::slugValid(value.slug)){LLSD key;key["slug"]=value.slug;LLFloaterReg::showInstance("gif_report",key);}
                  else if(name=="copy")gViewerWindow->getWindow()->copyTextToClipboard(utf8str_to_wstring(board->mShareURLs[value.id]));
                  else if((name=="remove"||name=="favourite")&&old!=board->mLibrary.end())FSGifLibrary::remove(old->id,error);
                  else if(name=="favourite"&&fs_klipy::slugValid(value.slug)){value.id.clear();value.url.clear();value.title=utf8str_truncate(value.title,128);value.favourite=true;FSGifLibrary::save(std::move(value),error);}
                  board->reload(true);if(board->getChild<LLComboBox>("collection")->getValue().asString()=="favourites")board->providerSearch();
              }
              else if(name=="remove")FSGifLibrary::remove(value.id,error);
              else if(name=="favourite"){value.favourite=!value.favourite;FSGifLibrary::save(std::move(value),error);}
              else if(name=="copy")gViewerWindow->getWindow()->copyTextToClipboard(utf8str_to_wstring(value.url));
              board->reload(true);board->mStatus=error;
          } });
        menu->addChild(LLUICtrlFactory::create<LLMenuItemCallGL>(item));
    }
    if(x<0||y<0)getChild<LLButton>("options")->localPointToScreen(0,0,&x,&y);menu->show(x,y,this);
}
bool FSFloaterGifBoard::handleKeyHere(KEY key,MASK mask)
{
    if(mask==MASK_NONE&&(key==KEY_LEFT||key==KEY_RIGHT||key==KEY_UP||key==KEY_DOWN)&&!getChild<LLFilterEditor>("search")->hasFocus()&&!mItems.empty())
    { auto at=std::find_if(mItems.begin(),mItems.end(),[this](const auto& item){return item.id==mSelected;});int index=at==mItems.end()?0:static_cast<int>(at-mItems.begin());const int width=(std::max)(120,mScroll->getContentWindowRect().getWidth()-4),columns=(std::max)(1,width/(std::clamp(FSChatMedia::boardOptions().size,100,240)+8));
      if(at!=mItems.end())index=std::clamp(index+(key==KEY_LEFT?-1:key==KEY_RIGHT?1:key==KEY_UP?-columns:columns),0,static_cast<int>(mItems.size())-1);
      mSelected=mItems[index].id;choose();const int cell=(width-8*(columns-1))/columns,stride=cell+32,y=mDocument->getRect().getHeight()-(index/columns+1)*stride+8;
      mScroll->scrollToShowRect(LLRect((index%columns)*(cell+8),y+cell+24,(index%columns)*(cell+8)+cell,y));
      // Move keyboard focus with the selection, including newly virtualized
      // rows, so Enter/Space cannot operate on the formerly focused GIF.
      layoutTiles();
      const auto tile=mTiles.find(static_cast<std::size_t>(index));
      if(tile!=mTiles.end())
      {
          auto* selected=static_cast<GifTile*>(tile->second);
          gFocusMgr.setKeyboardFocus(selected->card?selected->card:selected);
      }
      return true; }
    return LLFloater::handleKeyHere(key,mask);
}
bool FSFloaterGifItem::postBuild()
{ getChild<LLButton>("save")->setCommitCallback([this](LLUICtrl*,const LLSD&){save();});getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*,const LLSD&){closeFloater();});return true; }
void FSFloaterGifItem::onOpen(const LLSD& key)
{
    mAccount=gAgent.getID();mLogin=gAgent.getSessionID();mGrid=LLGridManager::getInstance()->getGrid();mID=key["id"].asString();
    for(const auto* field:{"url","title","tags","collection"})getChild<LLLineEditor>(field)->setText(key[field].asString());getChild<LLCheckBoxCtrl>("favourite")->set(key["favourite"].asBoolean());getChild<LLTextBox>("status")->setText(LLStringExplicit(""));
}
void FSFloaterGifItem::save()
{
    if(!login(mAccount,mLogin,mGrid)){closeFloater();return;}
    Item item;item.id=mID;item.url=getChild<LLLineEditor>("url")->getText();item.title=getChild<LLLineEditor>("title")->getText();item.tags=getChild<LLLineEditor>("tags")->getText();item.collection=getChild<LLLineEditor>("collection")->getText();item.favourite=getChild<LLCheckBoxCtrl>("favourite")->get();
    std::string error;if(!FSGifLibrary::save(std::move(item),error)){getChild<LLTextBox>("status")->setText(error);return;}
    if(auto* board=LLFloaterReg::findTypedInstance<FSFloaterGifBoard>("gif_board"))board->reload(true);closeFloater();
}

bool FSFloaterGifReport::postBuild()
{
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*,const LLSD&){closeFloater();});
    getChild<LLButton>("report")->setCommitCallback([this](LLUICtrl*,const LLSD&)
    {const auto reason=getChild<LLComboBox>("reason")->getValue().asString();
     if(FSKlipy::reportItem(mSlug,reason,mAccount,mLogin,mGrid)){notice("Report queued for KLIPY.");closeFloater();}else notice("The report could not be queued. Reopen GIFs and try again.");});return true;
}
void FSFloaterGifReport::onOpen(const LLSD& key)
{mAccount=gAgent.getID();mLogin=gAgent.getSessionID();mGrid=LLGridManager::getInstance()->getGrid();mSlug=key["slug"].asString();}
