#pragma once

#include "assert.h"
#include "math.h"
#include "type.h"

#if __has_builtin(__builtin_bzero)
#define bzero(address, size) __builtin_bzero(address, size)
#else
#error bzero must be supported by compiler
#endif

#if __has_builtin(__builtin_alloca)
#define alloca(size) __builtin_alloca(size)
#else
#error alloca must be supported by compiler
#endif

#if __has_builtin(__builtin_memcpy)
#define memcpy(dest, src, n) __builtin_memcpy(dest, src, n)
#else
#error memcpy must be supported by compiler
#endif

struct memory_block {
  void *block;
  u64 used;
  u64 total;
};

struct memory_chunk {
  void *block;
  u64 size;
  u64 max;
};

struct memory_temp {
  struct memory_block *memory;
  u64 startedAt;
};

static void *
MemPush(struct memory_block *mem, u64 size)
{
  debug_assert(mem->used + size <= mem->total);
  void *result = mem->block + mem->used;
  mem->used += size;
  return result;
}

static void *
MemPushAligned(struct memory_block *mem, u64 size, u64 alignment)
{
  debug_assert(IsPowerOfTwo(alignment));

  void *block = mem->block + mem->used;

  u64 alignmentMask = alignment - 1;
  u64 alignmentResult = ((u64)block & alignmentMask);
  if (alignmentResult != 0) {
    // if it is not aligned
    u64 alignmentOffset = alignment - alignmentResult;
    size += alignmentOffset;
    block += alignmentOffset;
  }

  debug_assert(mem->used + size <= mem->total);
  mem->used += size;

  return block;
}

static struct memory_chunk *
MemPushChunk(struct memory_block *mem, u64 size, u64 max)
{
  struct memory_chunk *chunk = MemPush(mem, sizeof(*chunk) + max * sizeof(u8) + max * size);
  chunk->block = chunk + sizeof(*chunk);
  chunk->size = size;
  chunk->max = max;
  for (u64 index = 0; index < chunk->max; index++) {
    u8 *flag = (u8 *)chunk->block + (sizeof(u8) * index);
    *flag = 0;
  }
  return chunk;
}

static inline b8
MemChunkIsDataAvailableAt(struct memory_chunk *chunk, u64 index)
{
  u8 *flags = (u8 *)chunk->block;
  return *(flags + index);
}

static inline void *
MemChunkGetDataAt(struct memory_chunk *chunk, u64 index)
{
  void *dataBlock = (u8 *)chunk->block + chunk->max;
  void *result = dataBlock + index * chunk->size;
  return result;
}

static void *
MemChunkPush(struct memory_chunk *chunk)
{
  void *result = 0;
  void *dataBlock = chunk->block + sizeof(u8) * chunk->max;
  for (u64 index = 0; index < chunk->max; index++) {
    u8 *flag = chunk->block + sizeof(u8) * index;
    if (*flag == 0) {
      result = dataBlock + index * chunk->size;
      *flag = 1;
      return result;
    }
  }

  return result;
}

static void
MemChunkPop(struct memory_chunk *chunk, void *block)
{
  void *dataBlock = chunk->block + sizeof(u8) * chunk->max;
  debug_assert((block >= dataBlock && block <= dataBlock + (chunk->size * chunk->max)) &&
               "this block is not belong to this chunk");
  u64 index = (block - dataBlock) / chunk->size;
  u8 *flag = chunk->block + sizeof(u8) * index;
  *flag = 0;
}

static struct memory_temp
MemTempBegin(struct memory_block *memory)
{
  return (struct memory_temp){
      .memory = memory,
      .startedAt = memory->used,
  };
}

static void
MemTempEnd(struct memory_temp *tempMemory)
{
  struct memory_block *memory = tempMemory->memory;
  memory->used = tempMemory->startedAt;
}

#include "text.h"
static struct string
MemPushString(struct memory_block *memory, u64 size)
{
  return (struct string){
      .value = MemPush(memory, size),
      .length = size,
  };
}
