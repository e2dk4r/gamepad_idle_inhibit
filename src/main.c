#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include <assert.h>
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

#include "idle-inhibit-unstable-v1-client-protocol.h"

#define POLLIN 0x001  /* There is data to read.  */
#define POLLPRI 0x002 /* There is urgent data to read.  */
#define POLLOUT 0x004 /* Writing now will not block.  */

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define OP_INOTIFY_WATCH (1 << 0)
#define OP_DEVICE_OPEN (1 << 1)
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

#define debug(str) write(STDERR_FILENO, "d: " str, 3 + sizeof(str) - 1)
#define fatal(str) write(STDERR_FILENO, "e: " str, 3 + sizeof(str) - 1)
#define warning(str) write(STDERR_FILENO, "w: " str, 3 + sizeof(str) - 1)
#define info(str) write(STDOUT_FILENO, "i: " str, 3 + sizeof(str) - 1)

struct op {
  u8 type;
  int fd;
};

struct op_device_open {
  u8 type;
  const char path[32];
};

struct op_joystick_read {
  u8 type;
  u8 initialized : 1;
  int fd;
  struct input_event event;
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
libevdev_is_joystick(struct libevdev *evdev)
{
  return libevdev_has_event_type(evdev, EV_ABS) && libevdev_has_event_code(evdev, EV_ABS, ABS_HAT0X);
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
  u64 timeout = 300;

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
  memory_block.total = 9 * KILOBYTES;
  memory_block.block = mmap(0, (size_t)memory_block.total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!memory_block.block) {
    fatal("you do not have 1k memory available.\n");
    error_code = GAMEPAD_ERROR_MEMORY;
    goto wayland_exit;
  }

  struct memory_chunk *MemoryForDeviceOpenEvents = mem_push_chunk(&memory_block, sizeof(struct op_device_open), 10);
  struct memory_chunk *MemoryForJoystickReadEvents =
      mem_push_chunk(&memory_block, sizeof(struct op_joystick_read), 256);
  printf("total memory usage (in bytes):  %llu\n", memory_block.used);
  printf("total memory wasted (in bytes): %llu\n", memory_block.total - memory_block.used);

  /* io_uring */
  struct io_uring ring;
  if (io_uring_queue_init(4, &ring, 0)) {
    error_code = GAMEPAD_ERROR_IO_URING_SETUP;
    goto wayland_exit;
  }

  /* notify when on wayland events */
  int fd_wl_display = wl_display_get_fd(context.wl_display);
  if (fd_wl_display < 0) {
    error_code = GAMEPAD_ERROR_WAYLAND_FD;
    goto io_uring_exit;
  }

  struct op waylandOp = {
      .type = OP_WAYLAND,
      .fd = fd_wl_display,
  };
  {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_poll_multishot(sqe, waylandOp.fd, POLLIN);
    io_uring_sqe_set_data(sqe, &waylandOp);
  }

  /* notify when a new input added */
  int fd_inotify = inotify_init1(IN_NONBLOCK);
  if (fd_inotify < 0) {
    error_code = GAMEPAD_ERROR_INOTIFY_SETUP;
    goto io_uring_exit;
  }

  int fd_watch = inotify_add_watch(fd_inotify, "/dev/input", IN_CREATE | IN_DELETE);
  if (fd_watch < 0) {
    error_code = GAMEPAD_ERROR_INOTIFY_WATCH_SETUP;
    goto inotify_exit;
  }

  struct op inotifyOp = {
      .type = OP_INOTIFY_WATCH,
      .fd = fd_inotify,
  };
  {
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_poll_multishot(sqe, inotifyOp.fd, POLLIN);
    io_uring_sqe_set_data(sqe, &inotifyOp);
  }

  /* add already connected joysticks to queue */

  DIR *dir = opendir("/dev/input");
  if (dir == 0) {
    error_code = GAMEPAD_ERROR_DEV_INPUT_DIR_OPEN;
    goto inotify_watch_exit;
  }

  struct dirent *dirent;
  u32 dirent_max = 1024;
  while (dirent_max--) {
    errno = 0;
    dirent = readdir(dir);

    /* error occured */
    if (errno != 0) {
      error_code = GAMEPAD_ERROR_DEV_INPUT_DIR_READ;
      closedir(dir);
      goto inotify_watch_exit;
    }

    /* end of directory stream is reached */
    if (dirent == 0)
      break;

    if (dirent->d_type != DT_CHR)
      continue;

    /* get full path */
    char path[32] = "/dev/input/";
    for (char *dest = path + 11, *src = dirent->d_name; *src; src++, dest++) {
      *dest = *src;
    }

    struct op_joystick_read stagedOp = {};
    stagedOp.type = OP_JOYSTICK_READ,

    stagedOp.fd = open(path, O_RDONLY | O_NONBLOCK);
    if (stagedOp.fd < 0)
      continue;

    struct libevdev *evdev;
    int rc = libevdev_new_from_fd(stagedOp.fd, &evdev);
    if (rc < 0) {
      warning("libevdev failed\n");
      close(stagedOp.fd);
      if (evdev)
        libevdev_free(evdev);
      continue;
    }

    /* detect joystick */
    if (!libevdev_is_joystick(evdev)) {
      close(stagedOp.fd);
      libevdev_free(evdev);
      continue;
    }

    printf("Input device name: \"%s\"\n", libevdev_get_name(evdev));
    printf("Input device ID: bus %#x vendor %#x product %#x\n", libevdev_get_id_bustype(evdev),
           libevdev_get_id_vendor(evdev), libevdev_get_id_product(evdev));

    struct op_joystick_read *submitOp = mem_chunk_push(MemoryForJoystickReadEvents);
    *submitOp = stagedOp;
    struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
    io_uring_prep_read(sqe, submitOp->fd, &submitOp->event, sizeof(submitOp->event), 0);
    io_uring_sqe_set_data(sqe, submitOp);

    libevdev_free(evdev);
  }
  closedir(dir);

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
      /* on error, finish the program */
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

      /* get full path */
      char path[32] = "/dev/input/";
      for (char *dest = path + 11, *src = event->name; *src; src++, dest++) {
        *dest = *src;
      }

      printf("--> %d %s %s\n", event->mask, event->name, path);

      if (event->mask & IN_DELETE)
        goto cqe_seen;

      struct op_device_open *submitOp = mem_chunk_push(MemoryForDeviceOpenEvents);
      submitOp->type = OP_DEVICE_OPEN;
      for (char *dest = (char *)submitOp->path, *src = path; *src; src++, dest++)
        *dest = *src;

      /* wait for device initialization */
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      struct __kernel_timespec *ts = &(struct __kernel_timespec){
          .tv_nsec = 75000000, /* 750ms */
      };
      io_uring_prep_timeout(sqe, ts, 0, 0);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);
    }

    else if (op->type & OP_DEVICE_OPEN) {
      if (cqe->res < 0 && cqe->res != -ETIME) {
        warning("waiting for device initialiation failed\n");
        mem_chunk_pop(MemoryForDeviceOpenEvents, op);
        goto cqe_seen;
      }

      struct op_device_open *op = io_uring_cqe_get_data(cqe);
      struct op_joystick_read stagedOp = {};
      stagedOp.type = OP_JOYSTICK_READ;
      stagedOp.fd = open(op->path, O_RDONLY | O_NONBLOCK);
      mem_chunk_pop(MemoryForDeviceOpenEvents, op);
      if (stagedOp.fd < 0) {
        warning("opening device failed\n");
        goto cqe_seen;
      }

      struct libevdev *evdev;
      int rc = libevdev_new_from_fd(stagedOp.fd, &evdev);
      if (rc < 0) {
        warning("libevdev failed\n");
        goto error;
      }

      /* detect joystick */
      if (!libevdev_is_joystick(evdev)) {
        warning("This device does not look like a joystick\n");
        goto error;
      }

      printf("Input device name: \"%s\"\n", libevdev_get_name(evdev));
      printf("Input device ID: bus %#x vendor %#x product %#x\n", libevdev_get_id_bustype(evdev),
             libevdev_get_id_vendor(evdev), libevdev_get_id_product(evdev));

      struct op_joystick_read *submitOp = mem_chunk_push(MemoryForJoystickReadEvents);
      *submitOp = stagedOp;
      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_read(sqe, submitOp->fd, &submitOp->event, sizeof(submitOp->event), 0);
      io_uring_sqe_set_data(sqe, submitOp);
      io_uring_submit(&ring);

      libevdev_free(evdev);
      goto cqe_seen;

    error:
      if (evdev)
        libevdev_free(evdev);
      sqe = io_uring_get_sqe(&ring);
      io_uring_prep_close(sqe, stagedOp.fd);
      io_uring_sqe_set_data(sqe, 0);
      io_uring_submit(&ring);
    }

    else if (op->type & OP_JOYSTICK_READ) {
      struct op_joystick_read *op = io_uring_cqe_get_data(cqe);
      struct input_event *event = &op->event;

      /* on joystick read error (eg. joystick removed), close the fd */
      if (cqe->res < 0 && cqe->res != -EAGAIN) {
        warning("cannot read events from device. maybe disconnected?\n");
        struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
        io_uring_prep_close(sqe, op->fd);
        io_uring_sqe_set_data(sqe, 0);
        io_uring_submit(&ring);
        mem_chunk_pop(MemoryForJoystickReadEvents, op);
        goto cqe_seen;
      }

      info("joystick event\n");
      if (!context.zwp_idle_inhibitor_v1) {
        info("timer set\n");
        context.zwp_idle_inhibitor_v1 =
            zwp_idle_inhibit_manager_v1_create_inhibitor(context.zwp_idle_inhibit_manager_v1, context.wl_surface);
      }

      struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
      io_uring_prep_cancel(sqe, &idledOp, 0);
      io_uring_sqe_set_data(sqe, 0);

      sqe = io_uring_get_sqe(&ring);
      struct __kernel_timespec *ts = &(struct __kernel_timespec){
          .tv_sec = timeout,
      };
      io_uring_prep_timeout(sqe, ts, 0, 0);
      io_uring_sqe_set_data(sqe, &idledOp);
      io_uring_submit(&ring);

      printf("%p fd: %d time: %ld.%ld type: %d code: %d value: %d\n", op, op->fd, event->input_event_sec,
             event->input_event_usec, event->type, event->code, event->value);

      /* read event again from gamepad device */
      sqe = io_uring_get_sqe(&ring);
      io_uring_prep_read(sqe, op->fd, &op->event, sizeof(op->event), 0);
      io_uring_sqe_set_data(sqe, op);
      io_uring_submit(&ring);
    }

    else if (op->type & OP_IDLED) {
      u8 isTimesUp = cqe->res == -ETIME;
      u8 isCancelled = cqe->res == -ECANCELED;
      if (cqe->res != 0 && !(isTimesUp || isCancelled)) {
        fatal("waiting for idle failed\n");
        break;
      }

      if (isCancelled) {
        info("reset timer\n");
        goto cqe_seen;
      }

      info("idled\n");
      zwp_idle_inhibitor_v1_destroy(context.zwp_idle_inhibitor_v1);
      context.zwp_idle_inhibitor_v1 = 0;
    }

  cqe_seen:
    io_uring_cqe_seen(&ring, cqe);
  }

inotify_watch_exit:
  close(fd_watch);

inotify_exit:
  close(fd_inotify);

io_uring_exit:
  io_uring_queue_exit(&ring);

wayland_exit:
  wl_display_disconnect(context.wl_display);

exit:
  return error_code;
}
