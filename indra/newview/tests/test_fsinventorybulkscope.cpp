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

/** Interleaved fibers exercise the production AIS provenance scope. */
#include "../fsinventorybulkscope.h"
#include <boost/fiber/all.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main()
{
    try
    {
        FSInventoryBulk::FiberLocalTag<std::string> tags;
        using Scope = FSInventoryBulk::FiberLocalTag<std::string>::Scope;
        bool a_started=false, b_started=false, exit_a=false, exit_b=false, a_done=false;
        std::exception_ptr failure;
        boost::fibers::fiber a([&]()
        {
            try
            {
                {
                    Scope outer(tags,"A"); a_started=true;
                    while (!exit_a) boost::this_fiber::yield();
                    expect(tags.current() && *tags.current()=="A","fiber B/native activity cannot replace fiber A's provenance");
                    {
                        Scope nested(tags,"A-nested"); boost::this_fiber::yield();
                        expect(tags.current() && *tags.current()=="A-nested","nested provenance survives a yield");
                    }
                    expect(tags.current() && *tags.current()=="A","nested scope restores the same fiber's outer tag");
                }
                expect(!tags.current(),"outer scope clears only its own fiber");
            }
            catch (...) { failure=std::current_exception(); }
            a_done=true;
        });
        boost::fibers::fiber b([&]()
        {
            try
            {
                while (!a_started) boost::this_fiber::yield();
                expect(!tags.current(),"new AIS fiber does not inherit another suspended operation's tag");
                {
                    Scope scope(tags,"B"); b_started=true;
                    while (!exit_b) boost::this_fiber::yield();
                    expect(tags.current() && *tags.current()=="B","out-of-order fiber A scope destruction cannot clear fiber B's tag");
                }
                expect(!tags.current(),"fiber B cannot restore another coroutine's obsolete tag");
            }
            catch (...) { failure=std::current_exception(); b_started=true; }
        });
        while (!b_started) boost::this_fiber::yield();
        expect(!tags.current(),"native main-fiber edits remain external while AIS coroutines are suspended");
        { Scope native(tags,"native"); expect(tags.current() && *tags.current()=="native","main-fiber nested tag is isolated"); boost::this_fiber::yield(); }
        expect(!tags.current(),"main-fiber scope restoration does not restore an AIS tag");
        exit_a=true;
        while (!a_done) boost::this_fiber::yield();
        expect(!tags.current(),"overlapping scope completion cannot contaminate native provenance");
        exit_b=true; a.join(); b.join();
        if (failure) std::rethrow_exception(failure);
        expect(!tags.current(),"both AIS operations finish with native provenance empty");
    }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
    std::cout<<"Bulk inventory fiber provenance checks passed\n";
}
