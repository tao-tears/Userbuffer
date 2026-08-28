#ifndef _chain_buffer_h
#define _chain_buffer_h
typedef struct buf_chain_s buf_chain_t;
typedef struct buffer_s buffer_t;

// struct buf_chain_s {
//     struct buf_chain_s *next;
//     uint32_t buffer_len;
//     uint32_t misalign;
//     uint32_t off;
//     uint8_t *buffer;
// };

// struct buffer_s {
//     buf_chain_t *first;
//     buf_chain_t *last;
//     buf_chain_t **last_with_datap;
//     uint32_t total_len;
//     uint32_t last_read_pos; // for sep read
// };
// 创建缓冲区
buffer_t * buffer_new(uint32_t sz);

// 获取缓冲区当前已经存放的数据字节长度
uint32_t buffer_len(buffer_t *buf);

// 释放缓冲区内存
void buffer_free(buffer_t *buf);

// 往缓冲区追加写入数据
int buffer_add(buffer_t *buf, const void *data, uint32_t sz);

// 从缓冲区读出数据，拷贝到data，同时消费（移除）已读字节
int buffer_remove(buffer_t *r, void *data, uint32_t sz);

// 丢弃缓冲区前面sz字节，不拷贝出来，直接消费
int buffer_drain(buffer_t *buf, uint32_t sz);

// 在缓冲区内部查找分隔符sep，返回偏移位置，常用于按行解析TCP粘包
int buffer_search(buffer_t *buf, const char* sep, const uint32_t seplen);

// 获取可写直接指针（零拷贝，拿到空闲段起始地址）
uint8_t * buffer_write_atmost(buffer_t *buf,uint32_t *out_sz);

int buffer_write_commit(buffer_t *buf, uint32_t written);

void buffer_debug_print(buffer_t *buf);

#endif