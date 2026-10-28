# Pinned, decoder-only dependencies; no configure-time downloads or DLLs.
add_library(fs_chat_webp STATIC
  libwebp/src/dec/alpha_dec.c libwebp/src/dec/buffer_dec.c
  libwebp/src/dec/frame_dec.c libwebp/src/dec/idec_dec.c
  libwebp/src/dec/io_dec.c libwebp/src/dec/quant_dec.c
  libwebp/src/dec/tree_dec.c libwebp/src/dec/vp8_dec.c
  libwebp/src/dec/vp8l_dec.c libwebp/src/dec/webp_dec.c
  libwebp/src/dsp/alpha_processing.c libwebp/src/dsp/cpu.c
  libwebp/src/dsp/dec.c libwebp/src/dsp/dec_clip_tables.c
  libwebp/src/dsp/filters.c libwebp/src/dsp/lossless.c
  libwebp/src/dsp/rescaler.c libwebp/src/dsp/upsampling.c libwebp/src/dsp/yuv.c
  libwebp/src/utils/bit_reader_utils.c libwebp/src/utils/color_cache_utils.c
  libwebp/src/utils/filters_utils.c libwebp/src/utils/huffman_utils.c
  libwebp/src/utils/palette.c libwebp/src/utils/quant_levels_dec_utils.c
  libwebp/src/utils/rescaler_utils.c libwebp/src/utils/random_utils.c
  libwebp/src/utils/thread_utils.c libwebp/src/utils/utils.c
  libwebp/src/demux/anim_decode.c libwebp/src/demux/demux.c)
target_include_directories(fs_chat_webp PUBLIC libwebp)
target_compile_definitions(fs_chat_webp PRIVATE HAVE_CONFIG_H=1)
set_target_properties(fs_chat_webp PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Upstream C sources retain their own warning policy, including under /WX.
if(MSVC)
  target_compile_options(fs_chat_webp PRIVATE /W0 /WX-)
else()
  target_compile_options(fs_chat_webp PRIVATE -w)
endif()

add_library(fs_chat_media_codec STATIC ../../fschatmediacodec.cpp)
target_include_directories(fs_chat_media_codec PUBLIC ../..)
target_link_libraries(fs_chat_media_codec PRIVATE fs_chat_webp)
set_target_properties(fs_chat_media_codec PROPERTIES POSITION_INDEPENDENT_CODE ON)
