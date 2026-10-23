/* Isolated upstream C translation unit. The C++ adapter invokes header opening
   only, always with its bounded allocator; no audio decode API is called. */
#define STB_VORBIS_NO_PULLDATA_API
#define STB_VORBIS_MAX_CHANNELS 8
#define STB_VORBIS_DIVIDES_IN_CODEBOOK
#include "vendor/stb/stb_vorbis.c"
