/**
* @file llsearchableui.cpp
*
* $LicenseInfo:firstyear=2019&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2019, Linden Research, Inc.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation;
* version 2.1 of the License only.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*
* Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
* $/LicenseInfo$
*/

#include "llviewerprecompiledheaders.h"
#include "llsearchableui.h"

#include "llview.h"
#include "lltabcontainer.h"
#include "llmenugl.h"

ll::prefs::SearchableItem::~SearchableItem()
{}

void ll::prefs::SearchableItem::setNotHighlighted()
{
    mCtrl->setHighlighted( false );
}

bool ll::prefs::SearchableItem::hightlightAndHide( LLWString const &aFilter )
{
    if( mCtrl->getHighlighted() )
        return true;

    LLView const *pView = dynamic_cast< LLView const* >( mCtrl );
    if( pView && !pView->getVisible() )
        return false;

    if( aFilter.empty() )
    {
        mCtrl->setHighlighted( false );
        return true;
    }

    bool matches = mLabel.find(aFilter) != LLWString::npos;
    for (const auto& alias : mAliases)
        matches |= alias.find(aFilter) != LLWString::npos;
    if (matches)
    {
        mCtrl->setHighlighted( true );
        return true;
    }

    return false;
}

ll::prefs::PanelData::~PanelData()
{}

bool ll::prefs::PanelData::hightlightAndHide( LLWString const &aFilter )
{
    for( tSearchableItemList::iterator itr = mChildren.begin(); itr  != mChildren.end(); ++itr )
        (*itr)->setNotHighlighted();

    for (tPanelDataList::iterator itr = mChildPanel.begin(); itr != mChildPanel.end(); ++itr)
        (*itr)->setNotHighlighted();

    // <FS:Ansariel> FIRE-23969: This breaks prefs search - and isn't needed on FS
    //if (aFilter.empty())
    //{
    //  return true;
    //}
    // </FS:Ansariel>

    bool bVisible(false);
    for( tSearchableItemList::iterator itr = mChildren.begin(); itr  != mChildren.end(); ++itr )
        bVisible |= (*itr)->hightlightAndHide( aFilter );

    for( tPanelDataList::iterator itr = mChildPanel.begin(); itr  != mChildPanel.end(); ++itr )
        bVisible |= (*itr)->hightlightAndHide( aFilter );

    return bVisible;
}

void ll::prefs::PanelData::setNotHighlighted()
{
    for (tSearchableItemList::iterator itr = mChildren.begin(); itr != mChildren.end(); ++itr)
        (*itr)->setNotHighlighted();

    for (tPanelDataList::iterator itr = mChildPanel.begin(); itr != mChildPanel.end(); ++itr)
        (*itr)->setNotHighlighted();
}

bool ll::prefs::TabContainerData::hightlightAndHide( LLWString const &aFilter )
{
    LLPanel* currentPanel = mTabContainer->getCurrentPanel();
    if (!aFilter.empty() && mBeforeSearchVisibility.empty())
    {
        mBeforeSearchPanel = mTabContainer->getCurrentPanel();
        for (const auto& panel : mChildPanel)
            if (panel->mPanel)
                mBeforeSearchVisibility.emplace_back(panel->mPanel, mTabContainer->getTabVisibility(panel->mPanel));
    }
    for( tSearchableItemList::iterator itr = mChildren.begin(); itr  != mChildren.end(); ++itr )
        (*itr)->setNotHighlighted( );

    bool bVisible(false);
    for( tSearchableItemList::iterator itr = mChildren.begin(); itr  != mChildren.end(); ++itr )
        bVisible |= (*itr)->hightlightAndHide( aFilter );

    LLPanel const* firstMatch = nullptr;
    for( tPanelDataList::iterator itr = mChildPanel.begin(); itr  != mChildPanel.end(); ++itr )
    {
        bool searchable = true;
        if (!aFilter.empty())
        {
            for (const auto& visibility : mBeforeSearchVisibility)
                if (visibility.first == (*itr)->mPanel && !visibility.second)
                    searchable = false;
        }
        // Search must not reveal tabs hidden by a feature or platform gate.
        if (!searchable)
            (*itr)->setNotHighlighted();
        bool bPanelVisible = searchable && (*itr)->hightlightAndHide(aFilter);
        if( (*itr)->mPanel )
        {
            // With no active search, keep feature-gated visibility intact.
            // A cleared search restores its captured visibility below.
            if (!aFilter.empty())
                mTabContainer->setTabVisibility((*itr)->mPanel, bPanelVisible);
            if (bPanelVisible && !firstMatch)
                firstMatch = (*itr)->mPanel;
        }
        bVisible |= bPanelVisible;
    }

    if (aFilter.empty() && !mBeforeSearchVisibility.empty())
    {
        for (const auto& visibility : mBeforeSearchVisibility)
            mTabContainer->setTabVisibility(visibility.first, visibility.second);
        if (mBeforeSearchPanel)
            mTabContainer->selectTabPanel(mBeforeSearchPanel);
        mBeforeSearchVisibility.clear();
        mBeforeSearchPanel = nullptr;
    }
    else if (!aFilter.empty() && firstMatch)
    {
        mTabContainer->selectTabPanel(const_cast<LLPanel*>(firstMatch));
    }
    else if (aFilter.empty() && currentPanel)
    {
        mTabContainer->selectTabPanel(currentPanel);
    }
    return bVisible;
}

ll::statusbar::SearchableItem::SearchableItem()
    : mMenu(0)
    , mCtrl(0)
    , mWasHiddenBySearch( false )
{ }

void ll::statusbar::SearchableItem::setNotHighlighted( )
{
    for( tSearchableItemList::iterator itr = mChildren.begin(); itr  != mChildren.end(); ++itr )
        (*itr)->setNotHighlighted( );

    if( mCtrl )
    {
        mCtrl->setHighlighted( false );

        if (mWasHiddenBySearch)
        {
            mMenu->setVisible(true);
            mWasHiddenBySearch = false;
        }
    }
}

bool ll::statusbar::SearchableItem::hightlightAndHide(LLWString const &aFilter, bool hide)
{
    if ((mMenu && !mMenu->getVisible() && !mWasHiddenBySearch) || dynamic_cast<LLMenuItemTearOffGL*>(mMenu))
        return false;

    setNotHighlighted( );

    if( aFilter.empty() )
    {
        if( mCtrl )
            mCtrl->setHighlighted( false );
        return true;
    }

    bool bHighlighted(!hide);
    if( mLabel.find( aFilter ) != LLWString::npos )
    {
        if( mCtrl )
            mCtrl->setHighlighted( true );
        bHighlighted = true;
    }

    bool bVisible(false);
    for (tSearchableItemList::iterator itr = mChildren.begin(); itr != mChildren.end(); ++itr)
        bVisible |= (*itr)->hightlightAndHide(aFilter, !bHighlighted);

    if (mCtrl && !bVisible && !bHighlighted)
    {
        mWasHiddenBySearch = true;
        mMenu->setVisible(false);
    }
    return bVisible || bHighlighted;
}
