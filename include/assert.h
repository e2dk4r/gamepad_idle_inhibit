#pragma once

#if GAMEPAD_IDLE_INHIBIT_DEBUG
#define debug_assert(x)                                                                                                \
  if (!(x)) {                                                                                                          \
    __builtin_debugtrap();                                                                                             \
  }
#else
#define debug_assert(x)
#endif

#define runtime_assert(x)                                                                                              \
  if (!(x)) {                                                                                                          \
    __builtin_trap();                                                                                                  \
  }
