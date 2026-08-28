#ifndef BUFFER_H
#define BUFFER_H

#include <unistd.h>
#include <stdint.h>

typedef struct ringbuffer_s buffer_t;

// 创建环形缓冲区
buffer_t * buffer_new(uint32_t sz);

// 获取缓冲区当前已经存放的数据字节长度
uint32_t buffer_len(buffer_t *r);

// 释放缓冲区内存
void buffer_free(buffer_t *r);

// 往缓冲区追加写入数据
int buffer_add(buffer_t *r, const void *data, uint32_t sz);

// 从缓冲区读出数据，拷贝到data，同时消费（移除）已读字节
int buffer_remove(buffer_t *r, void *data, uint32_t sz);

// 丢弃缓冲区前面sz字节，不拷贝出来，直接消费
int buffer_drain(buffer_t *r, uint32_t sz);

// 在缓冲区内部查找分隔符sep，返回偏移位置，常用于按行解析TCP粘包
int buffer_search(buffer_t *r, const char* sep, const uint32_t seplen);

// 获取可写直接指针（零拷贝，拿到空闲段起始地址）
uint8_t * buffer_write_atmost(buffer_t *r,uint32_t *out_sz);

int buffer_write_commit(buffer_t *r, uint32_t written);

void buffer_debug_print(buffer_t *rb);
#endif