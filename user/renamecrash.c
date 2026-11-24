// renamecrash: helper for crash-testing rename.
//   renamecrash        set up, then rename forever (kill QEMU meanwhile)
//   renamecrash check  after reboot: check that every rename was all
//                      or nothing
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

static int
mkfile(char *path, char *data)
{
  int fd = open(path, O_CREATE | O_TRUNC | O_WRONLY);
  if (fd < 0)
    return -1;
  write(fd, data, strlen(data));
  close(fd);
  return 0;
}

static int
st(char *path, struct stat *s)
{
  return stat(path, s);
}

static int
check(void)
{
  struct stat sx, sy, sdx, sdy, sdd, sa, sf;
  int bad = 0;
  if (st("rc_x", &sx) < 0 || st("rc_y", &sy) < 0) {
    printf("rc FAIL: parent missing\n");
    return 1;
  }
  int inx = st("rc_x/d", &sdx) == 0, iny = st("rc_y/d", &sdy) == 0;
  if (inx + iny != 1) {
    printf("rc FAIL: rc_x/d %d rc_y/d %d (want exactly one)\n", inx, iny);
    return 1;
  }
  char *dd = inx ? "rc_x/d/.." : "rc_y/d/..";
  struct stat *par = inx ? &sx : &sy, *oth = inx ? &sy : &sx;
  if (st(dd, &sdd) < 0 || sdd.ino != par->ino) {
    printf("rc FAIL: .. of d does not name its parent\n");
    bad = 1;
  }
  if (par->nlink != 2 || oth->nlink != 1) {
    printf("rc FAIL: parent nlink %d other %d (want 2, 1)\n", par->nlink,
           oth->nlink);
    bad = 1;
  }
  if (st(inx ? "rc_x/d/f" : "rc_y/d/f", &sf) < 0 || sf.nlink != 1) {
    printf("rc FAIL: d/f missing or bad nlink\n");
    bad = 1;
  }
  if (st("rc_a", &sa) < 0 || sa.nlink != 1 || sa.size != 1) {
    printf("rc FAIL: rc_a missing or bad\n");
    bad = 1;
  }
  if (!bad)
    printf("rc OK: d in %s, nlinks %d/%d, rc_a intact\n", inx ? "rc_x" : "rc_y",
           sx.nlink, sy.nlink);
  return bad;
}

int
main(int argc, char *argv[])
{
  if (argc > 1 && strcmp(argv[1], "check") == 0)
    exit(check());

  if (mkdir("rc_x") < 0 || mkdir("rc_y") < 0 || mkdir("rc_x/d") < 0 ||
      mkfile("rc_x/d/f", "f") < 0 || mkfile("rc_a", "A") < 0) {
    printf("renamecrash: setup failed\n");
    exit(1);
  }
  printf("renamecrash: running\n");
  for (;;) {
    if (rename("rc_x/d", "rc_y/d") < 0 || rename("rc_y/d", "rc_x/d") < 0) {
      printf("renamecrash: rename failed\n");
      exit(1);
    }
    // replace rc_a with a fresh file: rc_a must never be missing
    mkfile("rc_t", "B");
    if (rename("rc_t", "rc_a") < 0) {
      printf("renamecrash: replace failed\n");
      exit(1);
    }
  }
}
