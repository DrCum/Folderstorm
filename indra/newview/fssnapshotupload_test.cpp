/**
 * Direct check of the snapshot upload cost gate. Not part of the viewer build.
 *
 * Allow plus 0 proceeds. Allow plus a positive cost asks. Never (deny) denies.
 * Thumbnail is always cost 0, so Allow proceeds.
 */

#include "fssnapshotupload.h"

#include <iostream>

namespace
{
int gFailures = 0;

void expect(bool ok, const char* label)
{
    if (ok)
    {
        std::cout << "ok " << label << "\n";
        return;
    }
    std::cout << "FAIL " << label << "\n";
    ++gFailures;
}
}

int main()
{
    using fs_snapshot::UploadAction;
    expect(fs_snapshot::decide("allow", 0) == UploadAction::Proceed, "Allow plus 0 proceeds");
    expect(fs_snapshot::decide("allow", 10) == UploadAction::Ask, "Allow plus a positive cost asks");
    expect(fs_snapshot::decide("deny", 0) == UploadAction::Deny, "Never denies");
    expect(fs_snapshot::decide("deny", 15) == UploadAction::Deny, "Never denies a positive cost");
    expect(fs_snapshot::decide("ask", 0) == UploadAction::Ask, "Ask still asks at cost 0");
    expect(fs_snapshot::quoted_cost("thumbnail", 10) == 0, "thumbnail is always cost 0");
    expect(fs_snapshot::decide("allow", fs_snapshot::quoted_cost("thumbnail", 10)) == UploadAction::Proceed,
           "Allow plus thumbnail cost 0 proceeds");
    expect(fs_snapshot::quoted_cost("texture", 10) == 10, "texture keeps the quoted cost");
    expect(fs_snapshot::clamp_edge(32) == 64 && fs_snapshot::clamp_edge(4096) == 2048, "edges clamp to 64-2048");
    std::string destination;
    expect(fs_snapshot::normalize_destination("", destination) && destination == "thumbnail", "empty destination is thumbnail");
    expect(fs_snapshot::normalize_destination("texture", destination) && destination == "texture", "texture destination");
    expect(!fs_snapshot::normalize_destination("photo", destination), "unknown destination is rejected");
    if (gFailures != 0)
    {
        std::cout << gFailures << " failed\n";
        return 1;
    }
    std::cout << "snapshot upload cost gate passed\n";
    return 0;
}
