#include "test.h"
#include "ek/container/str_view.h"

EK_LOG_FILE_TAG("test_str_view.c");

void str_view_test(void)
{
    EK_LOG_INFO("str_view test start");
    const size_t unused_start = ek_heap_unused();

    const char *src = "hello world";
    ek_str_view_t sv = ek_str_view_create(src);
    TEST_ASSERT(sv.str == src, "create str pointer mismatch");
    TEST_ASSERT_EQ(sv.str_len, 11U, "create str len mismatch");

    // chop_left 从左侧切掉 n 个字符
    TEST_ASSERT(ek_str_view_chop_left(&sv, 6) == 0, "chop_left failed");
    TEST_ASSERT(sv.str == src + 6, "chop_left pointer mismatch");
    TEST_ASSERT_EQ(sv.str_len, 5U, "chop_left len mismatch");
    TEST_ASSERT(strncmp(sv.str, "world", 5) == 0, "chop_left content mismatch");

    // 超过长度时按全部切掉处理
    TEST_ASSERT(ek_str_view_chop_left(&sv, 100) == 0, "chop_left overflow failed");
    TEST_ASSERT_EQ(sv.str_len, 0U, "chop_left overflow len mismatch");
    TEST_ASSERT(ek_str_view_chop_left(NULL, 1) == -EINVAL, "chop_left NULL should fail");

    // chop_right 从右侧切掉 n 个字符
    sv = ek_str_view_create(src);
    TEST_ASSERT(ek_str_view_chop_right(&sv, 6) == 0, "chop_right failed");
    TEST_ASSERT(sv.str == src, "chop_right pointer should not move");
    TEST_ASSERT_EQ(sv.str_len, 5U, "chop_right len mismatch");
    TEST_ASSERT(strncmp(sv.str, "hello", 5) == 0, "chop_right content mismatch");

    TEST_ASSERT(ek_str_view_chop_right(&sv, 100) == 0, "chop_right overflow failed");
    TEST_ASSERT_EQ(sv.str_len, 0U, "chop_right overflow len mismatch");
    TEST_ASSERT(ek_str_view_chop_right(NULL, 1) == -EINVAL, "chop_right NULL should fail");

    // trim 去掉两端空白
    sv = ek_str_view_create(" \t hello world \r\n");
    TEST_ASSERT(ek_str_view_trim(&sv) == 0, "trim failed");
    TEST_ASSERT_EQ(sv.str_len, 11U, "trim len mismatch");
    TEST_ASSERT(strncmp(sv.str, "hello world", 11) == 0, "trim content mismatch");

    sv = ek_str_view_create("no-space");
    TEST_ASSERT(ek_str_view_trim(&sv) == 0, "trim plain failed");
    TEST_ASSERT_EQ(sv.str_len, 8U, "trim plain len mismatch");

    sv = ek_str_view_create("    ");
    TEST_ASSERT(ek_str_view_trim(&sv) == 0, "trim all-space failed");
    TEST_ASSERT_EQ(sv.str_len, 0U, "trim all-space len mismatch");
    TEST_ASSERT(ek_str_view_trim(NULL) == -EINVAL, "trim NULL should fail");

    // split 按分隔符逐段切分
    sv = ek_str_view_create("a,bb,ccc");
    ek_str_view_t part = ek_str_view_split(&sv, ',');
    TEST_ASSERT_EQ(part.str_len, 1U, "split 1st len mismatch");
    TEST_ASSERT(strncmp(part.str, "a", 1) == 0, "split 1st content mismatch");

    part = ek_str_view_split(&sv, ',');
    TEST_ASSERT_EQ(part.str_len, 2U, "split 2nd len mismatch");
    TEST_ASSERT(strncmp(part.str, "bb", 2) == 0, "split 2nd content mismatch");

    // 最后一段没有分隔符，返回剩余内容并清空原视图
    part = ek_str_view_split(&sv, ',');
    TEST_ASSERT_EQ(part.str_len, 3U, "split 3rd len mismatch");
    TEST_ASSERT(strncmp(part.str, "ccc", 3) == 0, "split 3rd content mismatch");
    TEST_ASSERT_EQ(sv.str_len, 0U, "split rest should be cleared");

    // 开头的空段
    sv = ek_str_view_create(",x");
    part = ek_str_view_split(&sv, ',');
    TEST_ASSERT_EQ(part.str_len, 0U, "split empty head len mismatch");
    part = ek_str_view_split(&sv, ',');
    TEST_ASSERT_EQ(part.str_len, 1U, "split tail len mismatch");
    TEST_ASSERT(strncmp(part.str, "x", 1) == 0, "split tail content mismatch");

    TEST_ASSERT_EQ(ek_heap_unused(), unused_start, "str_view test used heap");
    EK_LOG_INFO("str_view test passed, unused heap:%u", (unsigned)ek_heap_unused());
}
