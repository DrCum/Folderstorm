/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../controller_model.h"
#include <cassert>
#include <iostream>
using namespace fs_controller;
int main()
{
    Config config;unsigned int serial=1;
    for(auto& p:config.profiles){p.id=std::string(31,'a')+static_cast<char>('0'+serial++);}
    assert(!config.enabled&&config.level(Op::List)==Level::Deny&&config.level(Op::Restart)==Level::Ask);
    Config saved;assert(Config::decode(config.encode(),saved)&&saved.encode()==config.encode());
    auto corrupt=config.encode();corrupt.back()=2;assert(!Config::decode(corrupt,saved));
    config.enabled=true;config.profiles[0].enabled=true;
    Source source{config.profiles[0].id,std::string(32,'b'),"opaque-login","account","grid",1,9,true};
    const std::uint64_t now=1800000000000ULL;
    Request q;q.instance=std::string(32,'c');q.token=std::string(64,'d');q.id=std::to_string(now)+"-"+std::string(32,'e');q.op=Op::Restart;
    q.profile=source.profile;q.revision=source.revision;q.worker=source.worker;q.generation=source.generation;q.viewerSession=source.viewerSession;
    Request decoded;assert(Request::decode(q.encode(),decoded)&&decoded.encode()==q.encode());
    assert(!constantEqual(q.token,std::string(64,'e'))&&constantEqual(q.token,q.token));
    assert(!freshRequest(q.id,now+60001)&&!freshRequest("old-id",now));
    assert(!Request::decode(q.encode()+"unexpected",decoded));
    Operations operations;std::string error;
    auto* r=operations.admit(q,{source},config.revision,now,error);assert(r&&r->request.token.empty());
    r->state="queued";const auto lookup=[&](const std::string&){return source;};
    assert(operations.allowed(*r,config,now,lookup));
    assert(operations.admit(q,{source},config.revision,now+1,error)==r); // duplicate does not execute
    auto changed=q;changed.viewerSession="other-login";assert(!operations.admit(changed,{source},config.revision,now,error)&&error=="request_id_conflict");
    auto replacement=source;replacement.worker=std::string(32,'f');
    assert(!operations.allowed(*r,config,now,[&](const std::string&){return replacement;}));
    assert(r->state=="cancelled"&&!operations.allowed(*r,config,now,lookup)); // terminal after re-enable
    q.id=std::to_string(now)+"-"+std::string(32,'f');
    r=operations.admit(q,{source},config.revision,now,error);assert(r);r->submitted=true;r->state="waiting_native_close";
    ++config.revision;assert(!operations.allowed(*r,config,now,lookup)&&r->state=="uncertain");
    q.id=std::to_string(now)+"-"+std::string(32,'a');r=operations.admit(q,{source},config.revision,now,error);assert(r);
    auto exited=source;exited.running=false;
    assert(operations.allowed(*r,config,now,[&](const std::string&){return exited;},true));
    assert(!operations.allowed(*r,config,now,[&](const std::string&){return exited;},false));
    // Tombstones outlive timestamp admission. Forgetting a record cannot resend it.
    assert(operations.admit(q,{source},config.revision,now+700000,error)==r);
    operations.receipts.clear();
    assert(!operations.admit(q,{source},config.revision,now+700000,error)&&error=="request_expired");
    Operations bounded;
    const char* digits="0123456789abcdef";
    for(unsigned int i=0;i<ReceiptCapacity;++i)
    {
        auto nonce=std::string(32,'a');nonce[30]=digits[(i>>4)&15];nonce[31]=digits[i&15];q.id=std::to_string(now)+"-"+nonce;
        auto* record=bounded.admit(q,{source},config.revision,now,error);assert(record);record->state="completed";
    }
    q.id=std::to_string(now)+"-"+std::string(32,'b');assert(!bounded.admit(q,{source},config.revision,now,error)&&error=="receipt_capacity");
    q.id=std::to_string(now+700000)+"-"+std::string(32,'c');assert(bounded.admit(q,{source},config.revision,now+700000,error)&&bounded.receipts.size()==1);
    q.id=std::to_string(now)+"-"+std::string(32,'a');assert(!bounded.admit(q,{source},config.revision,now+700000,error)&&error=="request_expired");
    assert(!bounded.admit(q,{source},config.revision,now,error)&&error=="request_expired"); // wall-clock rollback cannot revive an evicted ID
    Operations clocks;q.id=std::to_string(now)+"-"+std::string(32,'d');
    auto* timed=clocks.admit(q,{source},config.revision,now,error,1000);assert(timed);
    assert(!clocks.allowed(*timed,config,71000,lookup)&&timed->error=="deadline_expired");
    q.id=std::to_string(now+700000)+"-"+std::string(32,'e');
    assert(clocks.admit(q,{source},config.revision,now+700000,error,1100)&&clocks.receipts.size()==2); // wall-clock jumps do not shorten receipt retention
    // Idle past retention, then roll UTC back near a completed request. A new
    // ID must not evict its tombstone while the old timestamp is fresh again.
    Operations idle;auto original=q;original.id=std::to_string(now)+"-"+std::string(32,'6');
    auto* completed=idle.admit(original,{source},config.revision,now,error,1000);assert(completed);completed->state="completed";
    auto rolledBack=original;rolledBack.id=std::to_string(now+1000)+"-"+std::string(32,'7');
    assert(!idle.admit(rolledBack,{source},config.revision,now+1000,error,702000)&&error=="request_expired");
    assert(idle.find(original.id)==completed);
    // A request fresh against elapsed monotonic time may now evict the old
    // receipt. Its original ID remains expired even while UTC stays rolled back.
    auto fresh=original;fresh.id=std::to_string(now+701001)+"-"+std::string(32,'8');
    assert(idle.admit(fresh,{source},config.revision,now+1000,error,702001)&&!idle.find(original.id));
    assert(!idle.admit(original,{source},config.revision,now+1000,error,702002)&&error=="request_expired");
    return 0;
}
