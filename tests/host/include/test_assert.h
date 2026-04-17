#ifndef TEST_ASSERT_H
#define TEST_ASSERT_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>

extern int g_test_failures;

#define TEST_FAIL_IMPL(file, line, expr, fmt_expected, expected, fmt_actual, actual) \
  do { \
    ++g_test_failures; \
    printf("%s:%d: assertion failed: %s, expected=" fmt_expected ", actual=" fmt_actual "\n", \
           (file), (line), (expr), (expected), (actual)); \
  } while (0)

#define TEST_ASSERT_TRUE(expr) \
  do { \
    if (!(expr)) { \
      ++g_test_failures; \
      printf("%s:%d: assertion failed: %s\n", __FILE__, __LINE__, #expr); \
    } \
  } while (0)

#define TEST_ASSERT_EQ_INT(expected, actual) \
  do { \
    int test_expected_ = (int)(expected); \
    int test_actual_ = (int)(actual); \
    if (test_expected_ != test_actual_) { \
      TEST_FAIL_IMPL(__FILE__, __LINE__, #actual, "%d", test_expected_, "%d", test_actual_); \
    } \
  } while (0)

#define TEST_ASSERT_EQ_U32(expected, actual) \
  do { \
    uint32_t test_expected_ = (uint32_t)(expected); \
    uint32_t test_actual_ = (uint32_t)(actual); \
    if (test_expected_ != test_actual_) { \
      TEST_FAIL_IMPL(__FILE__, __LINE__, #actual, "%lu", (unsigned long)test_expected_, "%lu", (unsigned long)test_actual_); \
    } \
  } while (0)

#define TEST_ASSERT_EQ_FLOAT(expected, actual, eps) \
  do { \
    float test_expected_ = (float)(expected); \
    float test_actual_ = (float)(actual); \
    float test_eps_ = (float)(eps); \
    if (fabsf(test_expected_ - test_actual_) > test_eps_) { \
      TEST_FAIL_IMPL(__FILE__, __LINE__, #actual, "%f", (double)test_expected_, "%f", (double)test_actual_); \
    } \
  } while (0)

#endif /* TEST_ASSERT_H */
