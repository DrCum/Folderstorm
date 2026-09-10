/* Smoke-test AVX2 OpenJPEG the way the viewer uses it: full decode,
 * discard levels, truncated prefixes, 1-channel (sculpt) and 4-channel
 * (alpha). Linked against the 3p AVX2 libopenjp2.
 */
#include "openjpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int sample_at(int c, int x, int y)
{
    if (c == 0)
    {
        return (x * 7 + y * 3) & 255;
    }
    if (c == 1)
    {
        return (x * 5) & 255;
    }
    if (c == 2)
    {
        return (y * 9) & 255;
    }
    return 200;
}

static opj_image_t* make_image(int w, int h, int ncomp)
{
    opj_image_cmptparm_t cmpt[4];
    memset(cmpt, 0, sizeof(cmpt));
    for (int c = 0; c < ncomp; ++c)
    {
        cmpt[c].prec = 8;
        cmpt[c].bpp = 8;
        cmpt[c].sgnd = 0;
        cmpt[c].dx = 1;
        cmpt[c].dy = 1;
        cmpt[c].w = (OPJ_UINT32)w;
        cmpt[c].h = (OPJ_UINT32)h;
    }
    OPJ_COLOR_SPACE cs = (ncomp >= 3) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY;
    opj_image_t* image = opj_image_create((OPJ_UINT32)ncomp, cmpt, cs);
    if (!image)
    {
        return NULL;
    }
    image->x1 = (OPJ_UINT32)w;
    image->y1 = (OPJ_UINT32)h;
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int i = y * w + x;
            for (int c = 0; c < ncomp; ++c)
            {
                image->comps[c].data[i] = sample_at(c, x, y);
            }
        }
    }
    return image;
}

static int encode_to_file(opj_image_t* image, const char* path, int irreversible)
{
    opj_cparameters_t cpar;
    opj_set_default_encoder_parameters(&cpar);
    cpar.tcp_numlayers = 1;
    cpar.tcp_rates[0] = irreversible ? 8.0f : 0.0f;
    cpar.cp_disto_alloc = 1;
    cpar.irreversible = irreversible ? 1 : 0;
    cpar.cod_format = 0;
    /* MCT is RGB-only; 1-channel and RGBA stay untransformed (viewer encode path). */
    cpar.tcp_mct = (image->numcomps == 3) ? 1 : 0;

    opj_codec_t* enc = opj_create_compress(OPJ_CODEC_J2K);
    if (!enc || !opj_setup_encoder(enc, &cpar, image))
    {
        fprintf(stderr, "setup encoder failed (%s)\n", path);
        return 0;
    }
    opj_stream_t* ws = opj_stream_create_default_file_stream(path, OPJ_FALSE);
    int ok = ws && opj_start_compress(enc, image, ws) && opj_encode(enc, ws) && opj_end_compress(enc, ws);
    if (ws)
    {
        opj_stream_destroy(ws);
    }
    opj_destroy_codec(enc);
    if (!ok)
    {
        fprintf(stderr, "encode failed (%s)\n", path);
    }
    return ok;
}

static long file_size(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static int decode_file(const char* path, int discard, opj_image_t** out)
{
    opj_dparameters_t dpar;
    opj_set_default_decoder_parameters(&dpar);
    opj_codec_t* dec = opj_create_decompress(OPJ_CODEC_J2K);
    opj_setup_decoder(dec, &dpar);
    if (discard > 0)
    {
        opj_set_decoded_resolution_factor(dec, (OPJ_UINT32)discard);
    }
    opj_decoder_set_strict_mode(dec, OPJ_FALSE);
    opj_stream_t* rs = opj_stream_create_default_file_stream(path, OPJ_TRUE);
    opj_image_t* decoded = NULL;
    int ok = rs && opj_read_header(rs, dec, &decoded) && decoded && opj_decode(dec, rs, decoded);
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
        *out = NULL;
        return 0;
    }
    *out = decoded;
    return 1;
}

static int write_prefix(const char* src_path, const char* dst_path, size_t nbytes)
{
    FILE* src = fopen(src_path, "rb");
    FILE* dst = fopen(dst_path, "wb");
    if (!src || !dst)
    {
        if (src)
        {
            fclose(src);
        }
        if (dst)
        {
            fclose(dst);
        }
        return 0;
    }
    char* buf = (char*)malloc(nbytes);
    if (!buf)
    {
        fclose(src);
        fclose(dst);
        return 0;
    }
    size_t n = fread(buf, 1, nbytes, src);
    fwrite(buf, 1, n, dst);
    free(buf);
    fclose(src);
    fclose(dst);
    return n > 0;
}

static int expect_dims(opj_image_t* img, unsigned w, unsigned h, const char* what)
{
    if (!img || img->comps[0].w != w || img->comps[0].h != h)
    {
        fprintf(stderr, "FAIL %s: got %ux%u expected %ux%u\n",
                what,
                img ? img->comps[0].w : 0,
                img ? img->comps[0].h : 0,
                w, h);
        return 0;
    }
    printf("ok %s %ux%u comps=%u\n", what, w, h, img->numcomps);
    return 1;
}

static int lossless_matches_pattern(opj_image_t* decoded, int channels, const char* what)
{
    if (!decoded || (int)decoded->numcomps != channels)
    {
        fprintf(stderr, "FAIL %s: component count %u\n", what, decoded ? decoded->numcomps : 0);
        return 0;
    }
    OPJ_UINT32 w = decoded->comps[0].w;
    OPJ_UINT32 h = decoded->comps[0].h;
    for (int c = 0; c < channels; ++c)
    {
        if (!decoded->comps[c].data)
        {
            fprintf(stderr, "FAIL %s: null plane %d\n", what, c);
            return 0;
        }
        for (OPJ_UINT32 y = 0; y < h; ++y)
        {
            for (OPJ_UINT32 x = 0; x < w; ++x)
            {
                int got = decoded->comps[c].data[y * decoded->comps[c].w + x];
                int want = sample_at(c, (int)x, (int)y);
                if (got != want)
                {
                    fprintf(stderr, "FAIL %s: c=%d x=%u y=%u got=%d want=%d\n",
                            what, c, x, y, got, want);
                    return 0;
                }
            }
        }
    }
    printf("ok %s lossless roundtrip %ux%u\n", what, w, h);
    return 1;
}

int main(void)
{
    int failures = 0;
    const char* rgb_path = "/tmp/openjpeg-avx2-rgb.j2k";
    const char* gray_path = "/tmp/openjpeg-avx2-gray.j2k";
    const char* rgba_path = "/tmp/openjpeg-avx2-rgba.j2k";
    opj_image_t* decoded = NULL;

    opj_image_t* gray = make_image(32, 32, 1);
    if (!gray || !encode_to_file(gray, gray_path, 0))
    {
        return 1;
    }
    opj_image_destroy(gray);
    if (!decode_file(gray_path, 0, &decoded) || !lossless_matches_pattern(decoded, 1, "1-channel sculpt"))
    {
        ++failures;
    }
    if (decoded)
    {
        opj_image_destroy(decoded);
        decoded = NULL;
    }

    opj_image_t* rgba = make_image(64, 64, 4);
    if (!rgba || !encode_to_file(rgba, rgba_path, 0))
    {
        return 1;
    }
    opj_image_destroy(rgba);
    if (!decode_file(rgba_path, 0, &decoded) || !lossless_matches_pattern(decoded, 4, "4-channel alpha"))
    {
        ++failures;
    }
    if (decoded)
    {
        opj_image_destroy(decoded);
        decoded = NULL;
    }

    opj_image_t* rgb = make_image(256, 256, 3);
    if (!rgb || !encode_to_file(rgb, rgb_path, 1))
    {
        return 1;
    }
    opj_image_destroy(rgb);
    printf("encoded RGB %ld bytes\n", file_size(rgb_path));

    if (!decode_file(rgb_path, 0, &decoded) || !expect_dims(decoded, 256, 256, "full RGB"))
    {
        ++failures;
    }
    if (decoded)
    {
        opj_image_destroy(decoded);
        decoded = NULL;
    }

    for (int d = 0; d <= 5; ++d)
    {
        char label[32];
        snprintf(label, sizeof(label), "discard-%d RGB", d);
        unsigned expect = 256u >> d;
        if (expect == 0)
        {
            expect = 1;
        }
        if (!decode_file(rgb_path, d, &decoded))
        {
            fprintf(stderr, "FAIL %s: decode\n", label);
            ++failures;
        }
        else if (d <= 4 && !expect_dims(decoded, expect, expect, label))
        {
            ++failures;
        }
        else if (d > 4)
        {
            printf("ok %s decoded %ux%u\n", label, decoded->comps[0].w, decoded->comps[0].h);
        }
        if (decoded)
        {
            opj_image_destroy(decoded);
            decoded = NULL;
        }
    }

    const size_t prefixes[] = { 600, 2048, 8192 };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
    {
        char trunc_path[64];
        snprintf(trunc_path, sizeof(trunc_path), "/tmp/openjpeg-avx2-trunc-%zu.j2k", prefixes[i]);
        if (!write_prefix(rgb_path, trunc_path, prefixes[i]))
        {
            fprintf(stderr, "FAIL write prefix %zu\n", prefixes[i]);
            ++failures;
            continue;
        }
        /* Truncated J2C is the SL first-packet path. Decode in a child so a
         * codec abort cannot poison later tests. Not crashing is the bar. */
        pid_t pid = fork();
        if (pid == 0)
        {
            opj_image_t* trunc_img = NULL;
            int ok = decode_file(trunc_path, 0, &trunc_img);
            if (trunc_img)
            {
                opj_image_destroy(trunc_img);
            }
            _exit(ok ? 0 : 2);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status))
        {
            fprintf(stderr, "FAIL truncated %zu-byte decode crashed (signal %d)\n",
                    prefixes[i], WTERMSIG(status));
            ++failures;
        }
        else
        {
            printf("truncated %zu-byte RGB decode: %s\n",
                   prefixes[i], WEXITSTATUS(status) == 0 ? "ok" : "failed (allowed)");
        }
    }

    opj_image_t* rgba256 = make_image(256, 256, 4);
    const char* rgba256_path = "/tmp/openjpeg-avx2-rgba256.j2k";
    if (!rgba256 || !encode_to_file(rgba256, rgba256_path, 1))
    {
        fprintf(stderr, "FAIL 256 RGBA encode\n");
        ++failures;
    }
    else if (!decode_file(rgba256_path, 0, &decoded) ||
             !expect_dims(decoded, 256, 256, "256 RGBA"))
    {
        ++failures;
    }
    if (rgba256)
    {
        opj_image_destroy(rgba256);
    }
    if (decoded)
    {
        opj_image_destroy(decoded);
    }

    if (failures)
    {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("openjpeg avx2 smoke: ok");
    return 0;
}
