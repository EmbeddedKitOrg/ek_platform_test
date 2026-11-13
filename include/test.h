#ifndef TEST_H
#define TEST_H

#include "main.h"

#include "ek_log.h"
#include "ek_heap.h"
#include "ek_export.h"

#define TEST_FAIL(msg)             \
    do                             \
    {                              \
        EK_LOG_ERROR("%s", (msg)); \
        exit(1);                   \
    } while (0)

#define TEST_ASSERT(cond, msg) \
    do                         \
    {                          \
        if (!(cond))           \
        {                      \
            TEST_FAIL(msg);    \
        }                      \
    } while (0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do                            \
    {                             \
        if ((a) != (b))           \
        {                         \
            TEST_FAIL(msg);       \
        }                         \
    } while (0)

#define PI (3.1415926f)

typedef struct
{
    uint8_t name;
    float score;
} student_t;

void picothread_test_init(void);
void ringbuf_test(void);
void stack_test(void);
void str_test(void);
void str_view_test(void);
void vec_test(void);
void snapshot_test(void);
void evoke_test(void);
void vfs_test(void);
void shell_test(void);

#endif // TEST_H
