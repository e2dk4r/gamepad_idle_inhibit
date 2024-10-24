#ifndef __TEXT_H__
#define __TEXT_H__

struct string {
  u8 *data;
  u64 len;
};

static inline struct string
StringFromZeroTerminated(u8 *string)
{
  debug_assert(string != 0);
  u64 len = strnlen((char *)string, 1024);
  return (struct string){
      .data = string,
      .len = len,
  };
}

static inline b8
IsStringEqual(struct string *left, struct string *right)
{
  if (!left || !right || left->len != right->len)
    return 0;

  for (u64 index = 0; index < left->len; index++) {
    if (left->data[index] != right->data[index])
      return 0;
  }

  return 1;
}

static inline b8
IsStringContains(struct string *string, struct string *search)
{
  if (!string || !search || string->len < search->len)
    return 0;

  /* TEST CODE
  struct string string;
  struct string search;
  b8 result;

  string = (struct string){.data = (u8 *)"abc def ghi", .len = 11};
  search = (struct string){.data = (u8 *)"abc", .len = 3};
  debug_assert(IsStringContains(&string, &search));

  search = (struct string){.data = (u8 *)"def", .len = 3};
  debug_assert(IsStringContains(&string, &search));

  search = (struct string){.data = (u8 *)"ghi", .len = 3};
  debug_assert(IsStringContains(&string, &search));

  search = (struct string){.data = (u8 *)"ghijkl", .len = 6};
  debug_assert(!IsStringContains(&string, &search));

  search = (struct string){.data = (u8 *)"jkl", .len = 3};
  debug_assert(!IsStringContains(&string, &search));
  */

  for (u64 stringIndex = 0; stringIndex < string->len; stringIndex++) {
    b8 isFound = 1;
    for (u64 searchIndex = 0, substringIndex = stringIndex; searchIndex < search->len;
         searchIndex++, substringIndex++) {
      b8 isEndOfString = substringIndex == string->len;
      if (isEndOfString) {
        isFound = 0;
        break;
      }

      b8 isCharactersNotMatching = string->data[substringIndex] != search->data[searchIndex];
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
  if (!string || !search || string->len < search->len)
    return 0;

  /* TEST CODE
    struct string string;
    struct string search;

    string = (struct string){.data = (u8 *)"abc def ghi", .len = 11};
    search = (struct string){.data = (u8 *)"abc", .len = 3};
    debug_assert(IsStringStartsWith(&string, &search));

    search = (struct string){.data = (u8 *)"def", .len = 3};
    debug_assert(!IsStringStartsWith(&string, &search));

    search = (struct string){.data = (u8 *)"ghi", .len = 3};
    debug_assert(!IsStringStartsWith(&string, &search));

    search = (struct string){.data = (u8 *)"ghijkl", .len = 6};
    debug_assert(!IsStringStartsWith(&string, &search));

    search = (struct string){.data = (u8 *)"jkl", .len = 3};
    debug_assert(!IsStringStartsWith(&string, &search));
  */

  for (u64 searchIndex = 0; searchIndex < search->len; searchIndex++) {
    b8 isCharactersNotMatching = string->data[searchIndex] != search->data[searchIndex];
    if (isCharactersNotMatching)
      return 0;
  }

  return 1;
}

struct duration {
  u64 ns;
};

static inline b8
ParseDuration(struct string *string, struct duration *duration)
{
  if (!string || string->len == 0 || string->len < 3)
    return 0;

    /* TEST CODE
    struct string string;
    struct duration duration;
    b8 isParsingSuccessful;

    string = (struct string){.data = (u8 *)"1ns", .len = 3};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(isParsingSuccessful && duration.ns == 1);

    string = (struct string){.data = (u8 *)"1sec", .len = 4};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(isParsingSuccessful && duration.ns == 1 * 1e9L);

    string = (struct string){.data = (u8 *)"5sec", .len = 4};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(isParsingSuccessful && duration.ns == 5 * 1e9L);

    string = (struct string){.data = (u8 *)"5min", .len = 4};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(isParsingSuccessful && duration.ns == 5 * 1e9L * 60);

    string = (struct string){.data = (u8 *)"5day", .len = 4};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(isParsingSuccessful && duration.ns == 5 * 1e9L * 60 * 60 * 24);

    string = (struct string){.data = (u8 *)"1hr5min", .len = 7};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(isParsingSuccessful && duration.ns == (1 * 1e9L * 60 * 60) + (5 * 1e9L * 60));

    string = (struct string){.data = (u8 *)0, .len = 0};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(!isParsingSuccessful);

    string = (struct string){.data = (u8 *)"", .len = 0};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(!isParsingSuccessful);

    string = (struct string){.data = (u8 *)" ", .len = 1};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(!isParsingSuccessful);

    string = (struct string){.data = (u8 *)"abc", .len = 3};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(!isParsingSuccessful);

    string = (struct string){.data = (u8 *)"5m5s", .len = 4};
    isParsingSuccessful = ParseDuration(&string, &duration);
    debug_assert(!isParsingSuccessful)
    */

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
      .data = (u8 *)zeroTerminatedString,                                                                              \
      .len = sizeof(zeroTerminatedString) - 1,                                                                         \
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
      IsStringContains(string, &nanosecondUnitString) || IsStringContains(string, &microsecondUnitString) ||
      IsStringContains(string, &millisocondUnitString) || IsStringContains(string, &secondUnitString) ||
      IsStringContains(string, &minuteUnitString) || IsStringContains(string, &hourUnitString) ||
      IsStringContains(string, &dayUnitString) || IsStringContains(string, &weekUnitString);
  if (!isUnitExistsInString) {
    return 0;
  }

  struct duration parsed = {};
  u64 value = 0;
  for (u64 index = 0; index < string->len; index++) {
    u8 digitCharacter = string->data[index];
    b8 isDigit = digitCharacter >= '0' && digitCharacter <= '9';
    if (!isDigit) {
      // - get unit
      struct string unitString = {.data = string->data + index, .len = string->len - index};
      if (/* unit: nanosecond */ IsStringStartsWith(&unitString, &nanosecondUnitString)) {
        parsed.ns += value;
        index += nanosecondUnitString.len;
      } else if (/* unit: microsecond */ IsStringStartsWith(&unitString, &microsecondUnitString)) {
        parsed.ns += value * 1e3L;
        index += microsecondUnitString.len;
      } else if (/* unit: millisecond */ IsStringStartsWith(&unitString, &millisocondUnitString)) {
        parsed.ns += value * 1e6L;
        index += millisocondUnitString.len;
      } else if (/* unit: second */ IsStringStartsWith(&unitString, &secondUnitString)) {
        parsed.ns += value * 1e9L;
        index += secondUnitString.len;
      } else if (/* unit: minute */ IsStringStartsWith(&unitString, &minuteUnitString)) {
        parsed.ns += value * 1e9L * 60;
        index += minuteUnitString.len;
      } else if (/* unit: hour */ IsStringStartsWith(&unitString, &hourUnitString)) {
        parsed.ns += value * 1e9L * 60 * 60;
        index += hourUnitString.len;
      } else if (/* unit: day */ IsStringStartsWith(&unitString, &dayUnitString)) {
        parsed.ns += value * 1e9L * 60 * 60 * 24;
        index += dayUnitString.len;
      } else if (/* unit: week */ IsStringStartsWith(&unitString, &weekUnitString)) {
        parsed.ns += value * 1e9L * 60 * 60 * 24 * 7;
        index += weekUnitString.len;
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

/* TEST CODE
  struct duration left;
  struct duration right;

  left = (struct duration){.ns = 1 * 1e9L};
  right = (struct duration){.ns = 5 * 1e9L};
  debug_assert(IsDurationLessThen(&left, &right));
  debug_assert(!IsDurationGraterThan(&left, &right));

  left = (struct duration){.ns = 1 * 1e9L};
  right = (struct duration){.ns = 1 * 1e9L};
  debug_assert(!IsDurationLessThan(&left, &right));
  debug_assert(!IsDurationGraterThan(&left, &right));

  left = (struct duration){.ns = 5 * 1e9L};
  right = (struct duration){.ns = 1 * 1e9L};
  debug_assert(!IsDurationLessThan(&left, &right));
  debug_assert(IsDurationGraterThan(&left, &right));

  return 0;
 */
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
  if (!string)
    return 0;

  u64 parsed = 0;
  for (u64 index = 0; index < string->len; index++) {
    u8 digitCharacter = string->data[index];
    b8 isDigit = digitCharacter >= '0' && digitCharacter <= '9';
    if (!isDigit) {
      return 1;
    }

    parsed *= 10;
    u8 digit = digitCharacter - (u8)'0';
    parsed += digit;
  }

  *value = parsed;
  return 1;
}

#endif /* __TEXT_H__ */
