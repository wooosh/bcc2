#define _GNU_SOURCE
#include "helper.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define RED "\033[0;31m"
#define BLUE "\033[0;34m"
#define WHITE_UNDERLINE "\033[1;37m"
#define RESET "\033[0m"

#define POOL_MAX_SZ 4294967296
#define POOL_CHUNK_SZ 4096

SourcePosition
combine_pos(SourcePosition pos1, SourcePosition pos2) {
  SourcePosition ret;
  ret.start = pos1.start;
  ret.sz = (pos2.start - pos1.start) + pos2.sz;
  return ret;
}

SourcePosition
make_pos(const uint8_t *buf, size_t sz) {
  SourcePosition ret = {.start = buf, .sz = sz};
  return ret;
}

int64_t
next_vn() {
  static int64_t reg = 1;
  return reg++;
}

void
log_err(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, RED "error" RESET ": ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
}

void
log_err_final(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, RED "error" RESET ": ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}

void
actual_log_internal_err(const char *fmt, const char *file, size_t line, ...) {
  va_list args;
  va_start(args, line);
  fprintf(stderr, BLUE "internal error: " RESET "%s:%zd: ", file, line);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  exit(EXIT_FAILURE);
}

void
log_source_err(const char *fmt, const uint8_t *base, SourcePosition pos, ...) {
  va_list args;
  va_start(args, pos);

  fprintf(stderr, RED "error" RESET ": ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  while ((pos.start--) != base && (*pos.start) != '\n') {
  }

  ; /* noop */
  pos.start++;
  fprintf(stderr, " | " WHITE_UNDERLINE);
  for (; (*pos.start) != '\n'; pos.start++) {
    putc(*pos.start, stderr);
  }
  fprintf(stderr, RESET "\n");
  exit(EXIT_FAILURE);
}

void
mempool_init(MemPool *pool) {
  pool->base =
      mmap(NULL, POOL_MAX_SZ, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pool->base == MAP_FAILED) {
    log_internal_err("unable to open mmap pool", NULL);
  }
  if (mprotect(pool->base, POOL_CHUNK_SZ, PROT_READ | PROT_WRITE) == -1) {
    log_internal_err("unable to block out memory in pool", NULL);
  }
  pool->size = 0;
  pool->alloc = POOL_CHUNK_SZ;
}

void
mempool_deinit(MemPool *pool) {
  if (munmap(pool->base, POOL_MAX_SZ) == -1) {
    log_internal_err("unable to close memory pool", NULL);
  }
  pool->base = NULL;
}

static inline size_t
size_needed(size_t requested) {
  size_t ret = 0;
  while (ret < requested) {
    ret += POOL_CHUNK_SZ;
  }
  return ret;
}

void *
mempool_alloc(MemPool *pool, size_t amount) {
  if (pool->size + amount + 1 >= pool->alloc) {
    size_t needed = size_needed(amount);
    if (pool->alloc + needed > POOL_MAX_SZ) {
      log_internal_err("out of memory in mmap pool", NULL);
    }
    mprotect(pool->base + pool->alloc, needed, PROT_READ | PROT_WRITE);
    pool->alloc += needed;
  }
  void *ret = pool->base + pool->size;
  pool->size += amount;
  return ret;
}

/* start this high, so that we don't have to copy too many times */
#define VEC_INIT_ALLOC 32

void
vector_init(Vector *vec, size_t it_sz, MemPool *pool) {
  vec->it_sz = it_sz;
  vec->items = 0;
  vec->alloc = VEC_INIT_ALLOC;
  vec->pool = pool;
  vec->data = mempool_alloc(pool, it_sz * VEC_INIT_ALLOC);
}

void
vector_init_size(Vector *vec, size_t it_sz, MemPool *pool, size_t items) {
  vec->it_sz = it_sz;
  vec->items = items;
  vec->alloc = items;
  vec->pool = pool;
  vec->data = mempool_alloc(pool, it_sz * vec->alloc);
}

void
vector_resize(Vector *vec) {
  uint8_t *new_data = mempool_alloc(vec->pool, vec->alloc * vec->it_sz * 2);
  memcpy(new_data, vec->data, vec->items * vec->it_sz);
  vec->data = new_data;
  vec->alloc *= 2;
}
void
vector_push(Vector *vec, void *data) {
  if (vec->items + 1 > vec->alloc) {
    vector_resize(vec);
  }
  memcpy(vec->data + (vec->items++ * vec->it_sz), data, vec->it_sz);
}

void
vector_remove(Vector *vec, size_t idx) {
  memmove(vec->data + idx * vec->it_sz, vec->data + (idx + 1) * vec->it_sz,
          (vec->items - idx) * vec->it_sz);
  vec->items--;
}

void
vector_insert(Vector *vec, size_t idx, void *data) {
  if (vec->items + 1 > vec->alloc) {
    vector_resize(vec);
  }
  memmove(vec->data + (idx + 1) * vec->it_sz, vec->data + idx * vec->it_sz,
          (vec->items - idx) * vec->it_sz);
  memcpy(vec->data + (idx * vec->it_sz), data, vec->it_sz);
  vec->items++;
}

void *
vector_idx(Vector *vec, size_t idx) {
  if (idx >= vec->items) {
    return NULL;
  }
  return vec->data + (idx * vec->it_sz);
}

void *
vector_alloc(Vector *vec) {
  if (vec->items + 1 > vec->alloc) {
    vector_resize(vec);
  }
  return vec->data + (vec->items++ * vec->it_sz);
}
