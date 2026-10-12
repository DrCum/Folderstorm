/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsfloatersquirtynotes.h"
#include "fssquirtynotes.h"
#include "fssquirtypolicy.h"

#include "llavatarname.h"
#include "llbutton.h"
#include "llcallingcard.h"
#include "llcombobox.h"
#include "llfloateravatarpicker.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "llspinctrl.h"
#include "llstartup.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

bool FSFloaterSquirtyNotes::postBuild()
{
    getChild<LLComboBox>("im_mode")->setCommitCallback([this](LLUICtrl*, const LLSD&) { updateEnabled(); });
    getChild<LLComboBox>("online_mode")->setCommitCallback([this](LLUICtrl*, const LLSD&) { updateEnabled(); });
    getChild<LLButton>("im_add")->setClickedCallback([this] { addResident(false); });
    getChild<LLButton>("online_add")->setClickedCallback([this] { addResident(true); });
    getChild<LLButton>("im_remove")->setClickedCallback([this] { removeResident(false); });
    getChild<LLButton>("online_remove")->setClickedCallback([this] { removeResident(true); });
    getChild<LLButton>("apply")->setClickedCallback([this] { apply(); });
    getChild<LLButton>("cancel")->setClickedCallback([this] { closeFloater(); });
    getChild<LLButton>("check")->setClickedCallback([this] { test(false); });
    getChild<LLButton>("test")->setClickedCallback([this] { test(true); });
    return true;
}

void FSFloaterSquirtyNotes::onOpen(const LLSD&)
{
    getChild<LLComboBox>("im_mode")->setValue(gSavedPerAccountSettings.getS32("FSSquirtyIMMode"));
    getChild<LLComboBox>("online_mode")->setValue(gSavedPerAccountSettings.getS32("FSSquirtyOnlineMode"));
    getChild<LLSpinCtrl>("silence")->setValue(gSavedPerAccountSettings.getU32("FSSquirtyIMSilence"));
    getChild<LLSpinCtrl>("port")->setValue(gSavedSettings.getS32("FSSquirtyPort"));
    getChild<LLLineEditor>("token")->setText(FSSquirtyNotes::token());
    mIMResidents = gSavedPerAccountSettings.getLLSD("FSSquirtyIMResidents");
    mOnlineResidents = gSavedPerAccountSettings.getLLSD("FSSquirtyOnlineResidents");
    mStatusOverride.clear();
    refreshList(false);
    refreshList(true);
    updateEnabled();
}

void FSFloaterSquirtyNotes::updateEnabled()
{
    const bool logged_in = LLStartUp::getStartupState() == STATE_STARTED;
    for (const auto& prefix : {std::string("im"), std::string("online")})
    {
        const bool custom = logged_in && getChild<LLComboBox>(prefix + "_mode")->getValue().asInteger() == 2;
        getChild<LLComboBox>(prefix + "_mode")->setEnabled(logged_in);
        for (const auto& suffix : {"_residents", "_name", "_add", "_remove"})
            getChild<LLUICtrl>(prefix + suffix)->setEnabled(custom);
    }
    getChild<LLSpinCtrl>("silence")->setEnabled(logged_in && getChild<LLComboBox>("im_mode")->getValue().asInteger() != 0);
    getChild<LLButton>("apply")->setEnabled(logged_in);
}

void FSFloaterSquirtyNotes::refreshList(bool online)
{
    const LLSD& residents = online ? mOnlineResidents : mIMResidents;
    auto* list = getChild<LLScrollListCtrl>(online ? "online_residents" : "im_residents");
    list->deleteAllItems();
    for (auto it = residents.beginMap(); it != residents.endMap(); ++it)
    {
        LLSD row;
        row["id"] = LLUUID(it->first);
        row["columns"][0]["value"] = it->second.asString();
        list->addElement(row);
    }
    list->sortByColumnIndex(0, true);
}

void FSFloaterSquirtyNotes::addResident(bool online)
{
    const auto handle = getHandle();
    auto* editor = getChild<LLLineEditor>(online ? "online_name" : "im_name");
    std::string search = editor->getText();
    LLStringUtil::trim(search);
    auto* picker = LLFloaterAvatarPicker::show(
        [handle, online](const uuid_vec_t& ids, const std::vector<LLAvatarName>& names)
        {
            auto* self = dynamic_cast<FSFloaterSquirtyNotes*>(handle.get());
            if (!self || !self->getVisible() || ids.empty() || names.empty()) return;
            if (online && !LLAvatarTracker::instance().isBuddy(ids.front())) return;
            (online ? self->mOnlineResidents : self->mIMResidents)[ids.front().asString()] = names.front().getCompleteName();
            self->getChild<LLLineEditor>(online ? "online_name" : "im_name")->setText("");
            self->refreshList(online);
        }, false, true, true, search, this);
    if (picker)
    {
        addDependentFloater(picker);
        if (online)
        {
            picker->setOkBtnEnableCb([](const uuid_vec_t& ids)
            {
                return !ids.empty() && LLAvatarTracker::instance().isBuddy(ids.front());
            });
            if (search.empty()) picker->openFriendsTab();
        }
        else picker->setOkBtnEnableCb([](const uuid_vec_t& ids) { return !ids.empty(); });
    }
}

void FSFloaterSquirtyNotes::removeResident(bool online)
{
    auto* list = getChild<LLScrollListCtrl>(online ? "online_residents" : "im_residents");
    for (auto* item : list->getAllSelected())
        (online ? mOnlineResidents : mIMResidents).erase(item->getUUID().asString());
    refreshList(online);
}

void FSFloaterSquirtyNotes::apply()
{
    if (LLStartUp::getStartupState() != STATE_STARTED) return;
    const S32 im = getChild<LLComboBox>("im_mode")->getValue().asInteger();
    const S32 online = getChild<LLComboBox>("online_mode")->getValue().asInteger();
    std::string value = getChild<LLLineEditor>("token")->getText();
    LLStringUtil::trim(value);
    if ((!value.empty() || im != 0 || online != 0) &&
        !FSSquirtyPolicy::validToken(value))
    {
        mStatusOverride = "SetupNeeded";
        return;
    }
    FSSquirtyNotes::configurationChanged();
    FSSquirtyNotes::setToken(value);
    gSavedSettings.setS32("FSSquirtyPort", getChild<LLSpinCtrl>("port")->getValue().asInteger());
    gSavedPerAccountSettings.setS32("FSSquirtyIMMode", im);
    gSavedPerAccountSettings.setS32("FSSquirtyOnlineMode", online);
    gSavedPerAccountSettings.setU32("FSSquirtyIMSilence", getChild<LLSpinCtrl>("silence")->getValue().asInteger());
    gSavedPerAccountSettings.setLLSD("FSSquirtyIMResidents", mIMResidents);
    gSavedPerAccountSettings.setLLSD("FSSquirtyOnlineResidents", mOnlineResidents);
    mStatusOverride = "Saved";
}

void FSFloaterSquirtyNotes::test(bool squirt)
{
    mStatusOverride.clear();
    std::string value = getChild<LLLineEditor>("token")->getText();
    LLStringUtil::trim(value);
    FSSquirtyNotes::test(getChild<LLSpinCtrl>("port")->getValue().asInteger(), value, squirt);
}

void FSFloaterSquirtyNotes::draw()
{
    updateEnabled();
    const std::string status = FSSquirtyNotes::status();
    getChild<LLTextBox>("status")->setText(getString(mStatusOverride.empty() ? status : mStatusOverride));
    LLFloater::draw();
}
