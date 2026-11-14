/**
 * @file clock.c
 * @brief Shared virtual clock and event-batch gates for native Slurm
 * validation.
 * @details Scheduler time is virtual; monotonic and RPC timeout clocks stay
 * real. Releasing a batch does not order main scheduling before backfill.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/* Scheduling uses integer virtual seconds. Monotonic/RPC time stays real. */
struct validation_clock {
  _Atomic int64_t seconds;
  _Atomic uint64_t microticks;
  _Atomic uint32_t phase; /* 0 idle, 1 event batch, 2 backfill, 3 dispatch */
  _Atomic uint64_t dispatch_generation; /* completed main-dispatch passes */
};
static struct validation_clock *shared;
static int batch_dispatch;
static int (*real_clock_gettime)(clockid_t, struct timespec *);
static int (*real_nanosleep)(const struct timespec *, struct timespec *);
static int (*real_cond_wait)(pthread_cond_t *, pthread_mutex_t *,
                             const struct timespec *);

/** @brief Report whether this process initialized the validation clock. */
int validation_clock_active(void) { return shared != NULL; }

__attribute__((constructor)) static void initialize(void) {
  real_clock_gettime = dlsym(RTLD_NEXT, "clock_gettime");
  real_nanosleep = dlsym(RTLD_NEXT, "nanosleep");
  real_cond_wait = dlsym(RTLD_NEXT, "pthread_cond_timedwait");
  const char *path = getenv("SLURM_VALIDATION_CLOCK_FILE");
  if (!path)
    return;
  const char *batch = getenv("SLURM_VALIDATION_BATCH_DISPATCH");
  batch_dispatch = batch && !strcmp(batch, "1");
  int fd = open(path, O_RDWR);
  if (fd < 0)
    abort();
  shared =
      mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (shared == MAP_FAILED)
    abort();
}

static void pause_real(void) {
  struct timespec delay = {0, 1000000};
  real_nanosleep(&delay, NULL);
}

/** @brief Wait for an event batch or main pass to finish, then hold backfill.
 */
void validation_enter_backfill(void) {
  uint32_t expected;
  do {
    expected = 0;
    if (atomic_compare_exchange_weak(&shared->phase, &expected, 2))
      return;
    pause_real();
  } while (1);
}
/** @brief Release the gate after a native backfill cycle. */
void validation_leave_backfill(void) { atomic_store(&shared->phase, 0); }

/**
 * @brief Hold main dispatch until the supplied same-time event batch is
 * applied. DR_EVT's trace loop makes known same-time arrivals eligible,
 * processes all completions/releases at that time, then dispatches. Streaming
 * callers that append arrivals after advancing can instead create a separate
 * dispatch cycle. Without this gate, dispatch can consume one release before
 * another simultaneous release arrives through its RPC, unlike DR_EVT's batch
 * processing. Enabled by SLURM_VALIDATION_BATCH_DISPATCH=1; native scheduling
 * decisions are unchanged. Queued arrivals do not start during the batch. After
 * release, main dispatch and backfill keep their independent triggers; this
 * gate does not order them.
 */
void validation_enter_dispatch(void) {
  if (!batch_dispatch)
    return;
  uint32_t expected;
  do {
    expected = 0;
    if (atomic_compare_exchange_weak(&shared->phase, &expected, 3))
      return;
    pause_real();
  } while (1);
}

/** Release the dispatch gate after native scheduling finishes. */
void validation_leave_dispatch(void) {
  if (batch_dispatch) {
    atomic_fetch_add(&shared->dispatch_generation, 1);
    atomic_store(&shared->phase, 0);
  }
}
/**
 * @brief Read the number of completed native main-dispatch passes.
 * @return A counter that advances when a gated main-dispatch pass finishes.
 */
uint64_t validation_dispatch_generation(void) {
  return atomic_load(&shared->dispatch_generation);
}
/**
 * @brief Hold both schedulers while applying events at the supplied timestamp.
 * @param seconds Absolute virtual time in whole seconds.
 * @details Keep the subsecond counter when the timestamp repeats, so startup
 * does not move gettimeofday() backward. Slurm uses that clock for dispatch
 * decisions even when dispatch throttling is disabled.
 */
void validation_set_time(int64_t seconds) {
  uint32_t expected;
  do {
    expected = 0;
    if (atomic_compare_exchange_weak(&shared->phase, &expected, 1))
      break;
    pause_real();
  } while (1);
  /* Repeating TIME at startup must not move gettimeofday() backwards:
   * Slurm compares it with the last dispatch even with throttling off. */
  if (atomic_load(&shared->seconds) != seconds) {
    atomic_store(&shared->seconds, seconds);
    atomic_store(&shared->microticks, 0);
  }
}
/** @brief Release both scheduler gates without prescribing their order. */
void validation_release_batch(void) { atomic_store(&shared->phase, 0); }

time_t time(time_t *result) {
  time_t value = shared ? atomic_load(&shared->seconds) : 0;
  if (!shared) {
    time_t (*real_time)(time_t *) = dlsym(RTLD_NEXT, "time");
    return real_time(result);
  }
  if (result)
    *result = value;
  return value;
}
int gettimeofday(struct timeval *tv, void *tz) {
  if (!shared) {
    int (*real_gettimeofday)(struct timeval *, void *) =
        dlsym(RTLD_NEXT, "gettimeofday");
    return real_gettimeofday(tv, tz);
  }
  tv->tv_sec = atomic_load(&shared->seconds);
  /* A strictly increasing subsecond value lets sched_min_interval=0 dispatch.
   */
  tv->tv_usec = atomic_fetch_add(&shared->microticks, 1) % 1000000;
  return 0;
}
int clock_gettime(clockid_t id, struct timespec *tv) {
  if (shared && id == CLOCK_REALTIME) {
    tv->tv_sec = atomic_load(&shared->seconds);
    tv->tv_nsec = 0;
    return 0;
  }
  if (!real_clock_gettime)
    real_clock_gettime = dlsym(RTLD_NEXT, "clock_gettime");
  return real_clock_gettime(id, tv);
}
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *deadline) {
  if (!shared)
    return real_cond_wait(cond, mutex, deadline);
  struct timespec real_deadline;
  real_clock_gettime(CLOCK_REALTIME, &real_deadline);
  real_deadline.tv_nsec += 1000000;
  if (real_deadline.tv_nsec >= 1000000000) {
    real_deadline.tv_sec++;
    real_deadline.tv_nsec -= 1000000000;
  }
  return real_cond_wait(cond, mutex, &real_deadline);
}
unsigned int sleep(unsigned int seconds) {
  if (shared) {
    pause_real();
    return 0;
  }
  unsigned int (*real_sleep)(unsigned int) = dlsym(RTLD_NEXT, "sleep");
  return real_sleep(seconds);
}
