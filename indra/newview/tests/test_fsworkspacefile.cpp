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
#include "../fsworkspacefile.h"
#include <iostream>
int main()
{
    using namespace FSWorkspaceFile;
    int failures = 0;
    auto expect = [&](bool value, const char* label) { if (!value) { std::cerr << label << "\n"; ++failures; } };
    expect(boundedXML("<?xml version=\"1.0\"?><llsd><map><key>name</key><string>A &amp; B</string><array/></map></llsd>"), "Simple escaped LLSD accepted");
    expect(!boundedXML(""), "Empty documents refused");
    expect(!boundedXML(std::string(MAX_TRANSFER_BYTES + 1, ' ')), "Byte limit enforced before parsing");
    expect(!boundedXML("<!DOCTYPE llsd><llsd><map/></llsd>"), "DTD refused");
    expect(!boundedXML("<llsd><map><!ENTITY huge 'expanded'><string>&huge;</string></map></llsd>"), "Entity declarations refused");
    std::string deep;
    for (int i = 0; i < 25; ++i) deep += "<array>";
    for (int i = 0; i < 25; ++i) deep += "</array>";
    expect(!boundedXML(deep), "Nesting bound enforced");
    expect(!boundedXML("</llsd>"), "Unbalanced closings refused");
    std::string many = "<llsd><array>";
    for (int i = 0; i < 20000; ++i) many += "<undef/>";
    many += "</array></llsd>";
    expect(!boundedXML(many), "Node count bounded");
    if (!failures) std::cout << "Workspace import resource bounds passed\n";
    return failures ? 1 : 0;
}
