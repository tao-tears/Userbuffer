#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buffer.h"

static unsigned next_random(unsigned *state)
{
    *state = *state * 1103515245u + 12345u;
    return *state;
}

static void expect_remove(buffer_t *buf, const uint8_t *want, uint32_t len)
{
    uint8_t *got = malloc(len ? len : 1);

    assert(got != NULL);
    assert(buffer_len(buf) == len);
    assert(buffer_remove(buf, got, len) == (int)len);
    assert(memcmp(got, want, len) == 0);
    free(got);
}

static void test_empty_and_arguments(void)
{
    buffer_t *buf = buffer_new(0);
    uint8_t out[4] = {0};

    assert(buf != NULL);
    assert(buffer_len(buf) == 0);
    /* Current implementation rejects NULL even for a zero-length add. */
    assert(buffer_add(buf, NULL, 0) == -1);
    assert(buffer_remove(buf, NULL, 0) == -1);
    assert(buffer_drain(buf, 0) == 0);
    assert(buffer_search(buf, "", 0) == 0);
    assert(buffer_search(buf, "x", 1) == -1);
    assert(buffer_remove(buf, out, sizeof(out)) == 0);
    assert(buffer_drain(buf, sizeof(out)) == 0);
    assert(buffer_add(buf, NULL, 1) == -1);
    assert(buffer_remove(buf, NULL, 1) == -1);
    buffer_free(buf);
    puts("[empty/arguments] PASS");
}

static void test_cross_chain_append_remove(void)
{
    enum { FIRST = 3000, SECOND = 5000 };
    uint8_t *first = malloc(FIRST);
    uint8_t *second = malloc(SECOND);
    uint8_t *want = malloc(FIRST + SECOND);
    buffer_t *buf = buffer_new(0);

    assert(first && second && want && buf);
    for (uint32_t i = 0; i < FIRST; ++i)
        first[i] = (uint8_t)(i * 3u);
    for (uint32_t i = 0; i < SECOND; ++i)
        second[i] = (uint8_t)(i * 7u + 1u);
    memcpy(want, first, FIRST);
    memcpy(want + FIRST, second, SECOND);

    assert(buffer_add(buf, first, FIRST) == FIRST);
    assert(buffer_add(buf, second, SECOND) == SECOND);
    expect_remove(buf, want, FIRST + SECOND);
    assert(buffer_len(buf) == 0);

    free(first);
    free(second);
    free(want);
    buffer_free(buf);
    puts("[cross-chain append/remove] PASS");
}

static void test_partial_drain_and_realign(void)
{
    enum { INITIAL = 2000, DRAINED = 1000, APPENDED = 1500 };
    uint8_t initial[INITIAL];
    uint8_t appended[APPENDED];
    uint8_t *want = malloc(INITIAL - DRAINED + APPENDED);
    buffer_t *buf = buffer_new(0);

    assert(want && buf);
    for (uint32_t i = 0; i < INITIAL; ++i)
        initial[i] = (uint8_t)i;
    for (uint32_t i = 0; i < APPENDED; ++i)
        appended[i] = (uint8_t)(200u + i);

    assert(buffer_add(buf, initial, INITIAL) == INITIAL);
    assert(buffer_drain(buf, DRAINED) == DRAINED);
    assert(buffer_add(buf, appended, APPENDED) == APPENDED);
    memcpy(want, initial + DRAINED, INITIAL - DRAINED);
    memcpy(want + INITIAL - DRAINED, appended, APPENDED);
    expect_remove(buf, want, INITIAL - DRAINED + APPENDED);

    free(want);
    buffer_free(buf);
    puts("[partial drain/realign] PASS");
}

static void test_search(void)
{
    buffer_t *buf = buffer_new(0);
    const char *a = "prefix-abc";
    const char *b = "def-suffix";

    assert(buf != NULL);
    assert(buffer_add(buf, a, (uint32_t)strlen(a)) == (int)strlen(a));
    assert(buffer_add(buf, b, (uint32_t)strlen(b)) == (int)strlen(b));
    assert(buffer_search(buf, "", 0) == 0);
    assert(buffer_search(buf, "abcde", 5) == 12); /* crosses the two appends */
    assert(buffer_search(buf, "suffix", 6) == 20);
    assert(buffer_search(buf, "missing", 7) == -1);
    assert(buffer_len(buf) == strlen(a) + strlen(b));
    buffer_free(buf);
    puts("[search] PASS");
}

static void test_empty_node_reuse_and_commit(void)
{
    buffer_t *buf = buffer_new(0);
    uint32_t writable = 0;
    uint8_t *dst;
    const char *msg = "zero-copy payload";
    uint32_t len = (uint32_t)strlen(msg);

    assert(buf != NULL);
    dst = buffer_write_atmost(buf, &writable);
    assert(dst != NULL);
    assert(writable >= len);
    memcpy(dst, msg, len);
    assert(buffer_write_commit(buf, len) == 0);
    assert(buffer_len(buf) == len);
    expect_remove(buf, (const uint8_t *)msg, len);

    /* A zero-byte commit leaves an empty node; a later append must reuse it. */
    dst = buffer_write_atmost(buf, &writable);
    assert(dst != NULL && writable > 0);
    assert(buffer_write_commit(buf, 0) == 0);
    assert(buffer_add(buf, "reuse", 5) == 5);
    expect_remove(buf, (const uint8_t *)"reuse", 5);

    buffer_free(buf);
    puts("[empty-node reuse/commit] PASS");
}

static void test_random_model(void)
{
    enum { STEPS = 20000, MAX_MODEL = 100000 };
    buffer_t *buf = buffer_new(0);
    uint8_t *model = malloc(MAX_MODEL);
    uint8_t input[4096];
    uint8_t output[4096];
    uint32_t model_len = 0;
    unsigned seed = 0x12345678u;

    assert(buf && model);
    for (int step = 0; step < STEPS; ++step) {
        unsigned op = next_random(&seed) % 3u;
        uint32_t amount = next_random(&seed) % sizeof(input);

        if (op == 0) {
            assert(model_len + amount <= MAX_MODEL);
            for (uint32_t i = 0; i < amount; ++i)
                input[i] = (uint8_t)next_random(&seed);
            assert(buffer_add(buf, input, amount) == (int)amount);
            memcpy(model + model_len, input, amount);
            model_len += amount;
        } else if (op == 1) {
            uint32_t n = amount < model_len ? amount : model_len;
            assert(buffer_remove(buf, output, amount) == (int)n);
            assert(memcmp(output, model, n) == 0);
            memmove(model, model + n, model_len - n);
            model_len -= n;
        } else {
            uint32_t n = amount < model_len ? amount : model_len;
            assert(buffer_drain(buf, amount) == (int)n);
            memmove(model, model + n, model_len - n);
            model_len -= n;
        }
        assert(buffer_len(buf) == model_len);
    }

    expect_remove(buf, model, model_len);
    free(model);
    buffer_free(buf);
    puts("[random model] PASS");
}

int main(void)
{
    test_empty_and_arguments();
    test_cross_chain_append_remove();
    test_partial_drain_and_realign();
    test_search();
    test_empty_node_reuse_and_commit();
    test_random_model();
    puts("==== All chain buffer tests passed ====");
    return 0;
}
