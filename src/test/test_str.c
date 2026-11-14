#include "test.h"
#include "ek/container/str.h"

EK_LOG_FILE_TAG("test_str.c");

void str_test(void)
{
    EK_LOG_INFO("str test start");
    const size_t unused_start = ek_heap_unused();

    ek_str_t *s = ek_str_create(NULL);
    TEST_ASSERT(s != NULL, "create empty str failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(s), "") == 0, "empty str content mismatch");
    TEST_ASSERT_EQ(ek_str_get_len(s), 0U, "empty str len mismatch");
    ek_str_free(s);

    const char *i_am_a_string = "i am a string";
    s = ek_str_create(i_am_a_string);
    TEST_ASSERT(s != NULL, "create str failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(s), i_am_a_string) == 0, "init str content mismatch");
    ek_str_free(s);

    s = ek_str_create("hello");
    TEST_ASSERT(s != NULL, "create hello str failed");
    TEST_ASSERT(ek_str_append(s, " world!") == 0, "str append failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(s), "hello world!") == 0, "append content mismatch");

    TEST_ASSERT(ek_str_append_fmt(s, "%s", "EmbeddedKit") == 0, "str append_fmt failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(s), "hello world!EmbeddedKit") == 0, "append_fmt content mismatch");

    ek_str_t *slice = ek_str_slice(s, 12, (int32_t)ek_str_get_len(s));
    TEST_ASSERT(slice != NULL, "str slice failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(slice), "EmbeddedKit") == 0, "slice content mismatch");
    ek_str_free(slice);

    ek_str_t *s1 = ek_str_create(" N1netyNine99");
    TEST_ASSERT(s1 != NULL, "create cat src failed");
    TEST_ASSERT(ek_str_cat(s, s1) == 0, "str cat failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(s), "hello world!EmbeddedKit N1netyNine99") == 0, "cat content mismatch");

    ek_str_t *s2 = ek_str_create("abcdefg");
    TEST_ASSERT(s2 != NULL, "create reverse str failed");
    TEST_ASSERT(ek_str_reverse(s2) == 0, "str reverse failed");
    TEST_ASSERT(strcmp(ek_str_get_cstring(s2), "gfedcba") == 0, "reverse content mismatch");

    ek_str_free(s);
    ek_str_free(s1);
    ek_str_free(s2);

    // 参数检查：非法参数返回错误码而非断言
    TEST_ASSERT_EQ(ek_str_get_len(NULL), 0U, "null str len should be 0");
    TEST_ASSERT_EQ(ek_str_get_cap(NULL), 0U, "null str cap should be 0");
    TEST_ASSERT(strcmp(ek_str_get_cstring(NULL), "") == 0, "null str cstring should be empty");
    TEST_ASSERT(ek_str_append_fmt(NULL, "x") == -EINVAL, "append_fmt on null str should fail");
    TEST_ASSERT(ek_str_cat(NULL, NULL) == -EINVAL, "cat with null args should fail");
    ek_str_free(NULL);

    TEST_ASSERT_EQ(ek_heap_unused(), unused_start, "str test leaked heap");
    EK_LOG_INFO("str test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
