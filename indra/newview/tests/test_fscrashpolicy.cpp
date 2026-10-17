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
    assert(!(off==on));auto newEpoch=on;newEpoch.epoch=std::string(32,'c');assert(!(on==newEpoch));
    assert(validRole("host") && validRole("worker") && !validRole("Alice"));
    const auto info=environmentText("7.2.5.1","windows","avx2","worker","abc");
    assert(info.find("worker")!=std::string::npos && info.find("account")==std::string::npos);
}
