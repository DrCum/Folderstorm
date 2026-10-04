/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

/** Fiber-local tagging of synchronous AIS model updates that may yield. */
#ifndef FS_INVENTORY_BULK_SCOPE_H
#define FS_INVENTORY_BULK_SCOPE_H

#include <boost/fiber/fss.hpp>
#include <memory>

namespace FSInventoryBulk
{
template <typename Tag>
class FiberLocalTag
{
public:
    // Each fiber owns its current tag. Another fiber may run and even destroy
    // its scope while this one is suspended without changing this fiber's tag.
    class Scope
    {
    public:
        Scope(FiberLocalTag& owner, const Tag& tag) : mOwner(owner)
        {
            auto next = std::make_unique<Tag>(tag);
            mPrevious.reset(mOwner.mCurrent.release());
            mOwner.mCurrent.reset(next.release());
        }
        ~Scope() { mOwner.mCurrent.reset(mPrevious.release()); }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        FiberLocalTag& mOwner;
        std::unique_ptr<Tag> mPrevious;
    };
    const Tag* current() const { return mCurrent.get(); }
private:
    boost::fibers::fiber_specific_ptr<Tag> mCurrent;
};
}
#endif
