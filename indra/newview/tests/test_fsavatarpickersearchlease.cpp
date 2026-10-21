/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fsavatarpickersearchlease.h"
#include <cassert>
using Lease=fs_assistant_social::AvatarPickerSearchLease;
Lease::Ptr pickerSearchFromOtherTranslationUnit();
int main()
{
    auto mcpRequest=Lease::acquire();assert(mcpRequest);
    auto actualHttp=mcpRequest;
    assert(!Lease::acquire() && !pickerSearchFromOtherTranslationUnit());
    mcpRequest.reset(); // caller discards its result / permissions change
    assert(!Lease::acquire() && !pickerSearchFromOtherTranslationUnit());
    actualHttp.reset(); // only actual HTTP completion releases admission
    auto hostRequest=pickerSearchFromOtherTranslationUnit();assert(hostRequest);
    assert(!Lease::acquire());
    hostRequest.reset();
    auto nativePicker=Lease::acquire();assert(nativePicker);
    assert(!pickerSearchFromOtherTranslationUnit());
    nativePicker.reset();assert(pickerSearchFromOtherTranslationUnit());
}
