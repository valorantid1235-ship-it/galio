#ifndef COMMON_H
#define COMMON_H
#ifndef COMMON_H
#define COMMON_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;

#endif
#include <stdint.h>

/* Typedefs for clarity */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t  i32;

/* Kernel utility functions */
void *memset(void *s, int c, u32 n);
void *memcpy(void *dest, const void *src, u32 n);
void panic(const char *msg);

/* NEW: kernel status reporting */
void kernel_status(void);

#endif /* COMMON_H */
