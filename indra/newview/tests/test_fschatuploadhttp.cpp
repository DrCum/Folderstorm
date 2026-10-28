#include "fschatuploadpolicy.h"
#include <cassert>
using namespace fs_chat_upload;
int main()
{
    Config config; Progress cancelled; cancelled.cancelled = true;
    assert(upload(config, {1,2,3}, "image/png", std::string(32, 'a'), "", cancelled).state == State::Failed);
    Progress status;
    config.folder.clear();
    assert(upload(config, {1,2,3}, "image/png", std::string(32, 'a'), "", status).state == State::Failed);
    config = Config{};
    assert(upload(config, {}, "image/png", std::string(32, 'a'), "", status).state == State::Failed);
    assert(upload(config, {1,2,3}, "text/html", std::string(32, 'a'), "", status).state == State::Failed);
    assert(!status.submitted && status.percent == 0);
}
