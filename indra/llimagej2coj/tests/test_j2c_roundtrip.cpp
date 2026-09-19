/**
 * Encode→decode through OpenJPEG using the viewer's pack/unpack helpers.
 * 1-channel (sculpt) and 4-channel (alpha), including a 1024-wide RGBA strip.
 */
#include "llimagej2cpack.h"
#include "openjpeg.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

static opj_image_t* image_from_interleaved(const std::uint8_t* src, int channels, unsigned w, unsigned h)
{
    opj_image_cmptparm_t cmpt[4];
    std::memset(cmpt, 0, sizeof(cmpt));
    for (int c = 0; c < channels; ++c)
    {
        cmpt[c].prec = 8;
        cmpt[c].sgnd = 0;
        cmpt[c].dx = 1;
        cmpt[c].dy = 1;
        cmpt[c].w = w;
        cmpt[c].h = h;
    }
    OPJ_COLOR_SPACE cs = (channels >= 3) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;
    opj_image_t* image = opj_image_create((OPJ_UINT32)channels, cmpt, cs);
    if (!image)
    {
        return nullptr;
    }
    image->x1 = w;
    image->y1 = h;
    OPJ_INT32* planes[4] = {};
    for (int c = 0; c < channels; ++c)
    {
        planes[c] = image->comps[c].data;
    }
    llimagej2cpack::unpack_interleaved_u8_to_planar_int32(planes, src, channels, w, h);
    return image;
}

static bool encode_reversible(opj_image_t* image, const char* path)
{
    opj_cparameters_t cpar;
    opj_set_default_encoder_parameters(&cpar);
    cpar.tcp_numlayers = 1;
    cpar.tcp_rates[0] = 0.0f;
    cpar.cp_disto_alloc = 1;
    cpar.irreversible = 0;
    cpar.cod_format = 0;
    cpar.tcp_mct = (image->numcomps == 3) ? 1 : 0;
    unsigned min_dim = image->comps[0].w < image->comps[0].h ? image->comps[0].w : image->comps[0].h;
    int max_res = 1;
    while ((1u << (max_res - 1)) < min_dim && max_res < 6)
    {
        ++max_res;
    }
    cpar.numresolution = max_res;

    opj_codec_t* enc = opj_create_compress(OPJ_CODEC_J2K);
    if (!enc)
    {
        std::fprintf(stderr, "opj_create_compress failed\n");
        return false;
    }
    if (!opj_setup_encoder(enc, &cpar, image))
    {
        std::fprintf(stderr, "opj_setup_encoder failed numresolution=%d %ux%u c=%u\n",
                     cpar.numresolution, image->comps[0].w, image->comps[0].h, image->numcomps);
        opj_destroy_codec(enc);
        return false;
    }
    opj_stream_t* ws = opj_stream_create_default_file_stream(path, OPJ_FALSE);
    bool ok = ws && opj_start_compress(enc, image, ws) && opj_encode(enc, ws) && opj_end_compress(enc, ws);
    if (!ok)
    {
        std::fprintf(stderr, "opj_encode failed for %s\n", path);
    }
    if (ws)
    {
        opj_stream_destroy(ws);
    }
    opj_destroy_codec(enc);
    return ok;
}

static opj_image_t* decode_file(const char* path)
{
    opj_dparameters_t dpar;
    opj_set_default_decoder_parameters(&dpar);
    opj_codec_t* dec = opj_create_decompress(OPJ_CODEC_J2K);
    opj_setup_decoder(dec, &dpar);
    opj_decoder_set_strict_mode(dec, OPJ_FALSE);
    opj_stream_t* rs = opj_stream_create_default_file_stream(path, OPJ_TRUE);
    opj_image_t* decoded = nullptr;
    bool ok = rs && opj_read_header(rs, dec, &decoded) && decoded && opj_decode(dec, rs, decoded);
    if (ok)
    {
        opj_end_decompress(dec, rs);
    }
    if (rs)
    {
        opj_stream_destroy(rs);
    }
    opj_destroy_codec(dec);
    if (!ok)
    {
        if (decoded)
        {
            opj_image_destroy(decoded);
        }
        return nullptr;
    }
    return decoded;
}

static bool roundtrip(const std::uint8_t* src, int channels, unsigned w, unsigned h, const char* path, const char* label)
{
    opj_image_t* image = image_from_interleaved(src, channels, w, h);
    if (!image || !encode_reversible(image, path))
    {
        std::fprintf(stderr, "encode failed for %s (file %s)\n", label, path);
        expect(false, (std::string(label) + " encode").c_str());
        if (image)
        {
            opj_image_destroy(image);
        }
        return false;
    }
    opj_image_destroy(image);

    opj_image_t* decoded = decode_file(path);
    if (!decoded)
    {
        expect(false, (std::string(label) + " decode").c_str());
        return false;
    }

    const OPJ_INT32* planes[4] = {};
    for (int c = 0; c < channels; ++c)
    {
        planes[c] = decoded->comps[c].data;
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * h * static_cast<unsigned>(channels));
    llimagej2cpack::pack_planar_int32_to_interleaved_u8(
        out.data(), channels, planes, 0, channels, w, h, decoded->comps[0].w);

    bool match = std::memcmp(src, out.data(), out.size()) == 0;
    expect(match, (std::string(label) + " pixel match after Y-flip pack/unpack").c_str());
    opj_image_destroy(decoded);
    return match;
}

int main()
{
    const unsigned gw = 32;
    const unsigned gh = 16;
    std::vector<std::uint8_t> gray(gw * gh);
    for (std::size_t i = 0; i < gray.size(); ++i)
    {
        gray[i] = static_cast<std::uint8_t>(i * 9u);
    }
    roundtrip(gray.data(), 1, gw, gh, "/tmp/pack-gray.j2k", "1-channel");

    const unsigned rw = 1024;
    const unsigned rh = 8;
    std::vector<std::uint8_t> rgba(rw * rh * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i)
    {
        rgba[i] = static_cast<std::uint8_t>(i * 13u);
    }
    roundtrip(rgba.data(), 4, rw, rh, "/tmp/pack-rgba1024.j2k", "1024 RGBA strip");

    if (g_failures)
    {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("pack+openjpeg roundtrip: ok");
    return 0;
}
