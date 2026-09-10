/**
 * @file test_j2cpack.cpp
 * @brief Standalone checks for OpenJPEG pack/unpack helpers (no viewer deps).
 */

#include "llimagej2cpack.h"

#include <algorithm>
#include <cstdio>
#include <vector>

static int g_failures = 0;

static void expect(bool cond, const char* what)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

int main()
{
    // 2x2 RGB, bottom-up interleaved: row0 (bottom) = 1,2,3 / 4,5,6
    // row1 (top) = 7,8,9 / 10,11,12
    const unsigned w = 2;
    const unsigned h = 2;
    const std::uint8_t interleaved[] = {
        1, 2, 3, 4, 5, 6,
        7, 8, 9, 10, 11, 12
    };

    std::vector<std::int32_t> r(w * h), g(w * h), b(w * h);
    std::int32_t* planes[3] = { r.data(), g.data(), b.data() };
    llimagej2cpack::unpack_interleaved_u8_to_planar_int32(planes, interleaved, 3, w, h);

    // Planar row 0 is the top of the JPEG2000 image = interleaved last row
    expect(r[0] == 7 && g[0] == 8 && b[0] == 9, "unpack top-left");
    expect(r[1] == 10 && g[1] == 11 && b[1] == 12, "unpack top-right");
    expect(r[2] == 1 && g[2] == 2 && b[2] == 3, "unpack bottom-left");
    expect(r[3] == 4 && g[3] == 5 && b[3] == 6, "unpack bottom-right");

    std::vector<std::uint8_t> roundtrip(sizeof(interleaved));
    const std::int32_t* cplanes[3] = { r.data(), g.data(), b.data() };
    llimagej2cpack::pack_planar_int32_to_interleaved_u8(
        roundtrip.data(), 3, cplanes, 0, 3, w, h, w);
    expect(std::equal(std::begin(interleaved), std::end(interleaved), roundtrip.begin()),
           "rgb roundtrip");

    // Clamp
    std::int32_t hot[] = { -10, 300 };
    const std::int32_t* hotp = hot;
    std::uint8_t out[2] = { 99, 99 };
    llimagej2cpack::pack_planar_int32_to_interleaved_u8(&out[0], 1, &hotp, 0, 1, 2, 1, 2);
    expect(out[0] == 0 && out[1] == 255, "clamp to 0..255");

    // 1-channel sculpt-sized
    const std::uint8_t gray_in[] = { 1, 2, 3, 4 }; // 2x2 bottom-up
    std::vector<std::int32_t> gray(4);
    std::int32_t* gp = gray.data();
    llimagej2cpack::unpack_interleaved_u8_to_planar_int32(&gp, gray_in, 1, 2, 2);
    expect(gray[0] == 3 && gray[1] == 4 && gray[2] == 1 && gray[3] == 2, "gray unpack flip");

    // 4-channel RGBA Y-flip
    const std::uint8_t rgba_in[] = {
        1, 2, 3, 4, 5, 6, 7, 8,
        9, 10, 11, 12, 13, 14, 15, 16
    };
    std::vector<std::int32_t> pr(4), pg(4), pb(4), pa(4);
    std::int32_t* rgba_planes[4] = { pr.data(), pg.data(), pb.data(), pa.data() };
    llimagej2cpack::unpack_interleaved_u8_to_planar_int32(rgba_planes, rgba_in, 4, 2, 2);
    expect(pr[0] == 9 && pg[0] == 10 && pb[0] == 11 && pa[0] == 12, "rgba unpack top-left");
    expect(pr[2] == 1 && pg[2] == 2 && pb[2] == 3 && pa[2] == 4, "rgba unpack bottom-left");
    std::vector<std::uint8_t> rgba_out(sizeof(rgba_in));
    const std::int32_t* crgba[4] = { pr.data(), pg.data(), pb.data(), pa.data() };
    llimagej2cpack::pack_planar_int32_to_interleaved_u8(
        rgba_out.data(), 4, crgba, 0, 4, 2, 2, 2);
    expect(std::equal(std::begin(rgba_in), std::end(rgba_in), rgba_out.begin()),
           "rgba roundtrip");

    // first_plane offset (aux starts at channel 1 of 2)
    std::int32_t a[2] = { 10, 20 };
    std::int32_t z[2] = { 1, 2 };
    const std::int32_t* two[2] = { a, z };
    std::uint8_t aux[2] = { 0, 0 };
    llimagej2cpack::pack_planar_int32_to_interleaved_u8(aux, 1, two, 1, 1, 2, 1, 2);
    expect(aux[0] == 1 && aux[1] == 2, "first_plane selects channel 1");

    // 1024x8 RGBA pack/unpack (row-wise path used by viewer encodes)
    const unsigned big_w = 1024;
    const unsigned big_h = 8;
    std::vector<std::uint8_t> big_in(big_w * big_h * 4);
    for (std::size_t i = 0; i < big_in.size(); ++i)
    {
        big_in[i] = static_cast<std::uint8_t>(i * 13u);
    }
    std::vector<std::int32_t> br(big_w * big_h), bg(big_w * big_h), bb(big_w * big_h), ba(big_w * big_h);
    std::int32_t* big_planes[4] = { br.data(), bg.data(), bb.data(), ba.data() };
    llimagej2cpack::unpack_interleaved_u8_to_planar_int32(big_planes, big_in.data(), 4, big_w, big_h);
    std::vector<std::uint8_t> big_out(big_in.size());
    const std::int32_t* cbig[4] = { br.data(), bg.data(), bb.data(), ba.data() };
    llimagej2cpack::pack_planar_int32_to_interleaved_u8(
        big_out.data(), 4, cbig, 0, 4, big_w, big_h, big_w);
    expect(big_in == big_out, "1024-wide rgba roundtrip");

    if (g_failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("test_j2cpack: ok");
    return 0;
}
