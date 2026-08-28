#include <stdbool.h>
#include <assert.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "buffer.h"

struct ringbuffer_s
{
    uint32_t size; // 总容量 固定为2的指数
    uint32_t tail; // 写指针（生产者）
    uint32_t head; // 读指针（消费者）
    uint8_t *buf;  // 数据缓冲区
    // 预留 1 个空位
};

static inline uint32_t min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}


static inline int is_power_of_two(uint32_t num)
{
    if (num < 2)
        return 0;
    return (num & (num - 1)) == 0;
}

static inline uint32_t roundup_power_of_two(uint32_t num)
{
    if (num == 0)
        return 2;
    int i = 0;
    for (; num != 0; ++i)
        num >>= 1;
    return 1U << i; // 1U  unsigned int，无符号整数
}
buffer_t *buffer_new(uint32_t sz)
{
    if (!is_power_of_two(sz))
        sz = roundup_power_of_two(sz);

    buffer_t *buf = (buffer_t *)malloc(sizeof(buffer_t) + sz);
    if (!buf)
    {
        return NULL;
    }

    buf->size = sz;
    buf->head = buf->tail = 0;
    buf->buf = (uint8_t *)(buf + 1);
    return buf;
}
void buffer_free(buffer_t *r)
{
    free(r);
    r = NULL;
}

// static uint32_t
uint32_t
rb_isempty(buffer_t *r)
{
    return r->head == r->tail;
}
// static uint32_t
uint32_t
rb_isfull(buffer_t *r)
{
    uint32_t next_tail = (r->tail + 1) & (r->size - 1);
    return next_tail == r->head;
}

uint32_t
buffer_len(buffer_t *r)
{
    if (r->tail < r->head)
    {
        return r->tail + r->size - r->head;
    }
    return r->tail - r->head;
}
/*
int
buffer_add(buffer_t *r,const void *data,uint32_t sz)
{
    const uint8_t* src = (const uint8_t*)data;
    uint32_t writable = (r->size - 1) - buffer_len(r);
    if (sz > writable) {
        //扩容机制
        return 0;
    }
    if(rb_isempty(r)){//空
        uint32_t tail_cap = r->size - r->tail;
        uint32_t head_cap = r->head;
        if(tail_cap >= sz){
            memmove(r->buf + r->tail,src,sz);

        }else if(tail_cap + head_cap >= sz){
            memmove(r->buf + r->tail,src,tail_cap);
            memmove(r->buf,src + tail_cap,sz-tail_cap);

        }
    }else if(r->tail > r->head){
        uint32_t tail_cap = r->size - r->tail;
        uint32_t head_cap = r->head;
        if(tail_cap >= sz){
            memmove(r->buf + r->tail,src,sz);

        }else if(tail_cap + head_cap >= sz){
            memmove(r->buf + r->tail,src,tail_cap);
            memmove(r->buf,src + tail_cap,sz-tail_cap);

        }
    }else{
        int cap = r->head - r->tail;
        if(cap >= sz){
            memmove(r->buf + r->tail,src,sz);

        }
    }
    r->tail = (r->tail + sz) % r->size;
    return sz;
}
    */
int buffer_add(buffer_t *r, const void *data, uint32_t sz)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t writable = (r->size - 1) - buffer_len(r);
    if (sz > writable)
    {
        // 扩容机制
        return 0;
    }
    uint32_t tail_cap = r->size - r->tail;
    uint32_t part1 = min(tail_cap, sz);
    memmove(r->buf + r->tail, src, part1);

    uint32_t part2 = sz - part1;
    if (part2 > 0)
    {
        memmove(r->buf, src + part1, part2);
    }
    r->tail = (r->tail + sz) & (r->size - 1); // => % r->size

    return sz;
}
// int
// buffer_remove(buffer_t *r,void *data,uint32_t sz)
// {
//     uint8_t* src = (uint8_t*)data;
//     uint32_t cap = 0;
//     uint32_t ret = 0;
//     if(rb_isempty(r)){
//         ret = 0;
//     }else if(r->head < r->tail){
//         cap = r->tail - r->head;
//         ret = cap > sz ? sz : cap;
//         memmove(data,r->buf + r->head,ret);
//         r->head = (r->head + ret ) % r->size;
//     }else{
//         uint32_t tail_cap = r->size - r->head;
//         uint32_t head_cap = r->tail;
//         cap = tail_cap + head_cap;
//         ret = cap > sz ? sz : cap;
//         if(ret <= tail_cap){
//             memmove(src,r->buf + r->head,ret);
//             r->head = (r->head + ret) % r->size;
//         }else{
//             memmove(data,r->buf + r->head,tail_cap);
//             memmove((void*)(src + tail_cap),r->buf,ret - tail_cap);
//             r->head = ret - tail_cap;
//         }
//     }
//     return ret;
// }
int buffer_remove(buffer_t *r, void *data, uint32_t sz)
{
    if (rb_isempty(r))
    {
        return 0;
    }
    uint32_t len = buffer_len(r);
    uint32_t ret = min(sz, len); // 有效可读数
    uint32_t tail_read = r->size - r->head;
    uint32_t part1 = min(tail_read, ret);
    memmove(data, r->buf + r->head, part1);

    uint32_t part2 = ret - part1;
    if (part2 > 0)
    {
        memmove((void *)((uint8_t *)data + part1), r->buf, part2);
    }
    r->head = (r->head + ret) & (r->size - 1); // => % r->size
    return ret;
}
// int buffer_drain(buffer_t *r,uint32_t sz)
// {
//     uint32_t ret = 0;
//     if(rb_isempty(r)){
//         ret = 0;
//     }else if(r->head < r->tail){
//         uint32_t cap = r->tail - r->head;
//         ret = cap > sz ? sz : cap;
//         r->head += ret;
//     }else{
//         uint32_t tail_cap = r->size - r->head;
//         uint32_t head_cap = r->tail;
//         uint32_t cap = tail_cap + head_cap;
//         ret = cap > sz ? sz : cap;
//         if(ret <= tail_cap){
//             r->head = (r->head + ret) % r->size;
//         }else{
//             r->head = ret - tail_cap;
//         }
//     }
//     return ret;
// }
int buffer_drain(buffer_t *r, uint32_t sz)
{
    if (rb_isempty(r))
    {
        return 0;
    }
    uint32_t len = buffer_len(r);
    uint32_t ret = min(sz, len);               // 有效消费数
    r->head = (r->head + ret) & (r->size - 1); // => % r->size
    return ret;
}
void buffer_debug_print(buffer_t *rb)
{
    printf("size:%u len:%u empty:%u full:%u head:%u tail:%u\n",
           rb->size, buffer_len(rb), rb_isempty(rb), rb_isfull(rb),
           rb->head, rb->tail);
}
static inline char buffer_get_char(buffer_t *r, uint32_t offset)
{
    uint32_t idx = (r->head + offset) & (r->size - 1);
    return r->buf[idx];
}
int buffer_search(buffer_t *r, const char *sep, const uint32_t seplen)
{
    uint32_t ret = 0;
    uint32_t len = buffer_len(r);
    if(len < seplen){
        return -1;
    }
    for (int i = 0; i <= len - seplen; ++i)
    {
        bool match = true;
        for (int j = 0; j < seplen; ++j)
        {
            char c = buffer_get_char(r, i + j);
            if (c != sep[j])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            ret = i + seplen;
            return ret;
        }
    }
    return -1;
}
uint8_t * 
buffer_write_atmost(buffer_t *r,uint32_t *out_sz)
{
   if (out_sz == NULL)
    {
        return NULL;
    }
    if(rb_isfull(r))
    {
        *out_sz = 0;
        return NULL;
    }
    uint32_t cnt;
    if(r->head <= r->tail)
    {
        cnt = r->size - r->tail;
    }
    else
    {
        cnt = r->head - r->tail;
    }
    uint32_t total_writable = (r->size - 1) - buffer_len(r);
    *out_sz = min(cnt, total_writable);
    return r->buf + r->tail;
}
int buffer_write_commit(buffer_t *r, uint32_t written)
{
    if(r == NULL) return -1;
    uint32_t cnt;
    if(r->head <= r->tail)
    {
        cnt = r->size - r->tail;
    }
    else
    {
        cnt = r->head - r->tail;
    }
    uint32_t total_writable = (r->size - 1) - buffer_len(r);
    uint32_t writable = min(cnt, total_writable);
    if(writable < written) return -1;
    r->tail = (r->tail + written) &(r->size - 1);
    return 0;
}