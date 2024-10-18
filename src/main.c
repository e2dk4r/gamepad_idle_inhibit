#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <fcntl.h>
#include <libevdev/libevdev.h>
#include <liburing.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#if GAMEPAD_IDLE_INHIBIT_DEBUG
#define assert(x)                                                                                                      \
  if (!(x)) {                                                                                                          \
    __builtin_debugtrap();                                                                                             \
  }
#else
#define assert(x)
#endif

#define runtime_assert(x)                                                                                              \
  if (!(x)) {                                                                                                          \
    __builtin_trap();                                                                                                  \
  }

#include "idle-inhibit-unstable-v1-client-protocol.h"

#define POLLIN 0x001  /* There is data to read.  */
#define POLLPRI 0x002 /* There is urgent data to read.  */
#define POLLOUT 0x004 /* Writing now will not block.  */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef u8 b8;

#define OP_INOTIFY_WATCH (1 << 0)
#define OP_DEVICE_OPEN (1 << 1)
#define OP_JOYSTICK_POLL (1 << 2)
#define OP_JOYSTICK_READ (1 << 3)
#define OP_WAYLAND (1 << 4)
#define OP_IDLED (1 << 5)

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

struct op_inotify_watch {
  u8 type;
  int fd;
};

struct op_joystick_poll {
  u8 type;
  int fd;
};

struct op_joystick_read {
  u8 type;
  struct input_event event;
  struct op_joystick_poll *op_joystick_poll;
};

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

#define KILOBYTES (1 << 10)
#define MEGABYTES (1 << 20)
#define GIGABYTES (1 << 30)

static void *
mem_chunk_push(struct memory_chunk *chunk)
{
  debug("mem_chunk_push\n");
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
mem_chunk_pop(struct memory_chunk *chunk, void *block)
{
  debug("mem_chunk_pop\n");
  void *dataBlock = chunk->block + sizeof(u8) * chunk->max;
  u64 index = (block - dataBlock) / chunk->size;
  u8 *flag = chunk->block + sizeof(u8) * index;
  *flag = 0;
}

static void *
mem_push(struct memory_block *mem, u64 size)
{
  assert(mem->used + size <= mem->total);
  void *result = mem->block + mem->used;
  mem->used += size;
  return result;
}

static struct memory_chunk *
mem_push_chunk(struct memory_block *mem, u64 size, u64 max)
{
  struct memory_chunk *chunk = mem_push(mem, sizeof(*chunk) + max * sizeof(u8) + max * size);
  chunk->block = chunk + sizeof(*chunk);
  chunk->size = size;
  chunk->max = max;
  for (u64 index = 0; index < chunk->max; index++) {
    u8 *flag = chunk->block + sizeof(u8) * index;
    *flag = 0;
  }
  return chunk;
}

static inline u8
libevdev_is_gamepad(struct libevdev *evdev)
{
  // see: https://www.kernel.org/doc/Documentation/input/gamepad.txt
  //      3. Detection
  return libevdev_has_event_type(evdev, EV_KEY) && libevdev_has_event_code(evdev, EV_KEY, BTN_GAMEPAD);
}

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
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    context->wl_compositor = wl_registry_bind(wl_registry, name, &wl_compositor_interface, version);
  }

  else if (strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0) {
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
main(void)
{
  int error_code = 0;
  struct wl_context context = {};

  // TODO: Read timeout from command line arguments
#if GAMEPAD_IDLE_INHIBIT_DEBUG
  u64 timeout = 3;
#else
  u64 timeout = 30;
#endif

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

  /* memory */
  struct memory_block memory_block = {};
  memory_block.total = 1 * KILOBYTES;
  memory_block.block = mmap(0, (size_t)memory_block.total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!memory_block.block) {
    fatal("you do not have 1k memory available.\n");
    error_code = GAMEPAD_ERROR_MEMORY;
    goto wayland_exit;
  }

  // TODO: make maxSupportedControllers configurable
  u32 maxSupportedControllers = 4;
  struct memory_chunk *MemoryForDeviceOpenEvents =
      mem_push_chunk(&memory_block, sizeof(struct op), maxSupportedControllers);
  struct memory_chunk *MemoryForJoystickPollEvents =
      mem_push_chunk(&memory_block, sizeof(struct op_joystick_poll), maxSupportedControllers);
  struct memory_chunk *MemoryForJoystickReadEvents =
      mem_push_chunk(&memory_block, sizeof(struct op_joystick_read), maxSupportedControllers);

#if GAMEPAD_IDLE_INHIBIT_DEBUG
  printf("total memory usage (in bytes):  %lu\n", memory_block.used);
  printf("total memory wasted (in bytes): %lu\n", memory_block.total - memory_block.used);
#endif

  /* io_uring */
  struct io_uring ring;
  if (io_uring_queue_init(maxSupportedControllers + 1 + 1 + 4, &ring, 0)) {
    error_code = GAMEPAD_ERROR_IO_URING_SETUP;
    goto wayland_exit;
  }

  /* notify when on wayland events */
  struct op waylandOp = {
      .type = OP_WAYLAND,
  };

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
  struct op_inotify_watch inotifyOp = {
      .type = OP_INOTIFY_WATCH,
  };

  int inotifyFd;
  int inotifyWatchFd;
  {
    inotifyFd = inotify_init1(IN_NONBLOCK);
    if (inotifyFd == -1) {
      error_code = GAMEPAD_ERROR_INOTIFY_SETUP;
      goto io_uring_exit;
    }

    inotifyWatchFd = inotify_add_watch(inotifyFd, "/dev/input", IN_CREATE | IN_ATTRIB);
    if (inotifyWatchFd == -1) {
      error_code = GAMEPAD_ERROR_INOTIFY_WATCH_SETUP;
      goto inotify_exit;
    }

    inotifyOp.fd = inotifyFd;
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

    char *path = dirent->d_name;
    if (!(path[0] == 'e' && path[1] == 'v' && path[2] == 'e' && path[3] == 'n' && path[4] == 't'))
      continue;

    if (path[5] == '2' && path[6] == '0') {
      int breakHere = 32;
      (void)breakHere;
    }

    struct op_joystick_poll stagedOp = {
        .type = OP_JOYSTICK_POLL,
    };

    stagedOp.fd = openat(inputDirFd, path, O_RDONLY | O_NONBLOCK);
    if (stagedOp.fd == -1) {
      warning("cannot open some event file\n");
      continue;
    }

    struct libevdev *evdev = libevdev_new();
    if (!evdev) {
      warning("libevdev\n");
      close(stagedOp.fd);
      continue;
    }

    if (libevdev_set_fd(evdev, stagedOp.fd) < 0) {
      warning("libevdev failed\n");
      close(stagedOp.fd);
      libevdev_free(evdev);
      continue;
    }

    /* detect joystick */
    b8 isGamepad = libevdev_is_gamepad(evdev);
    if (isGamepad) {
      printf("Input device name: \"%s\"\n", libevdev_get_name(evdev));
      printf("Input device ID: bus %#x vendor %#x product %#x\n", libevdev_get_id_bustype(evdev),
             libevdev_get_id_vendor(evdev), libevdev_get_id_product(evdev));
    }
    libevdev_free(evdev);
    if (!isGamepad) {
      close(stagedOp.fd);
      continue;
    }

    struct op_joystick_poll *submitOp = mem_chunk_push(MemoryForJoystickPollEvents);
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
      if (errno == EAGAIN)
        goto wait;
      fatal("io_uring\n");
      error_code = GAMEPAD_ERROR_IO_URING_WAIT;
      break;
    }

    struct op *op = io_uring_cqe_get_data(cqe);
    // NOTE: Ignore cqe with no data.
    //       eg. close() or cancel()
    if (op == 0)
      goto cqe_seen;

    if (!(op->type & OP_WAYLAND))
      wl_display_cancel_read(context.wl_display);

    /* on wayland events */
    if (op->type & OP_WAYLAND) {
      int revents = cqe->res;

      if (revents & POLLIN) {
        wl_display_read_events(context.wl_display);
      } else {
        wl_display_cancel_read(context.wl_display);
      }
    }

    /* on inotify events */
    else if (op->type & OP_INOTIFY_WATCH) {
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

      char *path = event->name;
      if (!(path[0] == 'e' && path[1] == 'v' && path[2] == 'e' && path[3] == 'n' && path[4] == 't'))
        continue;
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

      // TODO: Do not request openat() when gamepad disconnects!

      if (!(event->mask & IN_ATTRIB))
        goto cqe_seen;

      // - Try to open device
      struct op *submitOp = mem_chunk_push(MemoryForDeviceOpenEvents);
      submitOp->type = OP_DEVICE_OPEN;

      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_openat(sqe, inputDirFd, path, O_RDONLY | O_NONBLOCK, 0);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);
    }

    /* on device open events */
    else if (op->type & OP_DEVICE_OPEN) {
      // - Device open event is only one time,
      //   so release used memory
      mem_chunk_pop(MemoryForDeviceOpenEvents, op);

      // - When open failed, exit immediately
      b8 isOpenAtFailed = cqe->res < 0;
      if (isOpenAtFailed) {
        goto cqe_seen;
      }

      int fd = cqe->res;
      struct op_joystick_poll stagedOp = {
          .type = OP_JOYSTICK_POLL,
          .fd = fd,
      };

      // - Detect if connected device is gamepad
      struct libevdev *evdev;
      int rc = libevdev_new_from_fd(stagedOp.fd, &evdev);
      if (rc < 0) {
        warning("libevdev failed\n");
        goto error;
      }

      /* detect joystick */
      if (!libevdev_is_gamepad(evdev)) {
        warning("This device does not look like a gamepad\n");
        goto error;
      }

      printf("Input device name: \"%s\"\n", libevdev_get_name(evdev));
      printf("Input device ID: bus %#x vendor %#x product %#x\n", libevdev_get_id_bustype(evdev),
             libevdev_get_id_vendor(evdev), libevdev_get_id_product(evdev));
      libevdev_free(evdev);

      // - Queue poll on gamepad for input event
      struct op_joystick_poll *submitOp = mem_chunk_push(MemoryForJoystickPollEvents);
      *submitOp = stagedOp;
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_poll_add(sqe, submitOp->fd, POLLIN);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);

      goto cqe_seen;

    error:
      // NOTE: When device is not gamepad
      //   - Release resources
      if (evdev)
        libevdev_free(evdev);

      //   - Close file descriptor
      sqe = io_uring_get_sqe(&ring);
      io_uring_prep_close(sqe, stagedOp.fd);
      io_uring_sqe_set_data(sqe, 0);
      io_uring_submit(&ring);
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

        //   2 - Release resources
        mem_chunk_pop(MemoryForJoystickPollEvents, op);

        //   3 - Stop trying to queue read joystick event
        goto cqe_seen;
      }

      // NOTE: When joystick event ready to be read, queue read event
      struct op_joystick_read stagedOp = {
          .type = OP_JOYSTICK_READ,
          .op_joystick_poll = op,
      };

      //   - Acquire memory for read event
      struct op_joystick_read *submitOp = mem_chunk_push(MemoryForJoystickReadEvents);
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

          //   2 - Release resources
          mem_chunk_pop(MemoryForJoystickPollEvents, op_joystick_poll);
          mem_chunk_pop(MemoryForJoystickReadEvents, op);

          //   3 - Stop polling on file descriptor
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
        mem_chunk_pop(MemoryForJoystickReadEvents, op);

        //   3 - Stop trying to read joystick another event
        goto cqe_seen;
      }

      // NOTE: On joystick event
      struct input_event *event = &op->event;

      printf("%p fd: %d time: %ld.%ld type: %d code: %d value: %d\n", op, op_joystick_poll->fd, event->input_event_sec,
             event->input_event_usec, event->type, event->code, event->value);
      // if (event->code == BTN_SELECT && event->value == 1)
      //   info("===> select pressed\n");
      // else if (event->code == BTN_SELECT && event->value == 0)
      //   info("===> select released\n");

      // else if (event->code == BTN_START && event->value == 1)
      //   info("===> start pressed\n");
      // else if (event->code == BTN_START && event->value == 0)
      //   info("===> start released\n");

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
          .tv_sec = timeout,
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
  close(inotifyFd);

io_uring_exit:
  io_uring_queue_exit(&ring);

wayland_exit:
  wl_display_disconnect(context.wl_display);

exit:
  return error_code;
}
