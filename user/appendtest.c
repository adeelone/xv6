// appendtest: O_APPEND writes larger than one filewrite chunk must
// land in the file as one contiguous piece.
//
// filewrite() splits a write into chunks of at most 3072 bytes, each in
// its own transaction.  If the kernel only moves the offset to the end
// of file per chunk, another appender can slip its data in between two
// chunks of a large record, splitting it.
//
// usage: appendtest            run all tests
//        appendtest <name>     run tests whose name starts with <name>

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define BIG   8192   // large record: spans three filewrite chunks
#define SMALL 16     // small record: fits in one chunk

static char *curtest;
static int failures;

static char buf[BIG];

static void
fail(char *msg, int off)
{
  printf("  FAIL %s: %s (at offset %d)\n", curtest, msg, off);
  failures++;
}

// fork a child that opens path with O_APPEND and writes n records of
// size sz, each filled with byte c.  Returns the child's pid.
static int
appender(char *path, int n, int sz, char c)
{
  int pid = fork();
  if (pid < 0) {
    printf("appendtest: fork failed\n");
    exit(1);
  }
  if (pid == 0) {
    int fd = open(path, O_WRONLY | O_APPEND);
    if (fd < 0)
      exit(1);
    memset(buf, c, sz);
    for (int i = 0; i < n; i++)
      if (write(fd, buf, sz) != sz)
        exit(1);
    close(fd);
    exit(0);
  }
  return pid;
}

static int
waitall(int n)
{
  int ok = 1, st;
  for (int i = 0; i < n; i++) {
    wait(&st);
    if (st != 0)
      ok = 0;
  }
  return ok;
}

static int
mkempty(char *path)
{
  int fd = open(path, O_CREATE | O_TRUNC | O_WRONLY);
  if (fd < 0)
    return -1;
  close(fd);
  return 0;
}

// Walk the file record by record.  A record starts with its fill byte;
// it must continue with exactly the right number of that byte.  Counts
// records per kind.  Returns 0 if every record was whole.
static int
scan(char *path, char bigc[], int nbig, int counts[], char smallc, int *nsmall,
     int *total)
{
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    fail("cannot open file", 0);
    return -1;
  }
  int off = 0, bad = 0;
  char c;
  // read one byte at a time from a block-sized window
  static char win[1024];
  int wn = 0, wi = 0;
  int runc = -1, runlen = 0;

  for (int i = 0; i < nbig; i++)
    counts[i] = 0;
  *nsmall = 0;

  for (;;) {
    if (wi == wn) {
      wn = read(fd, win, sizeof(win));
      wi = 0;
      if (wn <= 0)
        break;
    }
    c = win[wi++];
    if (runc < 0) {
      runc = c;
      runlen = 0;
    }
    if (c != runc) {
      if (!bad)
        fail("record split by another append", off - runlen);
      bad = 1;
      runc = c;
      runlen = 0;
    }
    runlen++;
    off++;
    // is the current record complete?
    int want = (runc == smallc) ? SMALL : BIG;
    if (runlen == want) {
      if (runc == smallc)
        (*nsmall)++;
      else {
        int k;
        for (k = 0; k < nbig; k++)
          if (bigc[k] == runc)
            break;
        if (k == nbig) {
          if (!bad)
            fail("unexpected byte", off - 1);
          bad = 1;
        } else
          counts[k]++;
      }
      runc = -1;
    }
  }
  close(fd);
  if (runc >= 0 && !bad) {
    fail("partial record at end of file", off - runlen);
    bad = 1;
  }
  *total = off;
  return bad ? -1 : 0;
}

#define NBIG2 12  // per process in big_big: 2 * 12 * 8 KB = 192 KB

static void
big_big(void)
{
  char *path = "apt_bb";
  char bigc[2] = {'A', 'B'};
  int counts[2], nsmall, total;

  if (mkempty(path) < 0) {
    fail("create", 0);
    return;
  }
  appender(path, NBIG2, BIG, 'A');
  appender(path, NBIG2, BIG, 'B');
  if (!waitall(2))
    fail("an appender failed", 0);
  int r = scan(path, bigc, 2, counts, 's', &nsmall, &total);
  if (total != 2 * NBIG2 * BIG)
    fail("wrong file size", total);
  else if (r == 0 && (counts[0] != NBIG2 || counts[1] != NBIG2))
    fail("wrong record counts", total);
  unlink(path);
}

#define NBIG3   12   // large records in big_small
#define NSMALL3 400  // small records in big_small

static void
big_small(void)
{
  char *path = "apt_bs";
  char bigc[1] = {'L'};
  int counts[1], nsmall, total;

  if (mkempty(path) < 0) {
    fail("create", 0);
    return;
  }
  appender(path, NBIG3, BIG, 'L');
  appender(path, NSMALL3, SMALL, 's');
  if (!waitall(2))
    fail("an appender failed", 0);
  int r = scan(path, bigc, 1, counts, 's', &nsmall, &total);
  if (total != NBIG3 * BIG + NSMALL3 * SMALL)
    fail("wrong file size", total);
  else if (r == 0 && (counts[0] != NBIG3 || nsmall != NSMALL3))
    fail("wrong record counts", total);
  unlink(path);
}

// Ordinary (non-append) writers on the same file at the same time as
// big appends.  Checks only that nothing deadlocks and the appends
// still complete; the ordinary writer overwrites the start of the file.
static void
mixed_writers(void)
{
  char *path = "apt_mw";
  if (mkempty(path) < 0) {
    fail("create", 0);
    return;
  }
  appender(path, 8, BIG, 'A');
  appender(path, 8, BIG, 'B');
  int pid = fork();
  if (pid == 0) {
    int fd = open(path, O_WRONLY);
    if (fd < 0)
      exit(1);
    memset(buf, 'w', BIG);
    for (int i = 0; i < 16; i++) {
      if (write(fd, buf, BIG) != BIG)
        exit(1);
      close(fd);
      fd = open(path, O_WRONLY);
    }
    close(fd);
    exit(0);
  }
  if (!waitall(3))
    fail("a writer failed", 0);
  struct stat st;
  if (stat(path, &st) < 0 || st.size < 16 * BIG)
    fail("file too short", st.size);
  unlink(path);
}

struct test {
  void (*f)(void);
  char *name;
} tests[] = {
  {big_big, "big_big"},
  {big_small, "big_small"},
  {mixed_writers, "mixed_writers"},
  {0, 0},
};

static int
hasprefix(char *s, char *prefix)
{
  while (*prefix)
    if (*s++ != *prefix++)
      return 0;
  return 1;
}

int
main(int argc, char *argv[])
{
  char *prefix = argc > 1 ? argv[1] : "";
  int ran = 0;
  for (struct test *t = tests; t->name; t++) {
    if (!hasprefix(t->name, prefix))
      continue;
    int before = failures;
    curtest = t->name;
    printf("test %s: ", t->name);
    t->f();
    if (failures == before)
      printf("OK\n");
    ran++;
  }
  if (ran == 0)
    printf("appendtest: no test matches %s\n", prefix);
  else if (failures == 0)
    printf("appendtest: ALL %d TESTS PASSED\n", ran);
  else
    printf("appendtest: %d FAILURE(S)\n", failures);
  exit(failures != 0);
}
