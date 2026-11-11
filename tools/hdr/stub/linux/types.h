#include <stdint.h>
#include <stdbool.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
typedef u16 __le16; typedef u32 __le32;
#define BIT(n) (1UL<<(n))
#define LINUX_VERSION_CODE 0
#define KERNEL_VERSION(a,b,c) 1
