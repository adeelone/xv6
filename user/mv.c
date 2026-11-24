// mv: rename a file or directory.
//   mv old new      rename old to new.  If new is an existing directory,
//                   old is moved into it as new/<last part of old>, like
//                   Unix mv.  Either way it is one atomic rename().
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/fs.h"
#include "kernel/param.h"
#include "user/user.h"

// last path component of path, ignoring trailing slashes
static char *
basename(char *path, char *buf)
{
  int n = strlen(path);
  while (n > 1 && path[n - 1] == '/')
    n--;
  int s = n;
  while (s > 0 && path[s - 1] != '/')
    s--;
  if (n - s >= DIRSIZ)
    n = s + DIRSIZ;
  memmove(buf, path + s, n - s);
  buf[n - s] = 0;
  return buf;
}

int
main(int argc, char *argv[])
{
  struct stat so, sn;
  char target[MAXPATH], base[DIRSIZ + 1];

  if (argc != 3) {
    fprintf(2, "usage: mv old new\n");
    exit(1);
  }
  char *old = argv[1], *new = argv[2];

  if (stat(old, &so) < 0) {
    fprintf(2, "mv: %s: no such file or directory\n", old);
    exit(1);
  }

  strcpy(target, new);
  if (stat(new, &sn) == 0 && sn.type == T_DIR && so.ino != sn.ino) {
    // new is a directory: move old into it
    basename(old, base);
    if (strlen(new) + 1 + strlen(base) + 1 > MAXPATH) {
      fprintf(2, "mv: path too long\n");
      exit(1);
    }
    strcpy(target, new);
    int n = strlen(target);
    if (n > 0 && target[n - 1] != '/')
      target[n++] = '/';
    strcpy(target + n, base);
  }

  if (rename(old, target) < 0) {
    fprintf(2, "mv: cannot move %s to %s\n", old, target);
    exit(1);
  }
  exit(0);
}
