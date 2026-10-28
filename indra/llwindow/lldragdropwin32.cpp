/**
 * @file lldragdrop32.cpp
 * @brief Handler for Windows specific drag and drop (OS to client) code
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#if LL_WINDOWS

#if LL_OS_DRAGDROP_ENABLED

#include "linden_common.h"

#include "llwindowwin32.h"
#include "llkeyboardwin32.h"
#include "llwindowcallbacks.h"
#include "lldragdropwin32.h"
#include "llclipboardimage.h"
#include <shellapi.h>

class LLDragDropWin32Target:
    public IDropTarget
{
    public:
        ////////////////////////////////////////////////////////////////////////////////
        //
        LLDragDropWin32Target( HWND  hWnd ) :
            mRefCount( 1 ),
            mAppWindowHandle( hWnd ),
            mAllowDrop(false),
            mIsSlurl(false)
        {
        };

        virtual ~LLDragDropWin32Target()
        {
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        ULONG __stdcall AddRef( void )
        {
            return InterlockedIncrement( &mRefCount );
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        ULONG __stdcall Release( void )
        {
            LONG count = InterlockedDecrement( &mRefCount );

            if ( count == 0 )
            {
                delete this;
                return 0;
            }
            else
            {
                return count;
            };
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        HRESULT __stdcall QueryInterface( REFIID iid, void** ppvObject )
        {
            if ( iid == IID_IUnknown || iid == IID_IDropTarget )
            {
                AddRef();
                *ppvObject = this;
                return S_OK;
            }
            else
            {
                *ppvObject = 0;
                return E_NOINTERFACE;
            };
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        HRESULT __stdcall DragEnter( IDataObject* pDataObject, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect )
        {
            mDropFiles.clear();
            FORMATETC fileFormat = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
            if (pDataObject->QueryGetData(&fileFormat) == S_OK)
            {
                mDropUrl.clear(); mAllowDrop = readFiles(pDataObject, mDropFiles);
                if (!mAllowDrop) mDropFiles.clear();
                *pdwEffect = DROPEFFECT_NONE;
                if (mAllowDrop) { sendFiles(pt, LLWindowCallbacks::DNDA_START_TRACKING, pdwEffect); SetFocus(mAppWindowHandle); }
                return S_OK;
            }
            FORMATETC fmtetc = { CF_TEXT, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };

            // support CF_TEXT using a HGLOBAL?
            if ( S_OK == pDataObject->QueryGetData( &fmtetc ) )
            {
                mAllowDrop = true;
                mDropUrl = std::string();
                mIsSlurl = false;

                STGMEDIUM stgmed;
                if( S_OK == pDataObject->GetData( &fmtetc, &stgmed ) )
                {
                    PVOID data = GlobalLock( stgmed.hGlobal );
                    mDropUrl = std::string( (char*)data );
                    // XXX MAJOR MAJOR HACK!
                    LLWindowWin32 *window_imp = (LLWindowWin32 *)GetWindowLongPtr( mAppWindowHandle, GWLP_USERDATA );
                    if (NULL != window_imp)
                    {
                        LLCoordGL gl_coord( 0, 0 );

                        POINT pt2;
                        pt2.x = pt.x;
                        pt2.y = pt.y;
                        ScreenToClient( mAppWindowHandle, &pt2 );

                        LLCoordWindow cursor_coord_window( pt2.x, pt2.y );
                        MASK mask = gKeyboard->currentMask(true);

                        LLWindowCallbacks::DragNDropResult result = window_imp->completeDragNDropRequest( cursor_coord_window.convert(), mask,
                            LLWindowCallbacks::DNDA_START_TRACKING, mDropUrl );

                        switch (result)
                        {
                        case LLWindowCallbacks::DND_COPY:
                            *pdwEffect = DROPEFFECT_COPY;
                            break;
                        case LLWindowCallbacks::DND_LINK:
                            *pdwEffect = DROPEFFECT_LINK;
                            break;
                        case LLWindowCallbacks::DND_MOVE:
                            *pdwEffect = DROPEFFECT_MOVE;
                            break;
                        case LLWindowCallbacks::DND_NONE:
                        default:
                            *pdwEffect = DROPEFFECT_NONE;
                            break;
                        }
                    };

                    GlobalUnlock( stgmed.hGlobal );
                    ReleaseStgMedium( &stgmed );
                };
                SetFocus( mAppWindowHandle );
            }
            else
            {
                mAllowDrop = false;
                *pdwEffect = DROPEFFECT_NONE;
            };

            return S_OK;
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        HRESULT __stdcall DragOver( DWORD grfKeyState, POINTL pt, DWORD* pdwEffect )
        {
            if (!mDropFiles.empty()) { sendFiles(pt, LLWindowCallbacks::DNDA_TRACK, pdwEffect); return S_OK; }
            if ( mAllowDrop )
            {
                // XXX MAJOR MAJOR HACK!
                LLWindowWin32 *window_imp = (LLWindowWin32 *)GetWindowLongPtr( mAppWindowHandle, GWLP_USERDATA );
                if (NULL != window_imp)
                {
                    LLCoordGL gl_coord( 0, 0 );

                    POINT pt2;
                    pt2.x = pt.x;
                    pt2.y = pt.y;
                    ScreenToClient( mAppWindowHandle, &pt2 );

                    LLCoordWindow cursor_coord_window( pt2.x, pt2.y );
                    MASK mask = gKeyboard->currentMask(true);

                    LLWindowCallbacks::DragNDropResult result = window_imp->completeDragNDropRequest( cursor_coord_window.convert(), mask,
                        LLWindowCallbacks::DNDA_TRACK, mDropUrl );

                    switch (result)
                    {
                    case LLWindowCallbacks::DND_COPY:
                        *pdwEffect = DROPEFFECT_COPY;
                        break;
                    case LLWindowCallbacks::DND_LINK:
                        *pdwEffect = DROPEFFECT_LINK;
                        break;
                    case LLWindowCallbacks::DND_MOVE:
                        *pdwEffect = DROPEFFECT_MOVE;
                        break;
                    case LLWindowCallbacks::DND_NONE:
                    default:
                        *pdwEffect = DROPEFFECT_NONE;
                        break;
                    }
                };
            }
            else
            {
                *pdwEffect = DROPEFFECT_NONE;
            };

            return S_OK;
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        HRESULT __stdcall DragLeave( void )
        {
            if (!mDropFiles.empty()) { DWORD effect = 0; sendFiles(POINTL{}, LLWindowCallbacks::DNDA_STOP_TRACKING, &effect); mDropFiles.clear(); mAllowDrop = false; return S_OK; }
            // XXX MAJOR MAJOR HACK!
            LLWindowWin32 *window_imp = (LLWindowWin32 *)GetWindowLongPtr( mAppWindowHandle, GWLP_USERDATA );
            if (NULL != window_imp)
            {
                LLCoordGL gl_coord( 0, 0 );
                MASK mask = gKeyboard->currentMask(true);
                window_imp->completeDragNDropRequest( gl_coord, mask, LLWindowCallbacks::DNDA_STOP_TRACKING, mDropUrl );
            };
            return S_OK;
        };

        ////////////////////////////////////////////////////////////////////////////////
        //
        HRESULT __stdcall Drop( IDataObject* pDataObject, DWORD grfKeyState, POINTL pt, DWORD* pdwEffect )
        {
            if (!mDropFiles.empty())
            {
                std::vector<std::string> current; *pdwEffect = DROPEFFECT_NONE;
                if (readFiles(pDataObject, current) && current == mDropFiles) sendFiles(pt, LLWindowCallbacks::DNDA_DROPPED, pdwEffect);
                DWORD effect = 0; sendFiles(pt, LLWindowCallbacks::DNDA_STOP_TRACKING, &effect);
                mDropFiles.clear(); mAllowDrop = false; return S_OK;
            }
            if ( mAllowDrop )
            {
                // window impl stored in Window data (neat!)
                LLWindowWin32 *window_imp = (LLWindowWin32 *)GetWindowLongPtr( mAppWindowHandle, GWLP_USERDATA );
                if ( NULL != window_imp )
                {
                    POINT pt_client;
                    pt_client.x = pt.x;
                    pt_client.y = pt.y;
                    ScreenToClient( mAppWindowHandle, &pt_client );

                    LLCoordWindow cursor_coord_window( pt_client.x, pt_client.y );
                    LLCoordGL gl_coord(cursor_coord_window.convert());
                    LL_INFOS() << "### (Drop) URL is: " << mDropUrl << LL_ENDL;
                    LL_INFOS() << "###        raw coords are: " << pt.x << " x " << pt.y << LL_ENDL;
                    LL_INFOS() << "###      client coords are: " << pt_client.x << " x " << pt_client.y << LL_ENDL;
                    LL_INFOS() << "###         GL coords are: " << gl_coord.mX << " x " << gl_coord.mY << LL_ENDL;
                    LL_INFOS() << LL_ENDL;

                    // no keyboard modifier option yet but we could one day
                    MASK mask = gKeyboard->currentMask( true );

                    // actually do the drop
                    LLWindowCallbacks::DragNDropResult result = window_imp->completeDragNDropRequest( gl_coord, mask,
                        LLWindowCallbacks::DNDA_DROPPED, mDropUrl );

                    switch (result)
                    {
                    case LLWindowCallbacks::DND_COPY:
                        *pdwEffect = DROPEFFECT_COPY;
                        break;
                    case LLWindowCallbacks::DND_LINK:
                        *pdwEffect = DROPEFFECT_LINK;
                        break;
                    case LLWindowCallbacks::DND_MOVE:
                        *pdwEffect = DROPEFFECT_MOVE;
                        break;
                    case LLWindowCallbacks::DND_NONE:
                    default:
                        *pdwEffect = DROPEFFECT_NONE;
                        break;
                    }
                };
            }
            else
            {
                *pdwEffect = DROPEFFECT_NONE;
            };

            return S_OK;
        };

    ////////////////////////////////////////////////////////////////////////////////
    //
    private:
        static bool readFiles(IDataObject* object, std::vector<std::string>& files)
        {
            files.clear(); FORMATETC format = {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}; STGMEDIUM medium{};
            if (object->GetData(&format, &medium) != S_OK) return false;
            struct Release { STGMEDIUM* value; ~Release() { ReleaseStgMedium(value); } } release{&medium};
            if (medium.tymed != TYMED_HGLOBAL || !medium.hGlobal) return false;
            try
            {
                const auto drop = reinterpret_cast<HDROP>(medium.hGlobal);
                const auto count = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
                if (!count || count > ll_clipboard_image::MaxFiles) return false;
                for (UINT i = 0; i < count; ++i)
                {
                    const auto length = DragQueryFileW(drop, i, nullptr, 0); if (!length || length > 4096) return false;
                    std::wstring file(static_cast<std::size_t>(length) + 1, L'\0');
                    if (DragQueryFileW(drop, i, file.data(), length + 1) != length) return false;
                    file.resize(length); files.push_back(ll_convert_wide_to_string(file));
                }
                return true;
            }
            catch (...) { files.clear(); return false; }
        }
        void sendFiles(POINTL point, LLWindowCallbacks::DragNDropAction action, DWORD* effect)
        {
            *effect = DROPEFFECT_NONE;
            auto* window = reinterpret_cast<LLWindowWin32*>(GetWindowLongPtr(mAppWindowHandle, GWLP_USERDATA));
            if (!window) return;
            POINT local{point.x, point.y}; ScreenToClient(mAppWindowHandle, &local);
            LLCoordWindow coords(local.x, local.y);
            const auto result = window->completeFileDragNDropRequest(coords.convert(), gKeyboard->currentMask(true), action, mDropFiles);
            if (result == LLWindowCallbacks::DND_COPY) *effect = DROPEFFECT_COPY;
        }
        std::vector<std::string> mDropFiles;
        LONG mRefCount;
        HWND mAppWindowHandle;
        bool mAllowDrop;
        std::string mDropUrl;
        bool mIsSlurl;
        friend class LLWindowWin32;
};

////////////////////////////////////////////////////////////////////////////////
//
LLDragDropWin32::LLDragDropWin32() :
    mDropTarget( NULL ),
    mDropWindowHandle( NULL )

{
}

////////////////////////////////////////////////////////////////////////////////
//
LLDragDropWin32::~LLDragDropWin32()
{
}

////////////////////////////////////////////////////////////////////////////////
//
bool LLDragDropWin32::init( HWND hWnd )
{
    if ( NOERROR != OleInitialize( NULL ) )
        return false;

    mDropTarget = new LLDragDropWin32Target( hWnd );
    if ( mDropTarget )
    {
        HRESULT result = CoLockObjectExternal( mDropTarget, TRUE, FALSE );
        if ( S_OK == result )
        {
            result = RegisterDragDrop( hWnd, mDropTarget );
            if ( S_OK != result )
            {
                // RegisterDragDrop failed
                return false;
            };

            // all ok
            mDropWindowHandle = hWnd;
        }
        else
        {
            // Unable to lock OLE object
            return false;
        };
    };

    // success
    return true;
}

////////////////////////////////////////////////////////////////////////////////
//
void LLDragDropWin32::reset()
{
    if ( mDropTarget )
    {
        RevokeDragDrop( mDropWindowHandle );
        CoLockObjectExternal( mDropTarget, FALSE, TRUE );
        mDropTarget->Release();
    };

    OleUninitialize();
}

#endif // LL_OS_DRAGDROP_ENABLED

#endif // LL_WINDOWS

