#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

#define TEST_MIN(a, b) ((a) < (b) ? (a) : (b))

static unsigned next_random(unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state;
}

#define min(lth, rth) ((lth) < (rth) ? (lth) : (rth))
/* These helpers are implemented by buffer.c but are missing from buffer.h. */
extern uint32_t rb_isempty(buffer_t *r);
extern uint32_t rb_isfull(buffer_t *r);

static void expect_bytes(buffer_t *rb, const uint8_t *want, uint32_t n)
{
    uint8_t got[256];
    assert(n <= sizeof(got));
    memset(got, 0, sizeof(got));
    assert(buffer_len(rb) == n);
    assert(buffer_remove(rb, got, sizeof(got)) == (int)n);
    assert(memcmp(got, want, n) == 0);
    assert(rb_isempty(rb));
}

static void test_empty_and_capacity(void)
{
    buffer_t *rb = buffer_new(8);
    uint8_t in[8] = {0,1,2,3,4,5,6,7};
    uint8_t out[8] = {0};
    uint32_t writable = 0;

    assert(rb != NULL);
    assert(buffer_len(rb) == 0);
    assert(rb_isempty(rb));
    assert(!rb_isfull(rb));
    assert(buffer_remove(rb, out, 1) == 0);
    assert(buffer_drain(rb, 1) == 0);

    assert(buffer_write_atmost(rb, &writable) != NULL);
    assert(writable == 7);                 /* one slot is reserved */
    assert(buffer_add(rb, in, 7) == 7);
    assert(buffer_len(rb) == 7);
    assert(rb_isfull(rb));
    assert(buffer_write_atmost(rb, &writable) == NULL);
    assert(writable == 0);
    assert(buffer_add(rb, in, 1) == 0);
    assert(buffer_remove(rb, out, 8) == 7);
    assert(memcmp(out, in, 7) == 0);
    assert(rb_isempty(rb));
    buffer_free(rb);
    puts("[capacity] PASS");
}

static void test_constructor_rounding(void)
{
    buffer_t *rb = buffer_new(3);
    uint8_t data[] = {1, 2, 3, 4};
    uint8_t out[4] = {0};

    assert(rb != NULL);
    /* 3 rounds up to 4, with one slot reserved for full/empty detection. */
    assert(buffer_add(rb, data, 3) == 3);
    assert(buffer_add(rb, data + 3, 1) == 0);
    assert(buffer_remove(rb, out, sizeof(out)) == 3);
    assert(memcmp(out, data, 3) == 0);
    buffer_free(rb);
    puts("[constructor rounding] PASS");
}

static void test_wrap_and_partial_operations(void)
{
    buffer_t *rb = buffer_new(8);
    uint8_t first[] = {0,1,2,3};
    uint8_t second[] = {5,6,7,8,9};
    uint8_t want[] = {2,3,5,6,7,8,9};
    uint8_t out[8] = {0};

    assert(buffer_add(rb, first, 4) == 4);
    assert(buffer_remove(rb, out, 2) == 2);
    assert(memcmp(out, first, 2) == 0);
    assert(buffer_add(rb, second, 5) == 5); /* write wraps at the end */
    assert(buffer_len(rb) == 8 - 1);
    expect_bytes(rb, want, sizeof(want));

    assert(buffer_add(rb, first, 4) == 4);
    assert(buffer_drain(rb, 2) == 2);
    assert(buffer_len(rb) == 2);
    assert(buffer_remove(rb, out, 1) == 1);
    assert(out[0] == 2);
    assert(buffer_drain(rb, 99) == 1);
    assert(rb_isempty(rb));
    buffer_free(rb);
    puts("[wrap/partial] PASS");
}

static void test_write_atmost_boundaries(void)
{
    buffer_t *rb = buffer_new(16);
    uint8_t data[15];
    uint32_t n = 0;
    uint8_t *p;

    for (uint8_t i = 0; i < sizeof(data); ++i) data[i] = i;
    p = buffer_write_atmost(rb, &n);
    assert(p != NULL && n == 15);
    assert(buffer_add(rb, data, 10) == 10);       /* head=0, tail=10 */
    p = buffer_write_atmost(rb, &n);
    assert(p != NULL && n == 5);                 /* only [10..14] */
    assert(buffer_drain(rb, 8) == 8);             /* head=8, tail=10 */
    p = buffer_write_atmost(rb, &n);
    assert(p != NULL && n == 6);                 /* contiguous bytes [10..15] */
    assert(buffer_add(rb, data, 5) == 5);         /* tail=15 */
    assert(buffer_drain(rb, 4) == 4);             /* head=12 */
    assert(buffer_add(rb, data, 3) == 3);         /* tail=2, head=12 */
    p = buffer_write_atmost(rb, &n);
    assert(p != NULL && n == 9);                 /* head-tail-1 = 9 */
    assert(buffer_add(rb, data, 9) == 9);
    assert(rb_isfull(rb));
    assert(buffer_write_atmost(rb, &n) == NULL && n == 0);
    buffer_free(rb);
    puts("[write_atmost] PASS");
}

static void test_zero_copy_commit(void)
{
    buffer_t *rb = buffer_new(8);
    uint8_t *dst;
    uint8_t out[8] = {0};
    uint32_t writable = 0;

    assert(rb != NULL);
    dst = buffer_write_atmost(rb, &writable);
    assert(dst != NULL && writable == 7);
    memcpy(dst, "abc", 3);
    assert(buffer_write_commit(rb, 3) == 0);
    assert(buffer_len(rb) == 3);
    assert(buffer_remove(rb, out, sizeof(out)) == 3);
    assert(memcmp(out, "abc", 3) == 0);

    /* A zero-byte commit must leave the buffer unchanged. */
    dst = buffer_write_atmost(rb, &writable);
    assert(dst != NULL && writable > 0);
    assert(buffer_write_commit(rb, 0) == 0);
    assert(buffer_len(rb) == 0);
    buffer_free(rb);
    puts("[zero-copy commit] PASS");
}

static void test_zero_copy_contiguous_limit(void)
{
    buffer_t *rb = buffer_new(8);
    uint8_t input[] = {0, 1, 2, 3, 4, 5};
    uint8_t out[8] = {0};
    uint8_t *dst;
    uint32_t writable = 0;

    assert(rb != NULL);
    assert(buffer_add(rb, input, sizeof(input)) == (int)sizeof(input));
    assert(buffer_drain(rb, 4) == 4); /* head=4, tail=6 */

    dst = buffer_write_atmost(rb, &writable);
    assert(dst != NULL && writable == 2); /* only indexes 6 and 7 are contiguous */
    dst[0] = 6;
    dst[1] = 7;

    /*
     * Total free capacity is larger than this contiguous reservation.  The
     * commit must reject 3, because the caller only received 2 writable bytes.
     */
    assert(buffer_write_commit(rb, writable + 1) == -1);
    assert(buffer_len(rb) == 2);

    assert(buffer_write_commit(rb, writable) == 0);
    assert(buffer_remove(rb, out, sizeof(out)) == 4);
    assert(memcmp(out, "\004\005\006\007", 4) == 0);
    buffer_free(rb);
    puts("[zero-copy contiguous limit] PASS");
}

static void test_zero_copy_wrap(void)
{
    buffer_t *rb = buffer_new(8);
    uint8_t input[] = {10, 11, 12, 13, 14, 15};
    uint8_t out[8] = {0};
    uint8_t *dst;
    uint32_t writable = 0;

    assert(rb != NULL);
    assert(buffer_add(rb, input, sizeof(input)) == (int)sizeof(input));
    assert(buffer_drain(rb, 4) == 4); /* head=4, tail=6 */

    dst = buffer_write_atmost(rb, &writable);
    assert(dst != NULL && writable == 2);
    dst[0] = 16;
    dst[1] = 17;
    assert(buffer_write_commit(rb, 2) == 0); /* tail wraps to 0 */

    dst = buffer_write_atmost(rb, &writable);
    assert(dst != NULL && writable == 3);
    dst[0] = 18;
    dst[1] = 19;
    dst[2] = 20;
    assert(buffer_write_commit(rb, 3) == 0);

    assert(buffer_remove(rb, out, sizeof(out)) == 7);
    assert(memcmp(out, "\016\017\020\021\022\023\024", 7) == 0);
    buffer_free(rb);
    puts("[zero-copy wrap] PASS");
}

static void test_search(void)
{
    buffer_t *rb = buffer_new(16);
    char out[32] = {0};
    char fill[15];
    memset(fill, 'X', sizeof(fill));

    assert(buffer_add(rb, "hello\r\nworld", 12) == 12);
    assert(buffer_search(rb, "\r\n", 2) == 7);
    assert(buffer_search(rb, "xyz", 3) == -1);
    assert(buffer_search(rb, "", 0) == 0);
    assert(buffer_search(rb, "long", 4) == -1);
    buffer_free(rb);

    rb = buffer_new(16);
    assert(buffer_add(rb, fill, sizeof(fill)) == 15);
    assert(buffer_drain(rb, 14) == 14);
    assert(buffer_add(rb, "\r", 1) == 1);
    assert(buffer_add(rb, "\n", 1) == 1);
    assert(buffer_search(rb, "\r\n", 2) == 3); /* separator crosses index 15/0 */
    assert(buffer_remove(rb, out, 3) == 3);
    assert(memcmp(out, "X\r\n", 3) == 0);
    buffer_free(rb);
    puts("[search] PASS");
}

static void test_random_model(void)
{
    enum { CAP = 32, STEPS = 20000 };
    buffer_t *rb = buffer_new(CAP);
    uint8_t model[CAP - 1];
    uint8_t tmp[CAP];
    uint32_t model_len = 0;
    unsigned seed = 0x12345678u;

    for (int step = 0; step < STEPS; ++step) {
        unsigned op = next_random(&seed) % 3;
        uint32_t amount = next_random(&seed) % CAP;
        if (op == 0) {
            for (uint32_t i = 0; i < amount; ++i) tmp[i] = (uint8_t)next_random(&seed);
            uint32_t accepted = amount <= (CAP - 1 - model_len) ? amount : 0;
            assert(buffer_add(rb, tmp, amount) == (int)accepted);
            if (accepted) {
                memcpy(model + model_len, tmp, accepted);
                model_len += accepted;
            }
        } else if (op == 1) {
            uint32_t got = TEST_MIN(amount, model_len);
            assert(buffer_remove(rb, tmp, amount) == (int)got);
            assert(memcmp(tmp, model, got) == 0);
            memmove(model, model + got, model_len - got);
            model_len -= got;
        } else {
            uint32_t got = TEST_MIN(amount, model_len);
            assert(buffer_drain(rb, amount) == (int)got);
            memmove(model, model + got, model_len - got);
            model_len -= got;
        }
        assert(buffer_len(rb) == model_len);
        assert((rb_isfull(rb) != 0) == (model_len == CAP - 1));
    }
    buffer_free(rb);
    puts("[random model] PASS");
}

int main(void)
{
    test_empty_and_capacity();
    test_constructor_rounding();
    test_wrap_and_partial_operations();
    test_write_atmost_boundaries();
    test_zero_copy_commit();
    test_zero_copy_contiguous_limit();
    test_zero_copy_wrap();
    test_search();
    test_random_model();
    puts("==== All tests passed ====");
    return 0;
}
