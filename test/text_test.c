#include "text.h"

// TODO: Show error pretty error message when a test fails
enum text_test_error {
  TEXT_TEST_ERROR_NONE = 0,
  TEXT_TEST_ERROR_STRING_FROM_ZERO_TERMINATED,
  TEXT_TEST_ERROR_STRING_FROM_ZERO_TERMINATED_TRUNCATED,
  TEXT_TEST_ERROR_IS_STRING_EQUAL_MUST_BE_TRUE,
  TEXT_TEST_ERROR_IS_STRING_EQUAL_MUST_BE_FALSE,
  TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_TRUE_1,
  TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_TRUE_2,
  TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_TRUE_3,
  TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_FALSE_1,
  TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_FALSE_2,
  TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_TRUE,
  TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_1,
  TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_2,
  TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_3,
  TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_4,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_1NS,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_1SEC,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_5SEC,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_7MIN,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_1HR5MIN,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_10DAY,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_10DAY1SEC,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_NULL,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_EMPTY,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_SPACE,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_NO_DURATION_STRING,
  TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_WRONG_DURATION_NAMES,
  TEXT_TEST_ERROR_IS_DURATION_LESS_THAN_EXPECTED_TRUE,
  TEXT_TEST_ERROR_IS_DURATION_LESS_THAN_EXPECTED_FALSE,
  TEXT_TEST_ERROR_IS_DURATION_GRATER_THAN_EXPECTED_TRUE,
  TEXT_TEST_ERROR_IS_DURATION_GRATER_THAN_EXPECTED_FALSE,
  TEXT_TEST_ERROR_FORMATU64_EXPECTED_0,
  TEXT_TEST_ERROR_FORMATU64_EXPECTED_1,
  TEXT_TEST_ERROR_FORMATU64_EXPECTED_10,
  TEXT_TEST_ERROR_FORMATU64_EXPECTED_3912,
  TEXT_TEST_ERROR_FORMATU64_EXPECTED_18446744073709551615,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_0_9,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_1_0,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_1_00,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_9_05,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_2_50,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_2_55,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_0_9,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_1_0,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_1_00,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_2_50,
  TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_2_55,

  // src: https://mesonbuild.com/Unit-tests.html#skipped-tests-and-hard-errors
  // For the default exitcode testing protocol, the GNU standard approach in this case is to exit the program with error
  // code 77. Meson will detect this and report these tests as skipped rather than failed. This behavior was added in
  // version 0.37.0.
  MESON_TEST_SKIP = 77,
  // In addition, sometimes a test fails set up so that it should fail even if it is marked as an expected failure. The
  // GNU standard approach in this case is to exit the program with error code 99. Again, Meson will detect this and
  // report these tests as ERROR, ignoring the setting of should_fail. This behavior was added in version 0.50.0.
  MESON_TEST_FAILED_TO_SET_UP = 99,
};

int
main(void)
{
  enum text_test_error errorCode = TEXT_TEST_ERROR_NONE;

  // StringFromZeroTerminated
  {
    char *input = "abc";
    struct string result = StringFromZeroTerminated((u8 *)input, 1024);

    char *expectedValue = input;
    u64 expectedLength = 3;
    if ((char *)result.value != expectedValue || result.length != expectedLength) {
      errorCode = TEXT_TEST_ERROR_STRING_FROM_ZERO_TERMINATED;
      goto end;
    }
  }

  {
    char *input = "abcdefghijklm";
    struct string result = StringFromZeroTerminated((u8 *)input, 3);

    char *expectedValue = input;
    u64 expectedLength = 3;
    if (result.length != expectedLength || result.value != (u8 *)expectedValue) {
      errorCode = TEXT_TEST_ERROR_STRING_FROM_ZERO_TERMINATED_TRUNCATED;
      goto end;
    }
  }

  // IsStringEqual(struct string *left, struct string *right)
  {
    struct string leftString;
    struct string rightString;
    b8 expected;

    leftString = STRING_FROM_ZERO_TERMINATED("abc");
    rightString = STRING_FROM_ZERO_TERMINATED("abc");
    expected = 1;
    if (IsStringEqual(&leftString, &rightString) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_EQUAL_MUST_BE_TRUE;
      goto end;
    }

    rightString = STRING_FROM_ZERO_TERMINATED("abc def ghi");
    expected = 0;
    if (IsStringEqual(&leftString, &rightString) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_EQUAL_MUST_BE_FALSE;
      goto end;
    }
  }

  // IsStringContains(struct string *string, struct string *search)
  {
    struct string string;
    struct string search;
    b8 expected;

    string = STRING_FROM_ZERO_TERMINATED("abc def ghi");
    search = STRING_FROM_ZERO_TERMINATED("abc");
    expected = 1;
    if (IsStringContains(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_TRUE_1;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("def");
    if (IsStringContains(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_TRUE_2;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("ghi");
    if (IsStringContains(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_TRUE_3;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("ghijkl");
    expected = 0;
    if (IsStringContains(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_FALSE_1;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("jkl");
    if (IsStringContains(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_CONTAINS_EXPECTED_FALSE_2;
      goto end;
    }
  }

  // IsStringStartsWith(struct string *string, struct string *search)
  {
    struct string string;
    struct string search;
    b8 expected;

    string = STRING_FROM_ZERO_TERMINATED("abc def ghi");
    search = STRING_FROM_ZERO_TERMINATED("abc");
    expected = 1;
    if (IsStringStartsWith(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_TRUE;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("def");
    expected = 0;
    if (IsStringStartsWith(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_1;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("ghi");
    if (IsStringStartsWith(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_2;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("ghijkl");
    if (IsStringStartsWith(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_3;
      goto end;
    }

    search = STRING_FROM_ZERO_TERMINATED("jkl");
    if (IsStringStartsWith(&string, &search) != expected) {
      errorCode = TEXT_TEST_ERROR_IS_STRING_STARTS_WITH_EXPECTED_FALSE_4;
      goto end;
    }
  }

  // ParseDuration(struct string *string, struct duration *duration)
  {
    struct string string;
    struct duration duration;
    b8 value;
    b8 expected;
    u64 expectedDurationInNs;

    string = STRING_FROM_ZERO_TERMINATED("1ns");
    expected = 1;
    expectedDurationInNs = 1;
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_1NS;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("1sec");
    expected = 1;
    expectedDurationInNs = 1 * 1e9L;
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_1SEC;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("5sec");
    expectedDurationInNs = 5 * 1e9L;
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_5SEC;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("7min");
    expectedDurationInNs = 7 * 1e9L * 60;
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_7MIN;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("1hr5min");
    expectedDurationInNs = (1 * 1e9L * 60 * 60) + (5 * 1e9L * 60);
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_1HR5MIN;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("10day");
    expectedDurationInNs = 10 * 1e9L * 60 * 60 * 24;
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_10DAY;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("10day1sec");
    expectedDurationInNs = (10 * 1e9L * 60 * 60 * 24) + (1 * 1e9L);
    value = ParseDuration(&string, &duration);
    if (value != expected || duration.ns != expectedDurationInNs) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_TRUE_10DAY1SEC;
      goto end;
    }

    string = (struct string){};
    expected = 0;
    value = ParseDuration(&string, &duration);
    if (value != expected) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_NULL;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("");
    value = ParseDuration(&string, &duration);
    if (value != expected) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_EMPTY;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED(" ");
    value = ParseDuration(&string, &duration);
    if (value != expected) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_SPACE;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("abc");
    value = ParseDuration(&string, &duration);
    if (value != expected) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_NO_DURATION_STRING;
      goto end;
    }

    string = STRING_FROM_ZERO_TERMINATED("5m5s");
    value = ParseDuration(&string, &duration);
    if (value != expected) {
      errorCode = TEXT_TEST_ERROR_PARSE_DURATION_EXPECTED_FALSE_WRONG_DURATION_NAMES;
      goto end;
    }
  }

  // IsDurationLessThan(struct duration *left, struct duration *right)
  // IsDurationGraterThan(struct duration *left, struct duration *right)
  {
    struct duration left;
    struct duration right;
    b8 expectedLessThan;
    b8 expectedGraterThan;

    left = (struct duration){.ns = 1 * 1e9L};
    right = (struct duration){.ns = 5 * 1e9L};
    expectedLessThan = 1;
    if (IsDurationLessThan(&left, &right) != expectedLessThan) {
      errorCode = TEXT_TEST_ERROR_IS_DURATION_LESS_THAN_EXPECTED_TRUE;
      goto end;
    }
    expectedGraterThan = 0;
    if (IsDurationGraterThan(&left, &right) != expectedGraterThan) {
      errorCode = TEXT_TEST_ERROR_IS_DURATION_GRATER_THAN_EXPECTED_FALSE;
      goto end;
    }

    left = (struct duration){.ns = 1 * 1e9L};
    right = (struct duration){.ns = 1 * 1e9L};
    expectedLessThan = 0;
    if (IsDurationLessThan(&left, &right) != expectedLessThan) {
      errorCode = TEXT_TEST_ERROR_IS_DURATION_LESS_THAN_EXPECTED_FALSE;
      goto end;
    }
    expectedGraterThan = 0;
    if (IsDurationGraterThan(&left, &right) != expectedGraterThan) {
      errorCode = TEXT_TEST_ERROR_IS_DURATION_GRATER_THAN_EXPECTED_FALSE;
      goto end;
    }

    left = (struct duration){.ns = 5 * 1e9L};
    right = (struct duration){.ns = 1 * 1e9L};
    expectedLessThan = 0;
    if (IsDurationLessThan(&left, &right) != expectedLessThan) {
      errorCode = TEXT_TEST_ERROR_IS_DURATION_LESS_THAN_EXPECTED_FALSE;
      goto end;
    }
    expectedGraterThan = 1;
    if (IsDurationGraterThan(&left, &right) != expectedGraterThan) {
      errorCode = TEXT_TEST_ERROR_IS_DURATION_GRATER_THAN_EXPECTED_TRUE;
      goto end;
    }
  }

  // FormatU64(struct string *stringBuffer, u64 value)
  {
    u8 buf[20];
    struct string stringBuffer = {.value = buf, .length = sizeof(buf)};
    struct string expected;
    struct string value;

    value = FormatU64(&stringBuffer, 0);
    expected = STRING_FROM_ZERO_TERMINATED("0");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATU64_EXPECTED_0;
      goto end;
    }

    value = FormatU64(&stringBuffer, 1);
    expected = STRING_FROM_ZERO_TERMINATED("1");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATU64_EXPECTED_1;
      goto end;
    }

    value = FormatU64(&stringBuffer, 10);
    expected = STRING_FROM_ZERO_TERMINATED("10");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATU64_EXPECTED_10;
      goto end;
    }

    value = FormatU64(&stringBuffer, 3912);
    expected = STRING_FROM_ZERO_TERMINATED("3912");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATU64_EXPECTED_3912;
      goto end;
    }

    value = FormatU64(&stringBuffer, 18446744073709551615UL);
    expected = STRING_FROM_ZERO_TERMINATED("18446744073709551615");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATU64_EXPECTED_18446744073709551615;
      goto end;
    }
  }

  // FormatF32(struct string *stringBuffer, f32 value, u32 fractionCount)
  {
    u8 buf[20];
    struct string stringBuffer = {.value = buf, .length = sizeof(buf)};
    struct string expected;
    struct string value;

    value = FormatF32(&stringBuffer, 0.99f, 1);
    expected = STRING_FROM_ZERO_TERMINATED("0.9");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_0_9;
      goto end;
    }

    value = FormatF32(&stringBuffer, 1.0f, 1);
    expected = STRING_FROM_ZERO_TERMINATED("1.0");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_1_0;
      goto end;
    }

    value = FormatF32(&stringBuffer, 1.0f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("1.00");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_1_00;
      goto end;
    }

    value = FormatF32(&stringBuffer, 9.05f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("9.05");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_9_05;
      goto end;
    }

    value = FormatF32(&stringBuffer, 2.50f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("2.50");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_2_50;
      goto end;
    }

    value = FormatF32(&stringBuffer, 2.55999f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("2.55");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_2_55;
      goto end;
    }

    value = FormatF32(&stringBuffer, -0.99f, 1);
    expected = STRING_FROM_ZERO_TERMINATED("-0.9");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_0_9;
      goto end;
    }

    value = FormatF32(&stringBuffer, -1.0f, 1);
    expected = STRING_FROM_ZERO_TERMINATED("-1.0");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_1_0;
      goto end;
    }

    value = FormatF32(&stringBuffer, -1.0f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("-1.00");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_1_00;
      goto end;
    }

    value = FormatF32(&stringBuffer, -2.50f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("-2.50");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_2_50;
      goto end;
    }

    value = FormatF32(&stringBuffer, -2.55999f, 2);
    expected = STRING_FROM_ZERO_TERMINATED("-2.55");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = TEXT_TEST_ERROR_FORMATF32_EXPECTED_NEGATIVE_2_55;
      goto end;
    }
  }

  // FormatHex(struct string *stringBuffer, u64 value)
  {
    u8 buf[18];
    struct string stringBuffer = {.value = buf, .length = sizeof(buf)};
    struct string expected;
    struct string value;

    value = FormatHex(&stringBuffer, 0x0);
    expected = STRING_FROM_ZERO_TERMINATED("0x00");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = -1;
      goto end;
    }

    value = FormatHex(&stringBuffer, 0x4);
    expected = STRING_FROM_ZERO_TERMINATED("0x04");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = -1;
      goto end;
    }

    value = FormatHex(&stringBuffer, 0x00f2aa499b9028eaul);
    expected = STRING_FROM_ZERO_TERMINATED("0x00f2aa499b9028ea");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = -1;
      goto end;
    }

    value = FormatHex(&stringBuffer, 0x00f2aa499b9028eaul);
    expected = STRING_FROM_ZERO_TERMINATED("0x00f2aa499b9028ea");
    if (!IsStringEqual(&value, &expected)) {
      errorCode = -1;
      goto end;
    }
  }

end:
  return (int)errorCode;
}
