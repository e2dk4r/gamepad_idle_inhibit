#pragma once

#include "assert.h"
#include "math.h"
#include "type.h"

struct string {
  u8 *value;
  u64 length;
};

#define STRING_FROM_ZERO_TERMINATED(src)                                                                               \
  ((struct string){                                                                                                    \
      .value = (u8 *)src,                                                                                              \
      .length = sizeof(src) - 1,                                                                                       \
  })

static inline struct string
StringFromZeroTerminated(u8 *src, u64 max)
{
  debug_assert(src != 0);
  struct string string = {};

  string.value = src;

  while (*src) {
    string.length++;
    if (string.length == max)
      break;
    src++;
  }

  return string;
}

static inline b8
IsStringEqual(struct string *left, struct string *right)
{
  if (!left || !right || left->length != right->length)
    return 0;

  for (u64 index = 0; index < left->length; index++) {
    if (left->value[index] != right->value[index])
      return 0;
  }

  return 1;
}

static inline b8
IsStringContains(struct string *string, struct string *search)
{
  if (!string || !search || string->length < search->length)
    return 0;

  for (u64 stringIndex = 0; stringIndex < string->length; stringIndex++) {
    b8 isFound = 1;
    for (u64 searchIndex = 0, substringIndex = stringIndex; searchIndex < search->length;
         searchIndex++, substringIndex++) {
      b8 isEndOfString = substringIndex == string->length;
      if (isEndOfString) {
        isFound = 0;
        break;
      }

      b8 isCharactersNotMatching = string->value[substringIndex] != search->value[searchIndex];
      if (isCharactersNotMatching) {
        isFound = 0;
        break;
      }
    }

    if (isFound)
      return 1;
  }

  return 0;
}

static inline b8
IsStringStartsWith(struct string *string, struct string *search)
{
  if (!string || !search || string->length < search->length)
    return 0;

  for (u64 searchIndex = 0; searchIndex < search->length; searchIndex++) {
    b8 isCharactersNotMatching = string->value[searchIndex] != search->value[searchIndex];
    if (isCharactersNotMatching)
      return 0;
  }

  return 1;
}

struct duration {
  u64 ns;
};

#define DURATION_IN_SECONDS(seconds) ((struct duration){.ns = (seconds) * 1e9L})
#define DURATION_IN_DAYS(days) ((struct duration){.ns = 1e9L * 60 * 60 * 24 * days})
static inline b8
ParseDuration(struct string *string, struct duration *duration)
{
  if (!string || string->length == 0 || string->length < 3)
    return 0;

    // | Duration | Length      |
    // |----------|-------------|
    // | ns       | nanosecond  |
    // | us       | microsecond |
    // | ms       | millisecond |
    // | sec      | second      |
    // | min      | minute      |
    // | hr       | hour        |
    // | day      | day         |
    // | wk       | week        |

#define UNIT_STRING(variableName, zeroTerminatedString)                                                                \
  static struct string variableName = {                                                                                \
      .value = (u8 *)zeroTerminatedString,                                                                             \
      .length = sizeof(zeroTerminatedString) - 1,                                                                      \
  }
  UNIT_STRING(nanosecondUnitString, "ns");
  UNIT_STRING(microsecondUnitString, "us");
  UNIT_STRING(millisocondUnitString, "ms");
  UNIT_STRING(secondUnitString, "sec");
  UNIT_STRING(minuteUnitString, "min");
  UNIT_STRING(hourUnitString, "hr");
  UNIT_STRING(dayUnitString, "day");
  UNIT_STRING(weekUnitString, "wk");
#undef UNIT_STRING

  b8 isUnitExistsInString =
      IsStringContains(string, &secondUnitString) || IsStringContains(string, &minuteUnitString) ||
      IsStringContains(string, &hourUnitString) || IsStringContains(string, &nanosecondUnitString) ||
      IsStringContains(string, &microsecondUnitString) || IsStringContains(string, &millisocondUnitString) ||
      IsStringContains(string, &dayUnitString) || IsStringContains(string, &weekUnitString);
  if (!isUnitExistsInString) {
    return 0;
  }

  struct duration parsed = {};
  u64 value = 0;
  for (u64 index = 0; index < string->length; index++) {
    u8 digitCharacter = string->value[index];
    b8 isDigit = digitCharacter >= '0' && digitCharacter <= '9';
    if (!isDigit) {
      // - get unit
      struct string unitString = {.value = string->value + index, .length = string->length - index};
      if (/* unit: nanosecond */ IsStringStartsWith(&unitString, &nanosecondUnitString)) {
        parsed.ns += value;
        index += nanosecondUnitString.length - 1;
      } else if (/* unit: microsecond */ IsStringStartsWith(&unitString, &microsecondUnitString)) {
        parsed.ns += value * 1e3L;
        index += microsecondUnitString.length - 1;
      } else if (/* unit: millisecond */ IsStringStartsWith(&unitString, &millisocondUnitString)) {
        parsed.ns += value * 1e6L;
        index += millisocondUnitString.length - 1;
      } else if (/* unit: second */ IsStringStartsWith(&unitString, &secondUnitString)) {
        parsed.ns += value * 1e9L;
        index += secondUnitString.length - 1;
      } else if (/* unit: minute */ IsStringStartsWith(&unitString, &minuteUnitString)) {
        parsed.ns += value * 1e9L * 60;
        index += minuteUnitString.length;
      } else if (/* unit: hour */ IsStringStartsWith(&unitString, &hourUnitString)) {
        parsed.ns += value * 1e9L * 60 * 60;
        index += hourUnitString.length - 1;
      } else if (/* unit: day */ IsStringStartsWith(&unitString, &dayUnitString)) {
        parsed.ns += value * 1e9L * 60 * 60 * 24;
        index += dayUnitString.length - 1;
      } else if (/* unit: week */ IsStringStartsWith(&unitString, &weekUnitString)) {
        parsed.ns += value * 1e9L * 60 * 60 * 24 * 7;
        index += weekUnitString.length - 1;
      } else {
        // unsupported unit
        return 0;
      }

      // - reset value
      value = 0;
      continue;
    }

    value *= 10;
    u8 digit = digitCharacter - (u8)'0';
    value += digit;
  }

  *duration = parsed;

  return 1;
}

static inline b8
IsDurationLessThan(struct duration *left, struct duration *right)
{
  return left->ns < right->ns;
}

static inline b8
IsDurationGraterThan(struct duration *left, struct duration *right)
{
  return left->ns > right->ns;
}

static inline b8
ParseU64(struct string *string, u64 *value)
{
  // max u64: 18446744073709551615
  if (!string || string->length > 20)
    return 0;

  u64 parsed = 0;
  for (u64 index = 0; index < string->length; index++) {
    u8 digitCharacter = string->value[index];
    b8 isDigit = digitCharacter >= '0' && digitCharacter <= '9';
    if (!isDigit) {
      return 0;
    }

    parsed *= 10;
    u8 digit = digitCharacter - (u8)'0';
    parsed += digit;
  }

  *value = parsed;
  return 1;
}

/*
 * string buffer must at least able to hold 1 bytes, at most 20 bytes.
 */
static inline struct string
FormatU64(struct string *stringBuffer, u64 value)
{
  struct string result = {};
  if (!stringBuffer || stringBuffer->length == 0)
    return result;

  // max u64: 18446744073709551615
  static const u64 powersOf10[20] = {
      1e00L, 1e01L, 1e02L, 1e03L, 1e04L, 1e05L, 1e06L, 1e07L, 1e08L, 1e09L,
      1e10L, 1e11L, 1e12L, 1e13L, 1e14L, 1e15L, 1e16L, 1e17L, 1e18L, 1e19L,
  };
  u64 countOfDigits = 1;
  while (countOfDigits < 20 && value >= powersOf10[countOfDigits])
    countOfDigits++;

  if (countOfDigits > stringBuffer->length)
    return result;

  u64 index = 0;
  while (countOfDigits > 0) {
    u64 power = powersOf10[countOfDigits - 1];
    u64 digit = value / power;

    // turn digit into character
    stringBuffer->value[index] = digit + (u8)'0';

    value -= digit * power;

    index++;
    countOfDigits--;
  }

  result.value = stringBuffer->value;
  result.length = index; // written digits
  return result;
}

static inline struct string
FormatS64(struct string *stringBuffer, s64 value)
{
  struct string result = {};
  if (!stringBuffer || stringBuffer->length == 0)
    return result;

  b8 isNegativeValue = value < 0;
  if (isNegativeValue) {
    value *= -1;
    stringBuffer->value[0] = '-';
    stringBuffer->value += 1;
    stringBuffer->length -= 1;
  }

  result = FormatU64(stringBuffer, value);
  return result;
}

/*
 * string buffer must at least able to hold 3 bytes.
 * fractionCount [1,8]
 */
static inline struct string
FormatF32(struct string *stringBuffer, f32 value, u32 fractionCount)
{
  debug_assert(fractionCount >= 1 && fractionCount <= 8);

  struct string result = {};
  if (!stringBuffer || stringBuffer->length <= 3)
    return result;

  // 1 - convert integer part to string
  // assume value: 10.123
  //        integerValue: 10
  struct string stringBufferForInteger = {
      .value = stringBuffer->value,
      .length = stringBuffer->length,
  };

  b8 isNegativeValue = value < 0;
  if (isNegativeValue) {
    value *= -1;
    stringBufferForInteger.value[0] = '-';
    stringBufferForInteger.value += 1;
    stringBufferForInteger.length -= 1;
  }

  u32 integerValue = (u32)value;
  struct string integerString = FormatU64(&stringBufferForInteger, (u64)integerValue);
  if (integerString.length == 0)
    return result;

  // 2 - insert point
  stringBufferForInteger.value[integerString.length] = '.';

  // 3 - convert fraction to string
  struct string stringBufferForFraction = {
      .value = stringBufferForInteger.value + integerString.length + 1,
      .length = stringBufferForInteger.length - (integerString.length + 1),
  };

  // assume fractionCount = 2
  //        0.123 = 10.123 - 10.000
  //        12.30 =  0.123 * (10 ^ fractionCount)
  //        12    = (int)12.30
  u64 fractionMultiplier = 10;
  for (u32 fractionIndex = 1; fractionIndex < fractionCount; fractionIndex++)
    fractionMultiplier *= 10;

  u32 fractionValue = (u32)((f32)(value - (f32)integerValue) * (f32)fractionMultiplier);

  /*
   * 0.05f
   *   └── put zeros before putting fraction value
   */
  for (u32 m = fractionMultiplier / 10; fractionValue < m; m /= 10) {
    *stringBufferForFraction.value = '0';
    stringBufferForFraction.value++;
    stringBufferForFraction.length--;
  }

  struct string fractionString = FormatU64(&stringBufferForFraction, fractionValue);
  if (fractionString.length == 0)
    return result;

  /*
   * 0.50f
   *    └── put zeros after putting fraction value
   */
  while (fractionString.length < fractionCount) {
    fractionString.value[fractionString.length] = '0';
    fractionString.length++;
  }

  result.value = stringBuffer->value;
  result.length = isNegativeValue + integerString.length + 1 + fractionString.length;
  return result;
}

/*
 *
 * Converts unsigned 64-bit integer to hex string.
 *
 * @param stringBuffer needs at least 18 bytes
 * @return sub string from stringBuffer, returns 0 on string.value on failure
 *
 * @note Adapted from https://github.com/jart/cosmopolitan/blob/master/libc/intrin/formathex64.c
 * @copyright
 * ╒══════════════════════════════════════════════════════════════════════════════╕
 * │ Copyright 2023 Justine Alexandra Roberts Tunney                              │
 * │                                                                              │
 * │ Permission to use, copy, modify, and/or distribute this software for         │
 * │ any purpose with or without fee is hereby granted, provided that the         │
 * │ above copyright notice and this permission notice appear in all copies.      │
 * │                                                                              │
 * │ THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL                │
 * │ WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED                │
 * │ WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE             │
 * │ AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL         │
 * │ DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR        │
 * │ PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER               │
 * │ TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR             │
 * │ PERFORMANCE OF THIS SOFTWARE.                                                │
 * └──────────────────────────────────────────────────────────────────────────────┘
 */
static inline struct string
FormatHex(struct string *stringBuffer, u64 value)
{
  struct string result = {};
  if (!stringBuffer || stringBuffer->length < 18)
    return result;

  if (value == 0) {
    // edge case 0x00
    u8 *digit = stringBuffer->value;
    *digit++ = '0';
    *digit++ = 'x';
    *digit++ = '0';
    *digit++ = '0';
    result.value = stringBuffer->value;
    result.length = 4;
    return result;
  }

  u64 index = 0;
  stringBuffer->value[index] = '0';
  index++;
  stringBuffer->value[index] = 'x';
  index++;

  // 1 - pick good width
  u64 width;
  {
    u8 n = bsrl(value);
    if (n < 16) {
      if (n < 8)
        width = 8;
      else
        width = 16;
    } else {
      if (n < 32)
        width = 32;
      else
        width = 64;
    }
  }

  // 2 - turn value into hex
  do {
    width -= 4;
    stringBuffer->value[index] = "0123456789abcdef"[(value >> width) & 15];
    index++;
  } while (width);

  result.value = stringBuffer->value;
  result.length = index;
  return result;
}
