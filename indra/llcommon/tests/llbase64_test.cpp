/**
 * @file llbase64_test.cpp
 * @author James Cook
 * @date 2007-02-04
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include <string>
#include <sstream>

#include "linden_common.h"

#include "../llbase64.h"
#include "../llsdserialize.h"
#include "../lluuid.h"

#include "../test/lltut.h"

namespace tut
{
    struct base64_data
    {
        LLSD roundTripMessage(const LLSD& message)
        {
            std::ostringstream output;
            LLSDSerialize::toBinary(message, output);
            const std::string wire = output.str();
            const std::string encoded = LLBase64::encode(reinterpret_cast<const U8*>(wire.data()), wire.size());
            const std::string decoded = LLBase64::decodeAsString(encoded);
            ensure_equals("preserve binary LLSD bytes", decoded, wire);

            std::istringstream input(decoded);
            LLSD result;
            ensure("parse decoded binary LLSD",
                   LLSDSerialize::fromBinary(result, input, static_cast<llssize>(decoded.size()), 12) != LLSDParser::PARSE_FAILURE);
            return result;
        }
    };
    typedef test_group<base64_data> base64_test;
    typedef base64_test::object base64_object;
    tut::base64_test base64("LLBase64");

    template<> template<>
    void base64_object::test<1>()
    {
        std::string result;

        result = LLBase64::encode(NULL, 0);
        ensure("encode nothing", (result == "") );

        LLUUID nothing;
        result = LLBase64::encode(&nothing.mData[0], UUID_BYTES);
        ensure("encode blank uuid",
                (result == "AAAAAAAAAAAAAAAAAAAAAA==") );

        LLUUID id("526a1e07-a19d-baed-84c4-ff08a488d15e");
        result = LLBase64::encode(&id.mData[0], UUID_BYTES);
        ensure("encode random uuid",
                (result == "UmoeB6Gduu2ExP8IpIjRXg==") );

    }

    template<> template<>
    void base64_object::test<2>()
    {
        std::string result;

        U8 blob[40] = { 115, 223, 172, 255, 140, 70, 49, 125, 236, 155, 45, 199, 101, 17, 164, 131, 230, 19, 80, 64, 112, 53, 135, 98, 237, 12, 26, 72, 126, 14, 145, 143, 118, 196, 11, 177, 132, 169, 195, 134 };
        result = LLBase64::encode(&blob[0], 40);
        ensure("encode 40 bytes",
                (result == "c9+s/4xGMX3smy3HZRGkg+YTUEBwNYdi7QwaSH4OkY92xAuxhKnDhg==") );
    }

    template<> template<>
    void base64_object::test<3>()
    {
        ensure_equals("decode nothing", LLBase64::decodeAsString(""), std::string());
        ensure_equals("decode text", LLBase64::decodeAsString("aGVsbG8="), std::string("hello"));

        const U8 bytes[] = {0, 1, 0, 128, 255, 0};
        const std::string expected(reinterpret_cast<const char*>(bytes), sizeof(bytes));
        ensure_equals("decode leading, embedded and trailing zero bytes", LLBase64::decodeAsString("AAEAgP8A"), expected);
        ensure_equals("decode blank uuid", LLBase64::decodeAsString("AAAAAAAAAAAAAAAAAAAAAA=="), std::string(UUID_BYTES, '\0'));
    }

    template<> template<>
    void base64_object::test<4>()
    {
        LLSD discovery = LLSD::emptyMap();
        discovery["t"] = "capable";
        discovery["s"] = "";
        discovery["v"] = 1;
        const LLSD peer = roundTripMessage(discovery);
        ensure_equals("discovery type", peer["t"].asString(), std::string("capable"));
        ensure_equals("discovery version", peer["v"].asInteger(), 1);

        LLSD snapshot = LLSD::emptyMap();
        snapshot["t"] = "snapshot";
        snapshot["s"] = "11111111-2222-3333-4444-555555555555";
        snapshot["r"] = 0;
        snapshot["p"] = LLSD::Binary{0, 0, 128, 63, 0, 0, 0, 0, 255, 0};
        const LLSD pose = roundTripMessage(snapshot);
        ensure_equals("snapshot type", pose["t"].asString(), std::string("snapshot"));
        ensure_equals("snapshot session", pose["s"].asString(), snapshot["s"].asString());
        ensure_equals("snapshot revision", pose["r"].asInteger(), 0);
        ensure("snapshot binary type", pose["p"].isBinary());
        ensure("snapshot binary payload", pose["p"].asBinary() == snapshot["p"].asBinary());
    }

}
