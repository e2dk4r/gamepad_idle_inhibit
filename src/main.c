#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <fcntl.h>
#include <liburing.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include "StringBuilder.h"
#include "assert.h"
#include "memory.h"
#include "text.h"
#include "type.h"

// NOTE: CI fix
#ifndef IORING_ASYNC_CANCEL_ALL
#define IORING_ASYNC_CANCEL_ALL (1U << 0)
#endif

#define ARRAY_COUNT(array) (sizeof(array) / sizeof(array[0]))

#include "idle-inhibit-unstable-v1-client-protocol.h"

#define POLLIN 0x001  /* There is data to read.  */
#define POLLPRI 0x002 /* There is urgent data to read.  */
#define POLLOUT 0x004 /* Writing now will not block.  */

#define OP_DEVICE_OPEN (1 << 0)
#define OP_JOYSTICK_POLL (1 << 1)
#define OP_JOYSTICK_READ (1 << 2)
#define OP_IDLED (1 << 3)

#define GAMEPAD_ERROR_IO_URING_SETUP 1
#define GAMEPAD_ERROR_IO_URING_WAIT 2

#define GAMEPAD_ERROR_MEMORY 40

#define GAMEPAD_ERROR_INOTIFY_SETUP 10
#define GAMEPAD_ERROR_INOTIFY_WATCH_SETUP 11
#define GAMEPAD_ERROR_INOTIFY_WATCH 12
#define GAMEPAD_ERROR_INOTIFY_WATCH_POLL 12

#define GAMEPAD_ERROR_DEV_INPUT_DIR_OPEN 20
#define GAMEPAD_ERROR_DEV_INPUT_DIR_READ 21

#define GAMEPAD_ERROR_LIBEVDEV 30
#define GAMEPAD_ERROR_LIBEVDEV_FD 30

#define GAMEPAD_ERROR_WAYLAND 50
#define GAMEPAD_ERROR_WAYLAND_REGISTRY 51
#define GAMEPAD_ERROR_WAYLAND_EXTENSION 52
#define GAMEPAD_ERROR_WAYLAND_FD 53

#define GAMEPAD_ERROR_ARGUMENT_MISSING 9000
#define GAMEPAD_ERROR_ARGUMENT_UNKNOWN 9001
#define GAMEPAD_ERROR_ARGUMENT_MUST_BE_POSITIVE_NUMBER 9002
#define GAMEPAD_ERROR_ARGUMENT_MUST_BE_GRATER_THAN_0 9003
#define GAMEPAD_ERROR_ARGUMENT_MUST_BE_GRATER 9004
#define GAMEPAD_ERROR_ARGUMENT_MUST_BE_LESS 9005

#if GAMEPAD_IDLE_INHIBIT_DEBUG
#define debug(str) write(STDERR_FILENO, "d: " str, 3 + sizeof(str) - 1)
#else
#define debug(str)
#endif

#define fatal(str) write(STDERR_FILENO, "e: " str, 3 + sizeof(str) - 1)
#define warning(str) write(STDERR_FILENO, "w: " str, 3 + sizeof(str) - 1)
#define info(str) write(STDOUT_FILENO, "i: " str, 3 + sizeof(str) - 1)

struct op {
  u8 type;
};

struct op_device_open {
  u8 type;
  struct string path;
};

struct op_global {
};

struct op_inotify_watch {
  int fd;
};

struct gamepad {
  b8 isConnected : 1;
  b8 isEventIgnored : 1;

  b8 a : 1;
  b8 b : 1;
  b8 x : 1;
  b8 y : 1;

  b8 back : 1;
  b8 start : 1;
  b8 home : 1;

  b8 ls : 1;
  b8 rs : 1;

  // xbox
  b8 lb : 1;
  b8 rb : 1;

  // [0, 1] math coordinates
  f32 lsX;
  // [0, 1] math coordinates
  f32 lsY;
  // [0, 1] math coordinates
  f32 rsX;
  // [0, 1] math coordinates
  f32 rsY;

  // [0, 1] math coordinates
  f32 lt;
  // [0, 1] math coordinates
  f32 rt;
};

static struct gamepad *
GamepadGetNotConnected(struct gamepad *gamepads, u32 count)
{
  for (u32 index = 0; index < count; index++) {
    struct gamepad *gamepad = gamepads + index;
    if (gamepad->isConnected == 0)
      return gamepad;
  }

  return 0;
}

struct op_joystick_poll {
  u8 type;
  int fd;
  s32 stickRange;
  s32 stickMinimum;
  s32 triggerRange;
  s32 triggerMinimum;
  struct gamepad *gamepad;
  struct string path;
};

struct op_joystick_read {
  u8 type;
  struct input_event event;
  struct op_joystick_poll *op_joystick_poll;
};

#define KILOBYTES (1 << 10)
#define MEGABYTES (1 << 20)
#define GIGABYTES (1 << 30)

struct wl_context {
  struct wl_display *wl_display;
  struct wl_compositor *wl_compositor;
  struct wl_surface *wl_surface;
  struct zwp_idle_inhibit_manager_v1 *zwp_idle_inhibit_manager_v1;
  struct zwp_idle_inhibitor_v1 *zwp_idle_inhibitor_v1;
};

static void
wl_registry_global(void *data, struct wl_registry *wl_registry, u32 name, const char *interface, u32 version)
{
  struct wl_context *context = data;

  struct string interfaceString = StringFromZeroTerminated((u8 *)interface, 64);
  if (IsStringEqual(&interfaceString, &STRING_FROM_ZERO_TERMINATED("wl_compositor"))) {
    context->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
  } else if (IsStringEqual(&interfaceString, &STRING_FROM_ZERO_TERMINATED("zwp_idle_inhibit_manager_v1"))) {
    context->zwp_idle_inhibit_manager_v1 =
        wl_registry_bind(wl_registry, name, &zwp_idle_inhibit_manager_v1_interface, version);
  }
}

static void
wl_registry_global_remove(void *data, struct wl_registry *wl_registry, u32 name)
{
}

static const struct wl_registry_listener wl_registry_listener = {.global = wl_registry_global,
                                                                 .global_remove = wl_registry_global_remove};

int
main(int argc, char *argv[])
{
  int error_code = 0;
  struct wl_context context = {};

  struct duration timeout = DURATION_IN_SECONDS(30);
#if GAMEPAD_IDLE_INHIBIT_DEBUG
  timeout = DURATION_IN_SECONDS(3);
#endif

  u32 maxGamepadCount = 4;

  // parse commandline arguments
  for (u64 argumentIndex = 1; argumentIndex < argc; argumentIndex++) {
    struct string argument = StringFromZeroTerminated((u8 *)argv[argumentIndex], 1024);

    // --timeout
    if (IsStringEqual(&argument, &STRING_FROM_ZERO_TERMINATED("--timeout"))) {
      b8 isArgumentWasLast = argumentIndex + 1 == argc;
      if (isArgumentWasLast) {
        fatal("timeout value is missing\n");
        return GAMEPAD_ERROR_ARGUMENT_MISSING;
      }

      argumentIndex++;
      struct string timeoutString = StringFromZeroTerminated((u8 *)argv[argumentIndex], 1024);
      struct duration parsed;

      if (!ParseDuration(&timeoutString, &parsed)) {
        fatal("timeout must be positive number\n");
        return GAMEPAD_ERROR_ARGUMENT_MUST_BE_POSITIVE_NUMBER;
      } else if (IsDurationLessThan(&parsed, &DURATION_IN_SECONDS(1))) {
        fatal("timeout must be bigger or equal than 1 second\n");
        return GAMEPAD_ERROR_ARGUMENT_MUST_BE_GRATER;
      } else if (IsDurationGraterThan(&parsed, &DURATION_IN_DAYS(1))) {
        fatal("timeout must be less or equal 1 day\n");
        return GAMEPAD_ERROR_ARGUMENT_MUST_BE_LESS;
      }

      timeout = parsed;
    }

    // --max-gamepad-count
    else if (IsStringEqual(&argument, &STRING_FROM_ZERO_TERMINATED("--max-gamepad-count"))) {
      b8 isArgumentWasLast = argumentIndex + 1 == argc;
      if (isArgumentWasLast) {
        fatal("max-gamepad-count value is missing\n");
        return GAMEPAD_ERROR_ARGUMENT_MISSING;
      }

      argumentIndex++;
      struct string maxGamepadCountString = StringFromZeroTerminated((u8 *)argv[argumentIndex], 1024);
      if (!ParseU64(&maxGamepadCountString, (u64 *)&maxGamepadCount)) {
        fatal("max-gamepad-count must be positive number\n");
        return GAMEPAD_ERROR_ARGUMENT_MUST_BE_POSITIVE_NUMBER;
      } else if (maxGamepadCount == 0) {
        fatal("max-gamepad-count must be bigger than 0\n");
        return GAMEPAD_ERROR_ARGUMENT_MUST_BE_GRATER_THAN_0;
      } else if (maxGamepadCount > 256) {
        fatal("max-gamepad-count must be less than 256\n");
        return GAMEPAD_ERROR_ARGUMENT_MUST_BE_LESS;
      }
    }

    // -h, --help
    else if (IsStringEqual(&argument, &STRING_FROM_ZERO_TERMINATED("-h")) ||
             IsStringEqual(&argument, &STRING_FROM_ZERO_TERMINATED("--help"))) {
      static struct string helpString = {
#define HELP_STRING_TEXT                                                                                               \
  "NAME:"                                                                                                              \
  "\n"                                                                                                                 \
  "  gamepad_idle_inhibit - prevent idling wayland on controllers button presses"                                      \
  "\n\n"                                                                                                               \
  "SYNOPSIS:"                                                                                                          \
  "\n"                                                                                                                 \
  "  gamepad_idle_inhibit [OPTION]..."                                                                                 \
  "\n\n"                                                                                                               \
  "DESCIPTION:"                                                                                                        \
  "\n"                                                                                                                 \
  "  -t, --timeout [1sec,1day]\n"                                                                                      \
  "    How much time need to elapse to idle.\n"                                                                        \
  "    | Duration | Length      |\n"                                                                                   \
  "    |----------|-------------|\n"                                                                                   \
  "    | ns       | nanosecond  |\n"                                                                                   \
  "    | us       | microsecond |\n"                                                                                   \
  "    | ms       | millisecond |\n"                                                                                   \
  "    | sec      | second      |\n"                                                                                   \
  "    | min      | minute      |\n"                                                                                   \
  "    | hr       | hour        |\n"                                                                                   \
  "    | day      | day         |\n"                                                                                   \
  "    | wk       | week        |\n"                                                                                   \
  "    Default is 30sec."                                                                                              \
  "\n\n"                                                                                                               \
  "  --max-gamepad-count [1-256]\n"                                                                                    \
  "    How many gamepads need to be tracked.\n"                                                                        \
  "    Default is 4."                                                                                                  \
  "\n\n"
          .value = (u8 *)HELP_STRING_TEXT,
          .length = sizeof(HELP_STRING_TEXT) - 1,
#undef HELP_STRING_TEXT
      };
      write(STDOUT_FILENO, helpString.value, helpString.length);
      return 0;
    }

    // unknown argument
    else {
      write(STDERR_FILENO, "e: Unknown '", 12);
      write(STDERR_FILENO, argument.value, argument.length);
      write(STDERR_FILENO, "' argument\n", 11);
      return GAMEPAD_ERROR_ARGUMENT_UNKNOWN;
    }
  }

  // TODO: handle SIGINT

  /* wayland */
  context.wl_display = wl_display_connect(0);
  if (!context.wl_display) {
    fatal("cannot connect wayland display!\n");
    error_code = GAMEPAD_ERROR_WAYLAND;
    goto exit;
  }

  struct wl_registry *wl_registry = wl_display_get_registry(context.wl_display);
  if (!wl_registry) {
    fatal("cannot get wayland registry!\n");
    error_code = GAMEPAD_ERROR_WAYLAND_REGISTRY;
    goto wayland_exit;
  }

  wl_registry_add_listener(wl_registry, &wl_registry_listener, &context);
  wl_display_roundtrip(context.wl_display);

  if (!context.wl_compositor || !context.zwp_idle_inhibit_manager_v1) {
    fatal("this wayland compositor not supported\n");
    error_code = GAMEPAD_ERROR_WAYLAND_EXTENSION;
    goto wayland_exit;
  }

  context.wl_surface = wl_compositor_create_surface(context.wl_compositor);

  /* memory
   *   Select only one option.
   */
  struct memory_block memory = {};
  // TODO: tune total used memory according to arguments
  memory.total = 1 * KILOBYTES;

  // OPTION A - allocate from stack
  if (1) {
    // - check limit
    struct rlimit rlim;
    if (getrlimit(RLIMIT_STACK, &rlim)) {
      fatal("cannot check stack limit\n");
      error_code = GAMEPAD_ERROR_MEMORY;
      goto wayland_exit;
    }

    // (lldb) p rlim
    // (rlimit)  (rlim_cur = 8388608, rlim_max = 18446744073709551615)
    if (rlim.rlim_cur < (4 * KILOBYTES) + memory.total) {
      fatal("you do not have 1k memory available.\n");
      error_code = GAMEPAD_ERROR_MEMORY;
      goto wayland_exit;
    }

    // - allocate
    memory.block = alloca(memory.total);

    // - initialize to zero
    bzero(memory.block, memory.total);
  }

  // OPTION B - Allocate from RAM
  else {
    memory.block = mmap(0, (size_t)memory.total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!memory.block) {
      fatal("you do not have 1k memory available.\n");
      error_code = GAMEPAD_ERROR_MEMORY;
      goto wayland_exit;
    }
  }

  struct gamepad *gamepads = MemPush(&memory, maxGamepadCount * sizeof(*gamepads));

  struct memory_chunk *MemoryForDeviceOpenEvents =
      MemPushChunk(&memory, sizeof(struct op_device_open), maxGamepadCount);
  struct memory_chunk *MemoryForJoystickPollEvents =
      MemPushChunk(&memory, sizeof(struct op_joystick_poll), maxGamepadCount);
  struct memory_chunk *MemoryForJoystickReadEvents =
      MemPushChunk(&memory, sizeof(struct op_joystick_read), maxGamepadCount);

  struct string stdoutBuffer = MemPushString(&memory, 256);
  struct string stringBuffer = MemPushString(&memory, 32);
  struct string_builder stringBuilder = (struct string_builder){
      .outBuffer = &stdoutBuffer,
      .stringBuffer = &stringBuffer,
  };

#if GAMEPAD_IDLE_INHIBIT_DEBUG
  {
    StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("total memory usage (in bytes):  "));
    StringBuilderAppendU64(&stringBuilder, memory.used);
    StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));

    StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("total memory wasted (in bytes): "));
    StringBuilderAppendU64(&stringBuilder, memory.total - memory.used);
    StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));
    struct string string = StringBuilderFlush(&stringBuilder);
    write(STDOUT_FILENO, string.value, string.length);
  }
#endif

  /* io_uring */
  struct io_uring ring;
  if (io_uring_queue_init(maxGamepadCount + 1 + 1 + 4, &ring, 0)) {
    error_code = GAMEPAD_ERROR_IO_URING_SETUP;
    goto wayland_exit;
  }

  /* notify when on wayland events */
  struct op_global waylandOp = {};
  {
    int wlDisplayFd = wl_display_get_fd(context.wl_display);
    if (wlDisplayFd == -1) {
      error_code = GAMEPAD_ERROR_WAYLAND_FD;
      goto io_uring_exit;
    }

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_poll_multishot(sqe, wlDisplayFd, POLLIN);
    io_uring_sqe_set_data(sqe, &waylandOp);
  }

  /* notify when a new input added */
  struct op_inotify_watch inotifyOp = {};
  {
    inotifyOp.fd = inotify_init1(IN_NONBLOCK);
    if (inotifyOp.fd == -1) {
      error_code = GAMEPAD_ERROR_INOTIFY_SETUP;
      goto io_uring_exit;
    }

    int inotifyWatchFd = inotify_add_watch(inotifyOp.fd, "/dev/input", IN_CREATE | IN_ATTRIB);
    if (inotifyWatchFd == -1) {
      error_code = GAMEPAD_ERROR_INOTIFY_WATCH_SETUP;
      goto inotify_exit;
    }

    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_poll_multishot(sqe, inotifyOp.fd, POLLIN);
    io_uring_sqe_set_data(sqe, &inotifyOp);
  }

  /* add already connected joysticks to queue */
  int inputDirFd = open("/dev/input", O_RDONLY | O_NONBLOCK);
  if (inputDirFd == -1) {
    error_code = GAMEPAD_ERROR_DEV_INPUT_DIR_OPEN;
    goto inotify_exit;
  }

  DIR *dir = fdopendir(inputDirFd);
  runtime_assert(dir);

  struct dirent *dirent;
  u32 dirent_max = 1024;
  while (dirent_max--) {
    errno = 0;
    dirent = readdir(dir);

    /* error occured */
    if (errno != 0) {
      error_code = GAMEPAD_ERROR_DEV_INPUT_DIR_READ;
      closedir(dir);
      goto inotify_exit;
    }

    /* end of directory stream is reached */
    if (dirent == 0)
      break;

    if (dirent->d_type != DT_CHR)
      continue;

    struct op_joystick_poll stagedOp = {
        .type = OP_JOYSTICK_POLL,
        .path = StringFromZeroTerminated((u8 *)dirent->d_name, 1024),
    };
    if (!IsStringStartsWith(&stagedOp.path, &STRING_FROM_ZERO_TERMINATED("event")))
      continue;

    stagedOp.fd = openat(inputDirFd, (char *)stagedOp.path.value, O_RDONLY | O_NONBLOCK);
    if (stagedOp.fd == -1) {
      // warning("cannot open some event file\n");
      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("Cannot open device. event: "));
      StringBuilderAppendString(&stringBuilder, &stagedOp.path);
      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));

      struct string string = StringBuilderFlush(&stringBuilder);
      write(STDOUT_FILENO, string.value, string.length);
      continue;
    }

    // - get device id, capibilities
    struct input_id id;
    u8 evBits[(EV_CNT + 7) / 8];
    u8 keyBits[(KEY_CNT + 7) / 8];
    u8 absBits[(ABS_CNT + 7) / 8];
    if (/* get id */
        ioctl(stagedOp.fd, EVIOCGID, &id) < 0 ||
        /* get bits */
        ioctl(stagedOp.fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0 ||
        ioctl(stagedOp.fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0 ||
        ioctl(stagedOp.fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
      close(stagedOp.fd);
      continue;
    }

    /* detect joystick */
    b8 isGamepad =
        /* has KEY and ABS capibilities */
        (evBits[EV_KEY / 8] & 1 << (EV_KEY % 8)) &&
        (evBits[EV_ABS / 8] & 1 << (EV_ABS % 8))
        /* and has BTN_GAMEPAD
         * see: https://www.kernel.org/doc/Documentation/input/gamepad.txt
         *      3. Detection
         */
        && (keyBits[BTN_GAMEPAD / 8] & 1 << (BTN_GAMEPAD % 8));
    if (!isGamepad) {
      close(stagedOp.fd);
      continue;
    }

    // - get axes abs info
    struct input_absinfo stickAbsInfo;
    struct input_absinfo triggerAbsInfo;
    if (ioctl(stagedOp.fd, EVIOCGABS(ABS_X), &stickAbsInfo) < 0 ||
        ioctl(stagedOp.fd, EVIOCGABS(ABS_Z), &triggerAbsInfo) < 0) {
      close(stagedOp.fd);
      continue;
    }
    stagedOp.stickRange = stickAbsInfo.maximum - stickAbsInfo.minimum;
    stagedOp.stickMinimum = stickAbsInfo.minimum;
    stagedOp.triggerRange = triggerAbsInfo.maximum - triggerAbsInfo.minimum;
    stagedOp.triggerMinimum = triggerAbsInfo.minimum;

    {
      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("Gamepad connected @ bus "));
      StringBuilderAppendHex(&stringBuilder, id.bustype);

      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" vendor "));
      StringBuilderAppendHex(&stringBuilder, id.vendor);

      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" product "));
      StringBuilderAppendHex(&stringBuilder, id.product);

      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));
      struct string string = StringBuilderFlush(&stringBuilder);
      write(STDOUT_FILENO, string.value, string.length);
    }

    stagedOp.gamepad = GamepadGetNotConnected(gamepads, maxGamepadCount);
    if (!stagedOp.gamepad) {
      warning("Maximum number of gamepads connected! So not registering this one.\n");
      close(stagedOp.fd);
      continue;
    }
    stagedOp.gamepad->isConnected = 1;

    // - Queue poll on gamepad for input event
    struct op_joystick_poll *submitOp = MemChunkPush(MemoryForJoystickPollEvents);
    debug_assert(submitOp);
    *submitOp = stagedOp;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_poll_add(sqe, submitOp->fd, POLLIN);
    io_uring_sqe_set_data(sqe, submitOp);
  }

  // NOTE: closedir also closes inputDirFd, that we needed at main loop
  //       for use in openat()
  // closedir(dir);

  struct op idledOp = {
      .type = OP_IDLED,
  };

  /* submit any work */
  io_uring_submit(&ring);

  /* event loop */
  struct io_uring_cqe *cqe;
  while (1) {
    int error;

    while (wl_display_prepare_read(context.wl_display) != 0)
      wl_display_dispatch_pending(context.wl_display);
    wl_display_flush(context.wl_display);

  wait:
    error = io_uring_wait_cqe(&ring, &cqe);
    if (error) {
      error *= -1;
      if (error == EAGAIN || error == EINTR)
        goto wait;
      fatal("io_uring\n");

#if GAMEPAD_IDLE_INHIBIT_DEBUG
      // printf("errno: %d %s\n", -error, strerror(-error));

      // see: /usr/include/asm-generic/errno-base.h
      //      /usr/include/asm-generic/errno.h
      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("errno: "));
      StringBuilderAppendU64(&stringBuilder, error);
      StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));

      struct string string = StringBuilderFlush(&stringBuilder);
      write(STDOUT_FILENO, string.value, string.length);
#endif

      error_code = GAMEPAD_ERROR_IO_URING_WAIT;
      break;
    }

    struct op *op = io_uring_cqe_get_data(cqe);
    // NOTE: Ignore cqe with no data.
    //       eg. close() or cancel()
    if (op == 0)
      goto cqe_seen;

    if ((void *)op != &waylandOp)
      wl_display_cancel_read(context.wl_display);

    /* on wayland events */
    if ((void *)op == &waylandOp) {
      int revents = cqe->res;

      if (revents & POLLIN) {
        wl_display_read_events(context.wl_display);
      } else {
        wl_display_cancel_read(context.wl_display);
      }
    }

    /* on inotify events */
    else if ((void *)op == &inotifyOp) {
      struct op_inotify_watch *op = io_uring_cqe_get_data(cqe);

      // NOTE: If inotify fails, we finish program with error
      if (cqe->res < 0) {
        fatal("inotify watch\n");
        error_code = GAMEPAD_ERROR_INOTIFY_WATCH;
        break;
      }

      int revents = cqe->res;
      if (!(revents & POLLIN)) {
        fatal("inotify\n");
        error_code = GAMEPAD_ERROR_INOTIFY_WATCH_POLL;
        break;
      }

      // - Read inotify_event
      /*
       * get the number of bytes available to read from an
       * inotify file descriptor.
       * see: inotify(7)
       */
      u32 bufsz;
      ioctl(op->fd, FIONREAD, &bufsz);

      u8 buf[bufsz];
      ssize_t readBytes = read(op->fd, buf, sizeof(buf));
      if (readBytes < 0) {
        goto cqe_seen;
      }

      struct inotify_event *event = (struct inotify_event *)buf;
      if (event->len <= 0)
        goto cqe_seen;

      if (event->mask & IN_ISDIR)
        goto cqe_seen;

      struct op_device_open stagedOp = {
          .type = OP_DEVICE_OPEN,
          .path = StringFromZeroTerminated((u8 *)event->name, 1024),
      };

      if (!IsStringStartsWith(&stagedOp.path, &STRING_FROM_ZERO_TERMINATED("event")))
        goto cqe_seen;
      // printf("--> %d %s %s\n", event->mask, event->name, path);

      // NOTE: When gamepad connects, we have to wait for udev
      //       to set its permissions right.
      //
      //   When gamepad connects:
      //       0001 event20 mask: IN_CREATE
      //       0002 event20 mask: IN_ATTRIB
      //       ...
      //       ...
      //   When gamepad disconnects:
      //       0015 event20 mask: IN_ATTRIB

      // - Do not request openat() when gamepad disconnects!
      {
        for (u64 pollEventIndex = 0; pollEventIndex < MemoryForJoystickPollEvents->max; pollEventIndex++) {
          if (!MemChunkIsDataAvailableAt(MemoryForJoystickPollEvents, pollEventIndex))
            continue;
          struct op_joystick_poll *op_joystick_poll = MemChunkGetDataAt(MemoryForJoystickPollEvents, pollEventIndex);
          if (IsStringEqual(&stagedOp.path, &op_joystick_poll->path))
            goto cqe_seen;
        }
      }

      if (!(event->mask & IN_ATTRIB))
        goto cqe_seen;

      // - Try to open device
      struct op_device_open *submitOp = MemChunkPush(MemoryForDeviceOpenEvents);
      debug_assert(submitOp);
      *submitOp = stagedOp;

      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_openat(sqe, inputDirFd, (char *)submitOp->path.value, O_RDONLY | O_NONBLOCK, 0);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);
    }

    /* on device open events */
    else if (op->type & OP_DEVICE_OPEN) {
      struct op_device_open *op = io_uring_cqe_get_data(cqe);

      // - Device open event is only one time,
      //   so release used memory
      struct string path = op->path;
      MemChunkPop(MemoryForDeviceOpenEvents, op);

      // - When open failed, exit immediately
      b8 isOpenAtFailed = cqe->res < 0;
      if (isOpenAtFailed) {
        goto cqe_seen;
      }

      int fd = cqe->res;
      struct op_joystick_poll stagedOp = {
          .type = OP_JOYSTICK_POLL,
          .path = path,
          .fd = fd,
      };

      // - Detect if connected device is gamepad
      struct input_id id;
      u8 evBits[(EV_CNT + 7) / 8];
      u8 keyBits[(KEY_CNT + 7) / 8];
      u8 absBits[(ABS_CNT + 7) / 8];
      if (/* get id */
          ioctl(stagedOp.fd, EVIOCGID, &id) < 0 ||
          /* get bits */
          ioctl(stagedOp.fd, EVIOCGBIT(0, sizeof(evBits)), evBits) < 0 ||
          ioctl(stagedOp.fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) < 0 ||
          ioctl(stagedOp.fd, EVIOCGBIT(EV_ABS, sizeof(absBits)), absBits) < 0) {
        // NOTE: When cannot get device capibilities
        //   - Close file descriptor
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_close(sqe, stagedOp.fd);
        io_uring_sqe_set_data(sqe, 0);
        io_uring_submit(&ring);
        goto cqe_seen;
      }

      /* detect joystick */
      b8 isGamepad =
          /* has KEY and ABS capibilities */
          (evBits[EV_KEY / 8] & 1 << (EV_KEY % 8)) &&
          (evBits[EV_ABS / 8] & 1 << (EV_ABS % 8))
          /* and has BTN_GAMEPAD
           * see: https://www.kernel.org/doc/Documentation/input/gamepad.txt
           *      3. Detection
           */
          && (keyBits[BTN_GAMEPAD / 8] & 1 << (BTN_GAMEPAD % 8));
      if (!isGamepad) {
        // NOTE: When device is not gamepad
        //   - Close file descriptor
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_close(sqe, stagedOp.fd);
        io_uring_sqe_set_data(sqe, 0);
        io_uring_submit(&ring);
        goto cqe_seen;
      }

      // - get axes abs info
      struct input_absinfo stickAbsInfo;
      struct input_absinfo triggerAbsInfo;
      if (ioctl(stagedOp.fd, EVIOCGABS(ABS_X), &stickAbsInfo) < 0 ||
          ioctl(stagedOp.fd, EVIOCGABS(ABS_Z), &triggerAbsInfo) < 0) {
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_close(sqe, stagedOp.fd);
        io_uring_sqe_set_data(sqe, 0);
        io_uring_submit(&ring);
        goto cqe_seen;
      }
      stagedOp.stickRange = stickAbsInfo.maximum - stickAbsInfo.minimum;
      stagedOp.stickMinimum = stickAbsInfo.minimum;
      stagedOp.triggerRange = triggerAbsInfo.maximum - triggerAbsInfo.minimum;
      stagedOp.triggerMinimum = triggerAbsInfo.minimum;

      {
        StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("Gamepad connected @ bus "));
        StringBuilderAppendHex(&stringBuilder, id.bustype);

        StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" vendor "));
        StringBuilderAppendHex(&stringBuilder, id.vendor);

        StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" product "));
        StringBuilderAppendHex(&stringBuilder, id.product);

        StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));
        struct string string = StringBuilderFlush(&stringBuilder);
        write(STDOUT_FILENO, string.value, string.length);
      }

      stagedOp.gamepad = GamepadGetNotConnected(gamepads, maxGamepadCount);
      if (!stagedOp.gamepad) {
        warning("Maximum number of gamepads connected! So not registering this one.\n");
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_close(sqe, stagedOp.fd);
        io_uring_sqe_set_data(sqe, 0);
        io_uring_submit(&ring);
        goto cqe_seen;
      }
      stagedOp.gamepad->isConnected = 1;

      // - Queue poll on gamepad for input event
      struct op_joystick_poll *submitOp = MemChunkPush(MemoryForJoystickPollEvents);
      debug_assert(submitOp);
      *submitOp = stagedOp;
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_poll_add(sqe, submitOp->fd, POLLIN);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);

      goto cqe_seen;
    }

    /* on joystick poll events */
    else if (op->type & OP_JOYSTICK_POLL) {
      // OP_JOYSTICK_POLL event is requested when
      //   - At startup while detecting already connected
      //     gamepads
      //   - New gamepad connected.
      //     OP_INOTIFY_WATCH -> OP_DEVICE_OPEN -> Is gamepad?
      //   - There is no input event to be read from file descriptor.
      //     Continue to wait for any input event.
      //     OP_JOYSTICK_READ
      struct op_joystick_poll *op = io_uring_cqe_get_data(cqe);

      int revents = cqe->res;
      if (!(revents & POLLIN)) {
        warning("cannot read events from device. maybe disconnected?\n");

        // NOTE: When gamepad disconnets
        //   1 - Close the file descriptor
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_close(sqe, op->fd);
        io_uring_sqe_set_data(sqe, 0);
        io_uring_submit(&ring);

        //   2 - Disconnect virtual gamepad
        op->gamepad->isConnected = 0;

        //   3 - Release resources
        MemChunkPop(MemoryForJoystickPollEvents, op);

        //   4 - Stop trying to queue read joystick event
        goto cqe_seen;
      }

      // NOTE: When joystick event ready to be read, queue read event
      struct op_joystick_read stagedOp = {
          .type = OP_JOYSTICK_READ,
          .op_joystick_poll = op,
      };

      //   - Acquire memory for read event
      struct op_joystick_read *submitOp = MemChunkPush(MemoryForJoystickReadEvents);
      debug_assert(submitOp);
      *submitOp = stagedOp;

      //   - Queue read event
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_read(sqe, op->fd, &submitOp->event, sizeof(submitOp->event), 0);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);
    }

    /* on joystick read events */
    else if (op->type & OP_JOYSTICK_READ) {
      struct op_joystick_read *op = io_uring_cqe_get_data(cqe);
      struct op_joystick_poll *op_joystick_poll = op->op_joystick_poll;

      /* on joystick read error (eg. joystick removed), close the fd */
      b8 isReadFailed = cqe->res < 0;
      if (isReadFailed) {
        b8 isGamepadDisconnected = cqe->res < 0 && -cqe->res == ENODEV;
        if (isGamepadDisconnected) {
          warning("gamepad disconnected\n");

          // NOTE: When gamepad disconnets
          //   1 - Close the file descriptor
          struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
          io_uring_prep_close(sqe, op_joystick_poll->fd);
          io_uring_sqe_set_data(sqe, 0);
          io_uring_submit(&ring);

          //   2 - Disconnect virtual gamepad
          op_joystick_poll->gamepad->isConnected = 0;

          //   3 - Release resources
          MemChunkPop(MemoryForJoystickPollEvents, op_joystick_poll);
          MemChunkPop(MemoryForJoystickReadEvents, op);

          //   4 - Stop polling on file descriptor
          goto cqe_seen;
        }

        // NOTE: When there is no available data anymore to read
        //   1 - Continue to poll on file descriptor
        struct op_joystick_poll *submitOp = op->op_joystick_poll;
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_poll_add(sqe, submitOp->fd, POLLIN);
        io_uring_sqe_set_data(sqe, submitOp);
        io_uring_submit(&ring);

        //   2 - Release resources
        MemChunkPop(MemoryForJoystickReadEvents, op);

        //   3 - Stop trying to read joystick another event
        goto cqe_seen;
      }

      // NOTE: On joystick event
      struct input_event *event = &op->event;

#if GAMEPAD_IDLE_INHIBIT_DEBUG
      // printf("%p fd: %d time: %ldsec%ldus type: %d code: %d value: %d\n", op, op_joystick_poll->fd,
      //        event->input_event_sec, event->input_event_usec, event->type, event->code, event->value);
#endif
      struct gamepad *gamepad = op_joystick_poll->gamepad;
      if (event->type == EV_SYN) {
        // see: https://www.kernel.org/doc/html/latest/input/event-codes.html#ev-syn
        if (event->code == SYN_DROPPED)
          gamepad->isEventIgnored = 1;
        else if (event->code == SYN_REPORT)
          gamepad->isEventIgnored = 0;
      }

      if (!gamepad->isEventIgnored) {
        if (event->type == EV_KEY) {
          b8 isPressed = event->value & 1;
          switch (event->code) {
          case BTN_A:
            gamepad->a = isPressed;
            break;
          case BTN_B:
            gamepad->b = isPressed;
            break;
          case BTN_X:
            gamepad->x = isPressed;
            break;
          case BTN_Y:
            gamepad->y = isPressed;
            break;
          case BTN_SELECT:
            gamepad->back = isPressed;
            break;
          case BTN_START:
            gamepad->start = isPressed;
            break;
          case BTN_MODE:
            gamepad->home = isPressed;
            break;
          case BTN_THUMBL:
            gamepad->ls = isPressed;
            break;
          case BTN_THUMBR:
            gamepad->rs = isPressed;
            break;
          case BTN_TL:
          case BTN_TL2:
            gamepad->lb = isPressed;
            break;
          case BTN_TR:
          case BTN_TR2:
            gamepad->rb = isPressed;
            break;
          }
        } else if (event->type == EV_ABS) {
          switch (event->code) {
          case ABS_HAT0X:
          case ABS_HAT1X:
          case ABS_HAT2X:
          case ABS_HAT3X: {
            gamepad->lsX = (f32)event->value;
          } break;

          case ABS_HAT0Y:
          case ABS_HAT1Y:
          case ABS_HAT2Y:
          case ABS_HAT3Y: {
            gamepad->lsY = (f32)-event->value;
          } break;

          case ABS_X:
          case ABS_RX:
          case ABS_Y:
          case ABS_RY: {
            s32 minimum = op_joystick_poll->stickMinimum;
            s32 range = op_joystick_poll->stickRange;
            f32 normal = (f32)(event->value - minimum) / (f32)range;
            debug_assert(normal >= 0.0f && normal <= 1.0f);
            f32 unit = 2.0f * normal - 1.0f;
            debug_assert(unit >= -1.0f && unit <= 1.0f);

            if (event->code == ABS_Y || event->code == ABS_RY)
              unit *= -1;

            f32 *stick = event->code == ABS_X    ? &gamepad->lsX
                         : event->code == ABS_Y  ? &gamepad->lsY
                         : event->code == ABS_RX ? &gamepad->rsX
                         : event->code == ABS_RY ? &gamepad->rsY
                                                 : 0;
            debug_assert(stick);
            *stick = unit;
          } break;

          case ABS_Z:
          case ABS_RZ: {
            s32 minimum = op_joystick_poll->triggerMinimum;
            s32 range = op_joystick_poll->triggerRange;
            f32 normal = (f32)(event->value - minimum) / (f32)range;
            debug_assert(normal >= 0.0f && normal <= 1.0f);

            f32 *trigger = event->code == ABS_Z ? &gamepad->lt : event->code == ABS_RZ ? &gamepad->rt : 0;
            debug_assert(trigger);
            *trigger = normal;
          } break;
          }
        }

        StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("Gamepad #"));
        StringBuilderAppendHex(&stringBuilder, (u64)&gamepad);

#define STRING_BUILDER_APPEND_BUTTON(prefix, button)                                                                   \
  StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" " prefix " "));                             \
  StringBuilderAppendU64(&stringBuilder, gamepad->button)

#define STRING_BUILDER_APPEND_ANALOG(prefix, stick, stickX, stickY)                                                    \
  StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" " prefix " "));                             \
  StringBuilderAppendU64(&stringBuilder, gamepad->stick);                                                              \
  StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" "));                                        \
  StringBuilderAppendF32(&stringBuilder, gamepad->stickX, 2);                                                          \
  StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(","));                                        \
  StringBuilderAppendF32(&stringBuilder, gamepad->stickY, 2)

#define STRING_BUILDER_APPEND_TRIGGER(prefix, trigger)                                                                 \
  StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED(" " prefix " "));                             \
  StringBuilderAppendF32(&stringBuilder, gamepad->trigger, 2)

        STRING_BUILDER_APPEND_BUTTON("a", a);
        STRING_BUILDER_APPEND_BUTTON("b", b);
        STRING_BUILDER_APPEND_BUTTON("x", x);
        STRING_BUILDER_APPEND_BUTTON("y", y);

        STRING_BUILDER_APPEND_ANALOG("ls", ls, lsX, lsY);
        STRING_BUILDER_APPEND_ANALOG("rs", rs, rsX, rsY);

        STRING_BUILDER_APPEND_BUTTON("lb", lb);
        STRING_BUILDER_APPEND_TRIGGER("lt", lt);

        STRING_BUILDER_APPEND_BUTTON("rb", rb);
        STRING_BUILDER_APPEND_TRIGGER("rt", rt);

        STRING_BUILDER_APPEND_BUTTON("home", home);
        STRING_BUILDER_APPEND_BUTTON("back", back);
        STRING_BUILDER_APPEND_BUTTON("start", start);

#undef STRING_BUILDER_APPEND_ANALOG
#undef STRING_BUILDER_APPEND_TRIGGER
#undef STRING_BUILDER_APPEND_BUTTON

        StringBuilderAppendString(&stringBuilder, &STRING_FROM_ZERO_TERMINATED("\n"));
        struct string string = StringBuilderFlush(&stringBuilder);
        write(STDOUT_FILENO, string.value, string.length);
      }

      // DEBUG: get file path from file descriptor
      // char path[256];
      // snprintf(path, sizeof(path), "/proc/self/fd/%d", op_joystick_poll->fd);
      // readlink(path, path, sizeof(path));
      // printf("fd: %d path: %s\n", op_joystick_poll->fd, path);

      // NOTE: When there is more data to read
      //   1 - Try to read another joystick event
      struct op_joystick_read *submitOp = op;
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_read(sqe, op_joystick_poll->fd, &submitOp->event, sizeof(submitOp->event), 0);
      io_uring_sqe_set_data(sqe, submitOp);

      //   2 - If there is no wayland idle inhibitor, create one
      if (!context.zwp_idle_inhibitor_v1) {
        info("timer armed\n");
        context.zwp_idle_inhibitor_v1 =
            zwp_idle_inhibit_manager_v1_create_inhibitor(context.zwp_idle_inhibit_manager_v1, context.wl_surface);
        wl_surface_commit(context.wl_surface);
      }

      //   3 - Rearm idle timer for destroying wayland idle inhibitor
      /* cancel timer */
      sqe = io_uring_get_sqe(&ring);
      io_uring_prep_cancel(sqe, &idledOp, IORING_ASYNC_CANCEL_ALL);
      io_uring_sqe_set_data(sqe, 0);
      sqe->flags |= IOSQE_IO_LINK;

      /* arm timer again */
      sqe = io_uring_get_sqe(&ring);
      struct __kernel_timespec *ts = &(struct __kernel_timespec){
          .tv_nsec = timeout.ns,
      };
      io_uring_prep_timeout(sqe, ts, 0, 0);
      io_uring_sqe_set_data(sqe, &idledOp);
      io_uring_submit(&ring);
    }

    /* on idle event */
    else if (op->type & OP_IDLED) {
      u8 isTimesUp = cqe->res == -ETIME;
      u8 isCancelled = cqe->res == -ECANCELED;
      if (cqe->res != 0 && !(isTimesUp || isCancelled)) {
        fatal("waiting for idle failed\n");
        break;
      }

      if (isCancelled) {
        debug("timer cancelled\n");
        goto cqe_seen;
      }

      info("idled\n");
      zwp_idle_inhibitor_v1_destroy(context.zwp_idle_inhibitor_v1);
      wl_surface_commit(context.wl_surface);
      context.zwp_idle_inhibitor_v1 = 0;
    }

  cqe_seen:
    io_uring_cqe_seen(&ring, cqe);
  }

inotify_exit:
  close(inotifyOp.fd);

io_uring_exit:
  io_uring_queue_exit(&ring);

wayland_exit:
  wl_display_disconnect(context.wl_display);

exit:
  return error_code;
}
