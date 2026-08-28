#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include "buffer.h"

struct buf_chain_s
{
    struct buf_chain_s *next;
    uint32_t buffer_len;
    uint32_t misalign;
    uint32_t off;
    uint8_t *buffer;
};

struct buffer_s
{
    buf_chain_t *first;
    buf_chain_t *last;
    buf_chain_t **last_with_datap;
    uint32_t total_len;
    uint32_t last_read_pos; // for sep read
};
#define CHAIN_SPACE_LEN(ch) ((ch)->buffer_len - ((ch)->misalign + (ch)->off))
#define MIN_BUFFER_SIZE 1024              // chain最小分配内存块大小
#define MAX_TO_COPY_IN_EXPAND 4096        // expand场景：小数据优先拷贝，不新开chain阈值
#define BUFFER_CHAIN_MAX_AUTO_SIZE 4096   // 自动分配chain最大size；超过这个就直接分配更大块
#define MAX_TO_REALIGN_IN_EXPAND 2048     // realign内存整理阈值；最多移动 2048 字节有效数据进行整理
#define BUFFER_CHAIN_MAX 16 * 1024 * 1024 // 16M
#define BUFFER_CHAIN_EXTRA(t, c) (t *)((buf_chain_t *)(c) + 1)
#define BUFFER_CHAIN_SIZE sizeof(buf_chain_t)

static buf_chain_t *buf_chain_new(uint32_t size);
static void buf_chain_free(buf_chain_t *chain);
static void buffer_chain_realign(buf_chain_t *chain);
static int buffer_expand(buffer_t *buf, uint32_t need);
static int buffer_append_chain(buffer_t *buf, const void *data, uint32_t len);
static uint32_t buffer_copyout_chain(buffer_t *buf, void *data, uint32_t len);
static void buffer_remove_empty_chains(buffer_t *buf);
static uint32_t buffer_drain_chain(buffer_t *buf, uint32_t len);
static void buffer_append_chain_node(buffer_t *buf, buf_chain_t *chain);
static buf_chain_t *buffer_get_write_chain(buffer_t *buf);
static int buffer_get_char_at(const buffer_t *buf, uint32_t pos, uint8_t *out);

uint32_t
buffer_len(buffer_t *buf)
{
    return buf->total_len;
}

buffer_t *
buffer_new(uint32_t sz)
{
    (void)sz;
    buffer_t *buf = (buffer_t *)malloc(sizeof(buffer_t));
    if (!buf)
    {
        return NULL;
    }
    memset(buf, 0, sizeof(*buf));
    buf->last_with_datap = &buf->first;
    return buf;
}

static buf_chain_t *
buf_chain_new(uint32_t size)
{
    buf_chain_t *chain;
    uint32_t to_alloc;

    if (size > BUFFER_CHAIN_MAX - BUFFER_CHAIN_SIZE)
        return (NULL);

    size += BUFFER_CHAIN_SIZE;

    if (size < BUFFER_CHAIN_MAX_AUTO_SIZE / 2)
    {
        to_alloc = MIN_BUFFER_SIZE;
        while (to_alloc < size)
        {
            to_alloc <<= 1;
        }
    }
    else
    {
        to_alloc = size;
    }

    if ((chain = malloc(to_alloc)) == NULL)
        return (NULL);
    memset(chain, 0, BUFFER_CHAIN_SIZE);
    chain->buffer_len = to_alloc - BUFFER_CHAIN_SIZE;
    chain->next = NULL;
    chain->misalign = 0;
    chain->off = 0;
    chain->buffer = BUFFER_CHAIN_EXTRA(uint8_t, chain);

    return chain;
}
// 释放单个节点
static void buf_chain_free(buf_chain_t *chain)
{
    free(chain);
}

// 前部空间不足但尾部有碎片时移动数据
static void buffer_chain_realign(buf_chain_t *chain)
{
    memmove(chain->buffer, chain->buffer + chain->misalign, chain->off);
    chain->misalign = 0;
}
// 空间不足时追加或扩展节点
//  static int
//  buffer_expand(buffer_t *buf, uint32_t need)
//  {
//      if (need == 0)
//        return 0;
//      buf_chain_t *chain = buf->last;
//      if (chain != NULL && CHAIN_SPACE_LEN(buf->last) >= need)
//          return 1;

//     if (chain != NULL &&
//         chain->misalign > 0 &&
//         chain->off <= MAX_TO_REALIGN_IN_EXPAND &&
//         chain->buffer_len - chain->off >= need) {
//         buffer_chain_realign(chain);
//         return 1;
//     }
//     chain = buf_chain_new(need);
//     if(!chain){
//         return -1;
//     }
//     //入链
//     buffer_append_chain_node(buf,chain);

//     return 0;
// }
static int
buffer_expand(buffer_t *buf, uint32_t need)
{
    if (need == 0)
        return 0;
    buf_chain_t *chain = buffer_get_write_chain(buf);
    if (chain != NULL && CHAIN_SPACE_LEN(chain) >= need)
        return 0;

    if (chain != NULL &&
        chain->misalign > 0 &&
        chain->off <= MAX_TO_REALIGN_IN_EXPAND &&
        chain->buffer_len - chain->off >= need)
    {
        buffer_chain_realign(chain);
        return 0;
    }
    chain = buf_chain_new(need);
    if (!chain)
    {
        return -1;
    }
    // 入链
    buffer_append_chain_node(buf, chain);

    return 0;
}
// static int
// buffer_append_chain(buffer_t *buf,const void *data,uint32_t len)
// {
//     buf_chain_t* chain = buf->last;
//     uint32_t ret = buffer_expand(buf,len);
//     if(ret == 1){
//         memmove(chain->buffer + chain->misalign + chain->off,data,len);
//         chain->off += len;
//     }else if(ret == 0){ //扩容后
//         if(chain == NULL) chain = buf->last;
//         uint32_t part1 = CHAIN_SPACE_LEN(chain);
//         if(part1 > 0){
//             memmove(chain->buffer + chain->misalign + chain->off,data,part1);
//             chain->off += part1;
//         }
//         uint32_t part2 = len - part1;
//         if(part2 > 0){
//             chain = buf->last;
//             memmove(chain->buffer + chain->misalign + chain->off,(uint8_t*)data + part1,part2);
//             chain->off += part2;
//             buf->last_with_datap = &chain->next;
//          }
//     }
//     if(ret >= 0){
//         buf->total_len += len;
//     }

//     return ret;
// }

static int
buffer_append_chain(buffer_t *buf, const void *data, uint32_t len)
{
    const uint8_t *src = data;
    uint32_t remaining = len;
    if (buf == NULL || (data == NULL && len != 0))
        return -1;
    while (remaining > 0)
    {
        buf_chain_t *chain;
        uint32_t space;
        uint32_t written;

        chain = buffer_get_write_chain(buf);
        if (chain == NULL || CHAIN_SPACE_LEN(chain) == 0)
        {
            if (buffer_expand(buf, remaining) < 0)
            {
                return -1;
            }
            chain = buffer_get_write_chain(buf);
        }

        if (chain == NULL)
            return -1;
        space = CHAIN_SPACE_LEN(chain);

        written = space < remaining ? space : remaining;
        memmove(chain->buffer + chain->misalign + chain->off, src, written);

        chain->off += written;
        buf->total_len += written;

        buf->last_with_datap = &chain->next;
        src += written;
        remaining -= written;
    }
    return (int)len;
}
static uint32_t
buffer_copyout_chain(buffer_t *buf, void *data, uint32_t len)
{
    uint32_t ret = 0;
    buf_chain_t *chain = buf->first;
    while (len > 0 && chain != NULL)
    {

        uint32_t n = chain->off < len ? chain->off : len;
        memmove((uint8_t *)data + ret, chain->buffer + chain->misalign, n);
        ret += n;
        int curoff = chain->off - n;
        len -= n;
        if (curoff == 0)
        {
            chain = chain->next;
        }
    }
    return (int)ret;
}
// 消费完数据后释放空节点
static void buffer_remove_empty_chains(buffer_t *buf)
{
    while (buf->first != NULL && buf->first->off == 0)
    {
        buf_chain_t *chain = buf->first;
        buf->first = chain->next;
        buf_chain_free(chain);
    }
}
static uint32_t
buffer_drain_chain(buffer_t *buf, uint32_t len)
{
    uint32_t ret = 0;
    while (len > 0 && buf->first != NULL)
    {
        buf_chain_t *chain = buf->first;
        uint32_t n = chain->off < len ? chain->off : len;
        chain->misalign += n;
        chain->off -= n;
        len -= n;
        ret += n;
        buf->total_len -= n;
        if (chain->off == 0)
        {
            buffer_remove_empty_chains(buf);
        }
    }
    if (buf->first == NULL)
    {
        buf->last = NULL;
        buf->last_with_datap = &buf->first;
    }
    return (int)ret;
}

// 把新节点接到链表尾部
static void buffer_append_chain_node(buffer_t *buf, buf_chain_t *chain)
{
    if (buf == NULL || chain == NULL)
        return;
    chain->next = *buf->last_with_datap;
    *buf->last_with_datap = chain;
    if (chain->next == NULL)
        buf->last = chain;
}

void buffer_free(buffer_t *buf)
{
    if (buf == NULL)
        return;

    while (buf->first)
    {
        buf_chain_t *chain = buf->first;
        buf->first = chain->next;
        buf_chain_free(chain);
    }
    free(buf);
}

int buffer_add(buffer_t *buf, const void *data, uint32_t sz)
{
    if (buf == NULL || data == NULL)
        return -1;
    if (sz == 0)
        return 0;
    return buffer_append_chain(buf, data, sz);
}

int buffer_remove(buffer_t *buf, void *data, uint32_t sz)
{
    if (buf == NULL || data == NULL)
        return -1;
    if (sz == 0)
        return 0;
    uint32_t ret = buffer_copyout_chain(buf, data, sz);
    return buffer_drain_chain(buf, ret);
}

int buffer_drain(buffer_t *buf, uint32_t sz)
{
    if (buf == NULL)
        return -1;
    if (sz == 0)
        return 0;
    return buffer_drain_chain(buf, sz);
}

static int
buffer_get_char_at(const buffer_t *buf, uint32_t pos, uint8_t *out)
{
    const buf_chain_t *chain = buf->first;

    while (chain != NULL)
    {
        if (pos < chain->off)
        {
            *out = chain->buffer[chain->misalign + pos];
            return 0;
        }

        pos -= chain->off;
        chain = chain->next;
    }

    return -1;
}
int buffer_search(buffer_t *buf, const char *sep, const uint32_t seplen)
{
    if (buf == NULL || (sep == NULL && seplen != 0) || buf->total_len < seplen)
        return -1;
    if (seplen == 0)
        return 0;
    for (uint32_t start = 0; start <= buf->total_len - seplen; ++start)
    {
        bool match = true;
        for (uint32_t j = 0; j < seplen; ++j)
        {
            uint8_t c;
            if (buffer_get_char_at(buf, start + j, &c) < 0 || c != (uint8_t)sep[j])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return (int)(start + seplen);
        }
    }
    return -1;
}
static buf_chain_t *
buffer_get_write_chain(buffer_t *buf)
{
    buf_chain_t *chain;

    if (buf == NULL)
        return NULL;

    chain = *buf->last_with_datap;
    if (chain != NULL)
        return chain;

    return buf->last;
}
uint8_t *
buffer_write_atmost(buffer_t *buf, uint32_t *out_sz)
{
    if (out_sz == NULL)
        return NULL;
    *out_sz = 0;
    if (buf == NULL)
        return NULL;
    buf_chain_t *chain = buffer_get_write_chain(buf);
    if (chain == NULL || CHAIN_SPACE_LEN(chain) == 0)
    {
        if (buffer_expand(buf, MIN_BUFFER_SIZE) < 0)
        {
            return NULL;
        }
        chain = buffer_get_write_chain(buf);
    }
    *out_sz = CHAIN_SPACE_LEN(chain);
    return chain->buffer + chain->misalign + chain->off;
}
int buffer_write_commit(buffer_t *buf, uint32_t written)
{
    if (buf == NULL)
        return -1;
    buf_chain_t *chain = buffer_get_write_chain(buf);
    if (written > CHAIN_SPACE_LEN(chain))
    {
        written = CHAIN_SPACE_LEN(chain);
    }
    chain->off += written;
    buf->total_len += written;
    buf->last_with_datap = &chain->next;
    return 0;
}
