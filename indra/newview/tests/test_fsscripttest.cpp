/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsscripttestmodel.h"
#include "fsassistantfeatures.h"
#include <cassert>
int main()
{
    fs_script_test::Sequence run;run.start(3,10.,60.);assert(run.running() && run.index()==0);
    run.next(11.);assert(run.index()==1 && !run.stepExpired(12.,2.) && run.stepExpired(13.,2.));
    // Polling cannot consume an action or prevent cancellation.
    for(int i=0;i<200;++i)run.check(true,true,false,12.);
    run.stop();const auto stopped=run.index();run.next(12.);assert(run.state()=="stopped" && run.index()==stopped);
    run.start(2,20.,60.);run.check(true,true,true,21.);assert(run.state()=="inconclusive");run.next(22.);assert(run.index()==0);
    run.start(1,30.,60.);run.check(false,true,false,31.);assert(run.state()=="stopped");
    run.start(1,40.,10.);run.check(true,false,false,41.);assert(run.state()=="stopped");
    run.start(1,50.,10.);run.check(true,true,false,60.);assert(run.state()=="failed");
    run.start(2,70.,10.);run.next(71.);run.next(72.);assert(run.state()=="passed");run.stop();assert(run.state()=="passed");
    assert(fs_assistant::durableOperationSeconds("FSObjects","testBegin",600)==605.);
    assert(fs_assistant::durableOperationSeconds("FSObjects","testBegin",0)==70.);
    assert(fs_assistant::durableOperationSeconds("FSObjects","testBegin",601)==70.);
    assert(fs_assistant::durableOperationSeconds("FSObjects","touch",600)==70.);
    assert(fs_assistant::sourceBoundCancellation("FSObjects","testStop"));
    assert(!fs_assistant::sourceBoundCancellation("FSObjects","scriptRunning"));
    assert(!fs_assistant::sourceBoundCancellation("FSNavigation","testStop"));
    fs_script_test::ObservationWindow window;window.action(0);window.consume(1);
    // Action one emitted ready(1), success(2). Action two starts after both:
    // its assertion cannot consume action one's retained success.
    window.action(2);assert(window.cursor()==2 && window.baseline()==2);
    assert(!(2>window.cursor()) && 3>window.cursor());
    window.consume(3);assert(window.cursor()==3 && window.baseline()==2);
    // Consecutive positive/negative assertions share the same action window.
    assert(3>window.baseline());
}
