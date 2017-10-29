#include "aes-stream.h"

#include <immintrin.h>
#include <stdlib.h>

#define ROUNDS 10

#define COMPILER_ASSERT(X) (void) sizeof(char[(X) ? 1 : -1])

#if defined(__IBMC__) || defined(__SUNPRO_C) || defined(__SUNPRO_CC)
# pragma pack(1)
#else
# pragma pack(push, 1)
#endif

typedef struct CRYPTO_ALIGN(16) _aes_stream_state {
    __m128i round_keys[ROUNDS + 1];
    __m128i counter;
} _aes_stream_state;

#if defined(__IBMC__) || defined(__SUNPRO_C) || defined(__SUNPRO_CC)
# pragma pack()
#else
# pragma pack(pop)
#endif

static void
_aes_key_expand(__m128i round_keys[ROUNDS + 1], __m128i t)
{
    __m128i t1;

#define DRC(ROUND, RC)                                     \
    do {                                                   \
        t1 = _mm_aeskeygenassist_si128(t, (RC));           \
        round_keys[ROUND] = t;                             \
        t = _mm_xor_si128(t, _mm_slli_si128(t, 4));        \
        t = _mm_xor_si128(t, _mm_slli_si128(t, 8));        \
        t = _mm_xor_si128(t, _mm_shuffle_epi32(t1, 0xff)); \
    } while (0)

    DRC(0, 1); DRC(1, 2); DRC(2, 4); DRC(3, 8); DRC(4, 16);
    DRC(5, 32); DRC(6, 64); DRC(7, 128); DRC(8, 27); DRC(9, 54);
    round_keys[10] = t;
}

static void
_aes_stream(unsigned char *buf, size_t buf_len, _aes_stream_state *_st)
{
    CRYPTO_ALIGN(16) unsigned char t[16];
    const __m128i  one = _mm_set_epi64x(0, 1);
    __m128i       *round_keys = _st->round_keys;
    __m128i        s0, s1, r0, r1;
    __m128i        c0 = _st->counter, c1;
    size_t         i;
    size_t         remaining;

#define COMPUTE_ROUNDS(N)                                                              \
    do {                                                                               \
        r##N = _mm_aesenc_si128(   _mm_xor_si128(c##N, round_keys[0]), round_keys[1]); \
        r##N = _mm_aesenc_si128(_mm_aesenc_si128(r##N, round_keys[2]), round_keys[3]); \
        r##N = _mm_aesenc_si128(_mm_aesenc_si128(r##N, round_keys[4]), round_keys[5]); \
        s##N = r##N;                                                                   \
        r##N = _mm_aesenc_si128(_mm_aesenc_si128(r##N, round_keys[6]), round_keys[7]); \
        r##N = _mm_aesenc_si128(_mm_aesenc_si128(r##N, round_keys[8]), round_keys[9]); \
        r##N = _mm_xor_si128(s##N, _mm_aesenclast_si128(r##N, round_keys[10]));        \
    } while (0)

    remaining = buf_len;
    while (remaining > 32) {
        c1 = _mm_add_epi64(c0, one);
        COMPUTE_ROUNDS(0);
        COMPUTE_ROUNDS(1);
        c0 = _mm_add_epi64(c1, one);
        _mm_storeu_si128((__m128i *) (void *) (buf +  0), r0);
        _mm_storeu_si128((__m128i *) (void *) (buf + 16), r1);
        buf += 32;
        remaining -= 32;
    }
    while (remaining > 16) {
        COMPUTE_ROUNDS(0);
        c0 = _mm_add_epi64(c0, one);
        _mm_storeu_si128((__m128i *) (void *) buf, r0);
        buf += 16;
        remaining -= 16;
    }
    if (remaining > (size_t) 0U) {
        COMPUTE_ROUNDS(0);
        c0 = _mm_add_epi64(c0, one);
        _mm_store_si128((__m128i *) (void *) t, r0);
        for (i = 0; i < remaining; i++) {
            buf[i] = t[i];
        }
    }
    COMPUTE_ROUNDS(0);
    _aes_key_expand(round_keys, _mm_xor_si128(r0, round_keys[0]));
    _st->counter = c0;
}

void
aes_stream_init(aes_stream_state *st,
                const unsigned char key[AES_STREAM_KEYBYTES])
{
    _aes_stream_state *_st = (_aes_stream_state *) (void *) st;

    COMPILER_ASSERT(sizeof *st >= sizeof *_st);
    _aes_key_expand(_st->round_keys,
                    _mm_loadu_si128((const __m128i *) (const void *) key));
    _st->counter = _mm_setzero_si128();
}

void
aes_stream(unsigned char *buf, size_t buf_len, aes_stream_state *st)
{
    _aes_stream(buf, buf_len, (_aes_stream_state *) (void *) st);
}
