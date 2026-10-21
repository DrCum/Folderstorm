#include "fscrashpolicy.h"
#include <cassert>
int main()
{
    using namespace fs_crash;
    Consent off{false,std::string(32,'a')},on{true,std::string(32,'b')};
    assert(Consent::decode(off.encode())==off && Consent::decode(on.encode())==on);
    assert(!Consent::decode("1\n1\n").enabled);
    assert(!Consent::decode(on.encode()+"extra").enabled);
    auto invalid=on.encode();invalid[5]='z';assert(!Consent::decode(invalid).enabled);
    // Exercise the actual prompt orchestration: each persistent decision is
    // shared by subsequent host/viewer launches, even after declining.
    Consent saved;
    int asks=0,writes=0;
    std::optional<bool> answer=true;
    const auto read=[&](){return saved;};
    const auto ask=[&](){++asks;return answer;};
    const auto save=[&](bool enabled){++writes;saved={enabled,std::string(32,'d')};return true;};
    assert(offerConsentOnce(false,"viewer",read,ask,save));
    assert(offerConsentOnce(true,"worker",read,ask,save));
    assert(offerConsentOnce(true,"unknown",read,ask,save));
    assert(asks==0 && writes==0);
    assert(offerConsentOnce(true,"viewer",read,ask,save));
    assert(saved.enabled && asks==1 && writes==1);
    assert(offerConsentOnce(true,"host",read,ask,save));
    assert(asks==1 && writes==1);
    saved={};answer=false;
    assert(offerConsentOnce(true,"host",read,ask,save));
    assert(!saved.enabled && asks==2 && writes==2);
    saved=Consent::decode(saved.encode());
    assert(offerConsentOnce(true,"viewer",read,ask,save));
    assert(asks==2 && writes==2); // Upgrades keep the explicit Not now choice.
    saved=on;
    assert(offerConsentOnce(true,"viewer",read,ask,save));
    saved=off;
    assert(offerConsentOnce(true,"host",read,ask,save));
    assert(asks==2 && writes==2); // Preserve choices made before this feature.
    saved={};answer.reset();
    assert(offerConsentOnce(true,"viewer",read,ask,save));
    assert(saved.epoch.empty() && asks==3 && writes==2); // Dialog creation failed.
    answer=true;
    const auto concurrentDecline=[&](){++asks;saved=off;return answer;};
    assert(offerConsentOnce(true,"viewer",read,concurrentDecline,save));
    assert(saved==off && asks==4 && writes==2);
    saved={};answer=false;
    const auto concurrentEnable=[&](){++asks;saved=on;return answer;};
    assert(offerConsentOnce(true,"host",read,concurrentEnable,save));
    assert(saved==on && asks==5 && writes==2);
    saved={};
    const auto failedSave=[&](bool){++writes;return false;};
    assert(!offerConsentOnce(true,"viewer",read,ask,failedSave));
    assert(saved.epoch.empty() && asks==6 && writes==3);
    assert(offerConsentOnce(true,"host",read,ask,save));
    assert(!saved.enabled && asks==7 && writes==4); // Retry an unsaved choice.
    assert(!(off==on));auto newEpoch=on;newEpoch.epoch=std::string(32,'c');assert(!(on==newEpoch));
    assert(validRole("host") && validRole("worker") && !validRole("Alice"));
    const auto info=environmentText("7.2.5.1","windows","avx2","worker","abc");
    assert(info.find("worker")!=std::string::npos && info.find("account")==std::string::npos);
}
