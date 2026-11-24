//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->nlink >= NLINK_MAX) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  // dp may have been unlinked while we resolved it; linking into an
  // orphaned directory leaks ip (itrunc discards the record without
  // dropping ip->nlink).  create() has the same guard.
  if (dp->nlink == 0) {
    iunlockput(dp);
    goto bad;
  }
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode *
create(char *path, short type, short major, short minor)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if (dp->nlink == 0) {
    iunlockput(dp);
    return 0;
  }

  // a new directory's ".." would push dp->nlink past its maximum
  if (type == T_DIR && dp->nlink >= NLINK_MAX) {
    iunlockput(dp);
    return 0;
  }

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
  } else {
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  f->append = (omode & O_APPEND) && ip->type == T_FILE;

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64
sys_pipe(void)
{
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, p->sz, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, p->sz, fdarray + sizeof(fd0), (char *)&fd1,
              sizeof(fd1)) < 0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

// ---------------------------------------------------------------- rename
//
// rename(old, new) moves the directory entry old to new in ONE log
// transaction, so after a crash the disk shows either the old name or
// the new one.
//
// Lock order (see the PA5 report for the argument):
//   1. renamelock: a global sleep lock, taken BEFORE begin_op().  Only
//      rename moves directories, so while it is held the shape of the
//      directory tree cannot change.  That makes the ".." walks of the
//      subtree check stable, and stops two renames from building a
//      cycle together.
//   2. begin_op().
//   3. The two parent directories, ancestor first.  If neither is an
//      ancestor of the other, either order is safe (see report).
//   4. The source inode, then the target inode.  Before taking them we
//      have checked that neither child is an ancestor of a locked
//      parent, so every lock still goes from ancestor to descendant,
//      the same rule namex, create and unlink follow.
// The subtree checks themselves take at most one inode lock at a time,
// like namex, and run before any parent is locked.

static struct sleeplock renamelock;

void
renameinit(void)
{
  initsleeplock(&renamelock, "rename");
}

// Is a the same directory as d, or one of d's ancestors?
// Walks ".." from d toward the root, one inode lock at a time.
// Returns 1 if yes, 0 if no, -1 if d is not attached to the tree
// (e.g. it was unlinked).  Caller holds renamelock, so directories
// cannot move during the walk, and is inside a transaction.
static int
isancestor(struct inode *a, struct inode *d)
{
  struct inode *cur, *next;

  cur = idup(d);
  for (;;) {
    if (cur == a) {
      iput(cur);
      return 1;
    }
    if (cur->inum == ROOTINO) {
      iput(cur);
      return 0;
    }
    ilock(cur);
    if (cur->type != T_DIR || cur->nlink == 0) {
      iunlockput(cur);
      return -1;
    }
    next = dirlookup(cur, "..", 0);
    iunlockput(cur);
    if (next == 0)
      return -1;
    cur = next;
  }
}

// Overwrite the directory entry at offset off in dp. dp must be locked.
static int
setdirent(struct inode *dp, uint off, char *name, uint inum)
{
  struct dirent de;

  memset(&de, 0, sizeof(de));
  if (name)
    strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;
  return 0;
}

// Look name up in dp, taking dp's lock only for the lookup.
static struct inode *
lookup(struct inode *dp, char *name)
{
  struct inode *ip;

  ilock(dp);
  if (dp->type != T_DIR) {
    iunlock(dp);
    return 0;
  }
  ip = dirlookup(dp, name, 0);
  iunlock(dp);
  return ip;
}

#define RENAME_RETRIES 10

uint64
sys_rename(void)
{
  char old[MAXPATH], new[MAXPATH];
  char oname[DIRSIZ], nname[DIRSIZ];
  struct inode *odp, *ndp, *ip, *tp, *ip2, *tp2;
  struct inode *first, *second;
  uint ooff, noff;
  int r, isdir, tries, a;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  for (tries = 0; tries < RENAME_RETRIES; tries++) {
    odp = ndp = ip = tp = ip2 = tp2 = 0;
    first = second = 0;
    r = -1;

    acquiresleep(&renamelock); // before begin_op: see comment above
    begin_op();

    // ---- phase 1: resolve names, no inode locks held between steps
    if ((odp = nameiparent(old, oname)) == 0 ||
        (ndp = nameiparent(new, nname)) == 0)
      goto out;
    if (namecmp(oname, ".") == 0 || namecmp(oname, "..") == 0 ||
        namecmp(nname, ".") == 0 || namecmp(nname, "..") == 0)
      goto out;
    if (odp->dev != ndp->dev)
      goto out;
    if ((ip = lookup(odp, oname)) == 0) // old doesn't exist
      goto out;
    tp = lookup(ndp, nname); // may be 0
    if (tp == ip) { // same name, or two hard links: no-op
      r = 0;
      goto out;
    }

    // ---- phase 2: tree-shape checks, under renamelock only
    // new must not be inside old's subtree (also catches ip == ndp).
    if ((a = isancestor(ip, ndp)) != 0)
      goto out; // 1: would create a cycle; -1: ndp is detached
    // the target must not be old's parent or one of its ancestors,
    // e.g. rename("d/e/f", "d").  Such a target is a non-empty
    // directory, so this is always an error; checking it here keeps us
    // from ever locking an ancestor after its descendant.
    if (tp && isancestor(tp, odp) != 0)
      goto out;

    // ---- phase 3: lock the parents, ancestor first
    if (odp == ndp) {
      first = odp;
    } else if (isancestor(ndp, odp) == 1) {
      first = ndp;
      second = odp;
    } else {
      // odp is an ancestor of ndp, or the two are unrelated
      first = odp;
      second = ndp;
    }
    ilock(first);
    if (second)
      ilock(second);

    // Directory entries can change between phase 1 and now (create,
    // unlink, link don't take renamelock).  Look both names up again
    // under the locks; if either changed, start over.
    ip2 = dirlookup(odp, oname, &ooff);
    tp2 = dirlookup(ndp, nname, &noff);
    if (ip2 != ip || tp2 != tp) {
      iunlock(first);
      if (second)
        iunlock(second);
      first = second = 0;
      if (ip2)
        iput(ip2);
      if (tp2)
        iput(tp2);
      r = -2; // retry
      goto out;
    }
    iput(ip2);
    ip2 = 0;
    if (tp2) {
      iput(tp2);
      tp2 = 0;
    }

    // ---- phase 4: lock the children, check everything
    ilock(ip);
    if (tp)
      ilock(tp);

    if (ndp->nlink == 0) // new's parent was unlinked
      goto unlock;
    isdir = (ip->type == T_DIR);
    if (tp) {
      if ((tp->type == T_DIR) != isdir) // file <-> directory
        goto unlock;
      if (isdir && !isdirempty(tp)) // non-empty target directory
        goto unlock;
    } else if (isdir && odp != ndp && ndp->nlink >= NLINK_MAX) {
      goto unlock; // ndp would gain a ".." link it can't count
    }

    // ---- phase 5: modify.  Every check has passed.
    if (tp == 0) {
      // Add first: dirlink is the one step that can fail (no space),
      // and it fails before anything else has changed.
      if (dirlink(ndp, nname, ip->inum) < 0)
        goto unlock;
    } else {
      // Overwrite new's entry in place: new never disappears.
      if (setdirent(ndp, noff, nname, ip->inum) < 0)
        panic("rename: setdirent new");
    }
    if (setdirent(odp, ooff, 0, 0) < 0) // remove old's entry
      panic("rename: setdirent old");

    if (isdir && odp != ndp) {
      // the moved directory's ".." now names its new parent
      uint dotdot;
      struct inode *pp = dirlookup(ip, "..", &dotdot);
      if (pp == 0)
        panic("rename: no ..");
      iput(pp); // pp is odp; we hold another reference to it
      if (setdirent(ip, dotdot, "..", ndp->inum) < 0)
        panic("rename: setdirent ..");
      odp->nlink--; // lost ip's ".."
      ndp->nlink++; // gained ip's ".."
    }
    if (tp) {
      if (tp->type == T_DIR)
        ndp->nlink--; // tp's ".." is gone
      tp->nlink--;    // the replaced inode loses one link
      iupdate(tp);
    }
    iupdate(odp);
    if (ndp != odp)
      iupdate(ndp);
    r = 0;

  unlock:
    if (tp)
      iunlock(tp);
    iunlock(ip);
    iunlock(first);
    if (second)
      iunlock(second);
    first = second = 0;

  out:
    // iput(tp) frees the replaced inode if it has no links and no open
    // descriptors; those writes belong to this same transaction.
    if (tp)
      iput(tp);
    if (ip)
      iput(ip);
    if (odp)
      iput(odp);
    if (ndp)
      iput(ndp);
    end_op();
    releasesleep(&renamelock);

    if (r != -2)
      return r;
  }
  return -1;
}
