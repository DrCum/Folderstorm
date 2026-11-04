"""Run the About report formatter and the live location-choice reset on Windows or POSIX."""
import unittest
import test_fsobjecttools_native as native


class AboutLocation(unittest.TestCase):
    def test_location_is_removed_before_localization_and_reset_on_context_change(self):
        app = (native.ROOT / "llappviewer.cpp").read_text()
        about = (native.ROOT / "llfloaterabout.cpp").read_text()
        fixture = r"""
#include <cassert>
#include <ctime>
#include <map>
#include <sstream>
#include <string>
#include <vector>
using S32=int;
#define STRINGIZE(expr) ([&]{std::ostringstream s;s<<expr;return s.str();}())
struct Value {
    std::string scalar;std::vector<Value> array;
    bool isArray()const{return !array.empty();}bool isUndefined()const{return false;}
    std::string asString()const{return scalar;}size_t size()const{return array.size();}
    const Value& operator[](int n)const{return array.at(n);}
    Value& operator=(int n){scalar=std::to_string(n);return *this;}
};
struct LLSD {
    using Integer=int;using Fields=std::map<std::string,Value>;using map_const_iterator=Fields::const_iterator;
    Fields map;
    auto beginMap()const{return map.begin();}auto endMap()const{return map.end();}
    bool has(std::string k)const{return map.count(k)!=0;}void erase(std::string k){map.erase(k);}
    Value& operator[](std::string k){return map[k];}
};
struct LLStringUtil {using format_map_t=std::map<std::string,std::string>;};
struct LLTrans {
    static std::string getString(const char* s,bool){return s;}
    static std::string getString(const char* s,const LLSD&,bool){return s;}
    static std::string getString(const char* s,const LLStringUtil::format_map_t& args,bool){
        // Simulate a translation using every available substitution.
        std::string out=s;for(const auto& kv:args)out+=" "+kv.second;return out;
    }
};
struct RlvActions {static inline bool allowed=true;static bool canShowLocation(){return allowed;}};
struct LLAppViewer {
    enum class ReportLocation{Include,Omit};LLSD snapshot;
    LLSD getViewerInfo()const{return snapshot;}
    std::string getViewerInfoString(bool,ReportLocation)const;
};
struct LLUUID {
    int id=0;static const LLUUID null;bool notNull()const{return id!=0;}
    bool operator==(LLUUID o)const{return id==o.id;}
};
const LLUUID LLUUID::null{};
struct Region{LLUUID id{2};LLUUID getRegionID()const{return id;}};
struct Agent{Region* region=nullptr;LLUUID session{1};Region* getRegion()const{return region;}
    LLUUID getSessionID()const{return session;}} gAgent;
bool gDisconnected=false;
struct Choice{bool value=false,enabled=false;void set(bool v){value=v;}void setEnabled(bool v){enabled=v;}};
struct LLFloaterAbout {
    LLUUID mReportSession,mReportRegion;bool mCanShowLocation=false,mRequestedNotes=false;
    Choice* mLocationChoice;int redraws=0;
    void syncLocationChoice(bool reopening=false);void setSupportText(){++redraws;}
};
"""
        cases = r"""
int main(){
    LLAppViewer app;
    for(const char* k:{"REGION","HOSTNAME","SLURL","SERVER_VERSION","SERVER_RELEASE_NOTES_URL"})
        app.snapshot[k].scalar=std::string("private-")+k;
    app.snapshot["POSITION"].array={Value{"private-global",{}}};
    app.snapshot["POSITION_LOCAL"].array={Value{"private-local",{}}};
    app.snapshot["CPU"].scalar="diagnostic-cpu";
    const auto public_report=app.getViewerInfoString(false,LLAppViewer::ReportLocation::Omit);
    assert(public_report.find("private-")==std::string::npos && public_report.find("diagnostic-cpu")!=std::string::npos);
    assert(public_report.find("AboutPosition")==std::string::npos);
    const auto full=app.getViewerInfoString(false,LLAppViewer::ReportLocation::Include);
    assert(full.find("private-SLURL")!=std::string::npos && full.find("private-global")!=std::string::npos);
    Choice choice;LLFloaterAbout view;view.mLocationChoice=&choice;Region a,b;b.id={3};
    view.syncLocationChoice(true);assert(!choice.enabled);
    gAgent.region=&a;view.syncLocationChoice();assert(choice.enabled && !choice.value);
    choice.value=true;const int draws=view.redraws;view.syncLocationChoice();assert(choice.value && view.redraws==draws);
    gAgent.region=&b;view.syncLocationChoice();assert(!choice.value);
    choice.value=true;RlvActions::allowed=false;view.syncLocationChoice();assert(!choice.value && !choice.enabled);
    RlvActions::allowed=true;view.syncLocationChoice();choice.value=true;gAgent.session={9};
    view.syncLocationChoice();assert(!choice.value);
    choice.value=true;view.syncLocationChoice(true);assert(!choice.value);
}
"""
        native.NativeObjectToolsTests.compile_and_run(self, fixture + native.function(
            app, "std::string LLAppViewer::getViewerInfoString(bool default_string, ReportLocation location) const") +
            native.function(about, "void LLFloaterAbout::syncLocationChoice(bool reopening)") + cases)


if __name__ == "__main__":
    unittest.main()
