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

/** Deterministic tests of production inventory plan/store/callback executor. */
#include "../fsinventorybulkmodel.h"
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace FSInventoryBulkModel;
namespace
{
void expect(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture
{
    std::shared_ptr<Store> store = std::make_shared<Store>();
    std::shared_ptr<History> entry;
    double now = 1;
    bool allowed = true, delayed_submission = false;
    int replies = 0, dispatches = 0;
    std::map<std::string, std::function<void(Outcome)>> callbacks;
    std::map<std::string, std::function<void()>> submit;
    std::map<std::string, std::string> errors;
    Fixture() { store->session("login"); }
    std::shared_ptr<Plan> prepare(const std::string& id, int count = 1, const std::string& prefix = "item")
    {
        auto p = std::make_shared<Plan>(); p->id = id; p->session = "login";
        for (int i=0;i<count;++i)
        {
            Row r; r.before.id=prefix+std::to_string(i); r.before.name="old";
            r.before.parent="parent"; r.before.ordinary=true; r.after_name="new";
            r.undo_eligible=true; p->rows.push_back(r);
        }
        std::string error; expect(store->prepare(p, now, error), error.c_str()); return p;
    }
    std::shared_ptr<Operation> execute(const std::string& plan_id, const std::string& op_id = "op")
    {
        std::string error; entry=store->claim(plan_id,op_id,now,error); expect(bool(entry),error.c_str());
        Backend b;
        b.now=[this]{return now;}; b.allowed=[this]{return allowed;};
        b.validate=[this](const Row& r){return errors[r.before.id];};
        b.perform=[this](const Row& r,bool,std::function<bool()>,std::function<void()> submitted,std::function<void(Outcome)> done)
        {
            ++dispatches; submit[r.before.id]=submitted; callbacks[r.before.id]=done;
            if (!delayed_submission) submitted();
        };
        return std::make_shared<Operation>(store,entry,std::move(b),10,[this](const History&){++replies;});
    }
    void confirmed(const std::string& id)
    {
        Outcome o; o.status="confirmed"; o.accepted=true;
        o.after.id=id; o.after.name="new"; o.after.parent="parent"; o.after.ordinary=true;
        callbacks.at(id)(o);
    }
};
void displayPrivacyBounds()
{
    expect(boundedLabel("a\nb\tc\x7f") == "a b c ", "display control characters cannot inject UI rows");
    const std::string unicode = std::string(252,'a') + "☃☃";
    const auto display = boundedLabel(unicode);
    expect(display.size()==255 && display.substr(252)=="...", "UTF8 shortening never splits a multibyte codepoint");
    expect(boundedLabel(std::string(2000,'a'),1024).size()==1024,"folder paths have separate bounded display limit");
    expect(unicode.size()==258,"display shortening leaves material snapshot unchanged");
}
void preparationAndConsumption()
{
    Fixture f; auto p=f.prepare("p",50);
    expect(p->expires==601,"plans have ten-minute TTL");
    std::string error;
    auto same=f.store->claim("p","op",1,error);
    expect(f.store->claim("p","different",1,error)==same,"consumption must return the original operation");
    expect(f.store->history(1).size()==1,"repeated claim creates no history");
    auto duplicate=std::make_shared<Plan>(*p); duplicate->id="duplicate"; duplicate->operation_id.clear(); duplicate->rows[1]=duplicate->rows[0];
    expect(!f.store->prepare(duplicate,1,error),"duplicate target scope is rejected");
    duplicate->rows.resize(51); expect(!f.store->prepare(duplicate,1,error),"51 requested targets are rejected");
    expect(!f.store->plan("p",601),"expiry does not restore consumed plan");
    f.store->session("other"); expect(f.store->history(1).empty(),"foreign login cannot read history");
}
void consumedPlansDoNotExhaustPreviewCapacity()
{
    Fixture f; std::string error;
    for (int i=0;i<140;++i)
    {
        const auto id=std::to_string(i); f.prepare(id,1,"serial"+id);
        auto record=f.store->claim(id,"op"+id,1,error); expect(bool(record),error.c_str());
        Outcome outcome; outcome.status="confirmed"; f.store->submitted(*record,0); f.store->settle(*record,0,outcome); f.store->finish(*record);
    }
    expect(f.store->history(1).size()<=100,"completed serial calls evict old history without blocking writes at32");
    expect(!f.store->plan("0",1),"eviction never grants a consumed plan a replay path");
    for (int i=0;i<32;++i) f.prepare("pending"+std::to_string(i),1,"pending"+std::to_string(i));
    auto extra=std::make_shared<Plan>();extra->id="extra";extra->session="login";extra->rows.push_back(Row());extra->rows[0].before.id="extra";
    expect(!f.store->prepare(extra,1,error),"32 genuinely pending previews still enforce the cap");
}
void callbackOrderingAndConcurrency()
{
    Fixture f; f.prepare("p",6); auto op=f.execute("p");
    expect(!op->tick() && f.dispatches==4,"at most four requests outstanding");
    f.confirmed("item3"); f.confirmed("item1"); f.confirmed("item1");
    expect(!op->tick() && f.dispatches==6,"completion opens request slots; duplicate callback opens none");
    for (const auto& id:{"item5","item4","item0","item2"}) f.confirmed(id);
    expect(op->tick() && f.replies==1,"one terminal response after unordered callbacks");
    expect(f.entry->results[0].after.id=="item0","results retain requested order");
    op->tick(); expect(f.replies==1,"terminal tick cannot repeat response");
    std::string error; expect(f.store->claim("p","other",1,error)==f.entry,"repeat execute cannot replay");
}
void preflightIsNotSubmission()
{
    Fixture f; f.delayed_submission=true; f.prepare("p",2); auto op=f.execute("p"); op->tick();
    expect(f.entry->results[0].status=="not_started","preflight GET does not claim a mutation submission");
    f.now=11; op->tick();
    expect(f.entry->results[0].status=="cancelled_before_submission","preflight timeout is safe before submission");
    f.prepare("retry",1); std::string error; expect(bool(f.store->claim("retry","retry",11,error)),"unsubmitted target reservation is released");
}
void uncertainLateAndClear()
{
    Fixture f; f.prepare("p",2); auto op=f.execute("p"); op->tick(); f.now=11; op->tick();
    expect(f.entry->results[0].status=="unconfirmed","submitted timeout remains uncertain");
    f.prepare("retry"); std::string error; expect(!f.store->claim("retry","retry",11,error),"uncertain target cannot be retried");
    f.confirmed("item0"); expect(f.entry->results[0].status=="confirmed","late callback can reconcile existing entry");
    expect(f.store->undoAvailable(*f.entry,0),"verified late completion may offer limited undo");
    f.store->clear(); f.confirmed("item1");
    expect(f.store->history(11).empty(),"late callback cannot resurrect cleared history");
    expect(!f.store->undoAvailable(*f.entry,1),"clear permanently removes inverse access");
}
void clearCannotBypassCapacity()
{
    Fixture f; std::vector<std::shared_ptr<Operation>> active;
    for (int i=0;i<4;++i)
    {
        auto id=std::to_string(i); f.prepare(id,1,"target"+id); active.push_back(f.execute(id,"op"+id)); active.back()->tick(); f.store->clear();
    }
    f.prepare("fifth",1,"fresh"); std::string error;
    expect(!f.store->claim("fifth","fifth",1,error),"Clear does not free active executor capacity");
    f.now=11; for (auto& op:active) op->tick();
    expect(bool(f.store->claim("fifth","fifth",11,error)),"terminal executors release capacity even after Clear");
}
void cancellationAndValidation()
{
    Fixture f; f.prepare("p",6); f.errors["item0"]="locked";
    auto op=f.execute("p"); op->tick(); expect(f.entry->results[0].status=="skipped","current lock skips row before submission");
    f.allowed=false; op->tick();
    expect(f.entry->results[5].status=="cancelled_before_submission","revocation stops remaining queued work");
    f.allowed=true; op->tick(); expect(f.dispatches==4 && f.replies==1,"restoring permission never restarts terminal executor");
    Fixture stopped; stopped.prepare("p",5); auto second=stopped.execute("p"); second->tick(); stopped.store->stop("op"); second->tick();
    expect(stopped.entry->cancelled && stopped.entry->results[4].status=="cancelled_before_submission","Stop leaves submitted work uncertain and cancels remaining rows");
}
void clearThenStop()
{
    Fixture f; f.prepare("p",5); auto op=f.execute("p"); op->tick();
    f.store->clear(); f.store->stop("op"); op->tick();
    expect(f.entry->cancelled && f.entry->results[4].status=="cancelled_before_submission","Clear does not detach Stop from a live operation");
    expect(f.store->history(1).empty(),"stopping cleared work does not recreate history");
}
void limitedUndo()
{
    Fixture f; auto prepared=f.prepare("p",2); prepared->rows[0].before.copyable=false;
    prepared->rows[1].undo_eligible=false;
    auto op=f.execute("p"); op->tick(); f.confirmed("item0"); f.confirmed("item1"); op->tick();
    expect(f.store->undoAvailable(*f.entry,0),"rename does not require copy permission");
    expect(!f.store->undoAvailable(*f.entry,1),"explicitly ineligible targets never gain undo");
    std::string error;
    expect(f.store->previewUndo("op",{},"inverse",1,error),"confirmed eligible subset can prepare inverse");
    auto inverse=f.store->plan("inverse",1);
    expect(inverse->rows.size()==1 && inverse->rows[0].before.name=="new" && inverse->rows[0].after_name=="old","inverse freezes expected after and original before state");
    expect(!f.store->previewUndo("op",{"missing"},"bad",1,error),"foreign subset rejected");
    auto inverse_entry=f.store->claim("inverse","undo",1,error); f.store->submitted(*inverse_entry,0);
    expect(!f.store->undoAvailable(*f.entry,0),"inverse submission supersedes original inverse even if uncertain");
    Outcome result; result.status="confirmed"; f.store->settle(*inverse_entry,0,result); f.store->finish(*inverse_entry);
    expect(!f.store->undoAvailable(*inverse_entry,0),"no recursive inverse/redo");
}
void interveningNativeWriteBeforeConfirmation()
{
    Fixture f; auto prepared=f.prepare("p"); prepared->rows[0].before.external_revision=10;
    auto op=f.execute("p"); op->tick();
    // The AIS update succeeded. While its confirming GET was pending, a
    // native rename changed new -> temporary -> new. Final fields still match,
    // but its observed writer epoch must not become a fresh inverse baseline.
    Outcome result; result.status="confirmed";result.accepted=true;
    result.after=prepared->rows[0].before;result.after.name="new";result.after.external_revision=12;
    f.callbacks.at("item0")(result);op->tick();
    expect(f.entry->results[0].status=="confirmed","remote confirmation remains truthful after intervening writes");
    expect(!f.store->undoAvailable(*f.entry,0),"native ABA while confirming must supersede the older inverse");
    std::string error;expect(!f.store->previewUndo("op",{},"undo",1,error),"pending native write cannot authorize inverse preparation");
    Fixture own;auto own_plan=own.prepare("p");own_plan->rows[0].before.external_revision=20;
    auto own_op=own.execute("p");own_op->tick();
    result.after=own_plan->rows[0].before;result.after.name="new";result.after.revision=100;
    own.callbacks.at("item0")(result);own_op->tick();
    expect(own.store->undoAvailable(*own.entry,0),"own AIS cache application may change material revision without counting as another writer");
}
void retentionAndSessionBoundaries()
{
    Fixture f;
    for(int i=0;i<33;++i)
    {
        if(i<32) f.prepare(std::to_string(i),1,"unique"+std::to_string(i));
        else { auto p=std::make_shared<Plan>(); p->id="overflow";p->session="login";p->rows.push_back(Row());p->rows[0].before.id="other"; std::string error; expect(!f.store->prepare(p,1,error),"prepared plan cap enforced"); }
    }
    Fixture expired; expired.prepare("p"); auto op=expired.execute("p");op->tick();expired.confirmed("item0");op->tick();
    expect(expired.store->history(7201).empty(),"completed history expires after two hours");
    Fixture logout;logout.prepare("p");auto old=logout.execute("p");old->tick();logout.store->session("other");logout.confirmed("item0");
    expect(logout.store->history(1).empty(),"old login callback cannot affect new session"); old->tick();
    Snapshot a,b;a.id=b.id="item";a.revision=1;b.revision=2;
    expect(!(a==b),"known intervening native write revision prevents ABA inverse");
}
}
int main()
{
    try { displayPrivacyBounds(); preparationAndConsumption(); consumedPlansDoNotExhaustPreviewCapacity(); callbackOrderingAndConcurrency();preflightIsNotSubmission();uncertainLateAndClear();clearCannotBypassCapacity();cancellationAndValidation();clearThenStop();limitedUndo();interveningNativeWriteBeforeConfirmation();retentionAndSessionBoundaries(); }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
    std::cout<<"Inventory bulk model checks passed\n";
}
