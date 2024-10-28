#include "memory.h"
#include "text.h"

struct string_builder {
  struct string *outBuffer;
  struct string *stringBuffer;
  u64 length;
};

static inline void
StringBuilderAppendString(struct string_builder *stringBuilder, struct string *string)
{
  struct string *outBuffer = stringBuilder->outBuffer;
  memcpy(outBuffer->value + stringBuilder->length, string->value, string->length);
  stringBuilder->length += string->length;
  debug_assert(stringBuilder->length <= outBuffer->length);
}

static inline void
StringBuilderAppendU64(struct string_builder *stringBuilder, u64 value)
{
  struct string *outBuffer = stringBuilder->outBuffer;
  struct string *stringBuffer = stringBuilder->stringBuffer;

  struct string string = FormatU64(stringBuffer, value);
  memcpy(outBuffer->value + stringBuilder->length, string.value, string.length);
  stringBuilder->length += string.length;
  debug_assert(stringBuilder->length <= outBuffer->length);
}

static inline void
StringBuilderAppendHex(struct string_builder *stringBuilder, u64 value)
{
  struct string *outBuffer = stringBuilder->outBuffer;
  struct string *stringBuffer = stringBuilder->stringBuffer;

  struct string string = FormatHex(stringBuffer, value);
  memcpy(outBuffer->value + stringBuilder->length, string.value, string.length);
  stringBuilder->length += string.length;
  debug_assert(stringBuilder->length <= outBuffer->length);
}

static inline void
StringBuilderAppendF32(struct string_builder *stringBuilder, f32 value, u64 fractionCount)
{
  struct string *outBuffer = stringBuilder->outBuffer;
  struct string *stringBuffer = stringBuilder->stringBuffer;

  struct string string = FormatF32(stringBuffer, value, fractionCount);
  memcpy(outBuffer->value + stringBuilder->length, string.value, string.length);
  stringBuilder->length += string.length;
  debug_assert(stringBuilder->length <= outBuffer->length);
}

/*
 * Returns string that is ready for transmit.
 * Also resets length of builder.
 *
 * @code
 *   StringBuilderAppend..(stringBuilder, x);
 *   struct string string = StringBuilderFlush(stringBuilder);
 *   write(x, string.value, string.length);
 * @endcode
 */
static inline struct string
StringBuilderFlush(struct string_builder *stringBuilder)
{
  struct string *outBuffer = stringBuilder->outBuffer;
  struct string result = (struct string){.value = outBuffer->value, .length = stringBuilder->length};
  stringBuilder->length = 0;
  return result;
}
