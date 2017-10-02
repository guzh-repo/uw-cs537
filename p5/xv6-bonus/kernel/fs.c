// File system implementation.  Four layers:
//   + Blocks: allocator for raw disk blocks.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// Disk layout is: superblock, inodes, block in-use bitmap, data blocks.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "mmu.h"
#include "proc.h"
#include "spinlock.h"
#include "buf.h"
#include "fs.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
static void itrunc(struct inode*);

// Read the super block.
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  bwrite(bp);
  brelse(bp);
}

// Blocks.

// Allocate a disk block.
static uint
balloc(uint dev)
{
  int b, bi, m, bound;
  struct buf *bp;
  struct superblock sb;

  bp = 0;
  readsb(dev, &sb);
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb.ninodes));

    if(b+BPB > sb.size){ //last bitmap block
      bound = sb.size % BPB;
    } else {
      bound = BPB;
    }

    for(bi = 0; bi < bound; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use on disk.
        bwrite(bp);
        brelse(bp);
        return b + bi;
      }
    }
    brelse(bp);
  }

  //panic("balloc: out of blocks");
  return 0;
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  struct superblock sb;
  int bi, m;
  bzero(dev, b);

  readsb(dev, &sb);
  //cprintf("free block: 0x%x @ 0x%x\n", b, BBLOCK(b, sb.ninodes));
  bp = bread(dev, BBLOCK(b, sb.ninodes));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;  // Mark block free on disk.
  bwrite(bp);
  brelse(bp);
}

// Inodes.
//
// An inode is a single, unnamed file in the file system.
// The inode disk structure holds metadata (the type, device numbers,
// and data size) along with a list of blocks where the associated
// data can be found.
//
// The inodes are laid out sequentially on disk immediately after
// the superblock.  The kernel keeps a cache of the in-use
// on-disk structures to provide a place for synchronizing access
// to inodes shared between multiple processes.
//
// ip->ref counts the number of pointer references to this cached
// inode; references are typically kept in struct file and in proc->cwd.
// When ip->ref falls to zero, the inode is no longer cached.
// It is an error to use an inode without holding a reference to it.
//
// Processes are only allowed to read and write inode
// metadata and contents when holding the inode's lock,
// represented by the I_BUSY flag in the in-memory copy.
// Because inode locks are held during disk accesses,
// they are implemented using a flag rather than with
// spin locks.  Callers are responsible for locking
// inodes before passing them to routines in this file; leaving
// this responsibility with the caller makes it possible for them
// to create arbitrarily-sized atomic operations.
//
// To give maximum control over locking to the callers,
// the routines in this file that return inode pointers
// return pointers to *unlocked* inodes.  It is the callers'
// responsibility to lock them before using them.  A non-zero
// ip->ref keeps these unlocked inodes in the cache.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} icache;

void
iinit(void)
{
  initlock(&icache.lock, "icache");
}

static struct inode* iget(uint dev, uint inum);

// Allocate a new inode with the given type on device dev.
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;
  struct superblock sb;

  readsb(dev, &sb);
  for(inum = 1; inum < sb.ninodes; inum++){  // loop over inode blocks
    bp = bread(dev, IBLOCK(inum));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      bwrite(bp);   // mark it allocated on the disk
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  panic("ialloc: no inodes");
}

// Copy inode, which has changed, from memory to disk.
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->datab_idx, ip->datab_idx, sizeof(ip->datab_idx));
  memmove(dip->datab_chk, ip->datab_chk, sizeof(ip->datab_chk));
  dip->indirectb_idx = ip->indirectb_idx;
  bwrite(bp);
  brelse(bp);
}

// Find the inode with number inum on device dev
// and return the in-memory copy.
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&icache.lock);

  // Try for cached inode.
  empty = 0;
  for(ip = &icache.inode[0]; ip < &icache.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&icache.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Allocate fresh inode.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->flags = 0;
  release(&icache.lock);

  return ip;
}

// Increment reference count for ip.
// Returns ip to enable ip = idup(ip1) idiom.
struct inode*
idup(struct inode *ip)
{
  acquire(&icache.lock);
  ip->ref++;
  release(&icache.lock);
  return ip;
}

// Lock the given inode.
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquire(&icache.lock);
  while(ip->flags & I_BUSY)
    sleep(ip, &icache.lock);
  ip->flags |= I_BUSY;
  release(&icache.lock);

  if(!(ip->flags & I_VALID)){
    bp = bread(ip->dev, IBLOCK(ip->inum));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->datab_idx, dip->datab_idx, sizeof(ip->datab_idx));
    memmove(ip->datab_chk, dip->datab_chk, sizeof(ip->datab_chk));
    ip->indirectb_idx = dip->indirectb_idx;
    brelse(bp);
    ip->flags |= I_VALID;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// Unlock the given inode.
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !(ip->flags & I_BUSY) || ip->ref < 1)
    panic("iunlock");

  acquire(&icache.lock);
  ip->flags &= ~I_BUSY;
  wakeup(ip);
  release(&icache.lock);
}

// Caller holds reference to unlocked ip.  Drop reference.
void
iput(struct inode *ip)
{
  acquire(&icache.lock);
  if(ip->ref == 1 && (ip->flags & I_VALID) && ip->nlink == 0){
    // inode is no longer used: truncate and free inode.
    if(ip->flags & I_BUSY)
      panic("iput busy");
    ip->flags |= I_BUSY;
    release(&icache.lock);
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    acquire(&icache.lock);
    ip->flags = 0;
    wakeup(ip);
  }
  ip->ref--;
  release(&icache.lock);
}

// Common idiom: unlock, then put.
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

// Inode contents
//
// The contents (data) associated with each inode is stored
// in a sequence of blocks on the disk.  The first NDIRECT blocks
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in the block ip->addrs[NDIRECT].

// Return the disk block address of the nth block in inode ip.
// If there is no such block, bmap allocates one.
static uint
bmap(struct inode *ip, uint bn)
{
  //cprintf("bmap %d %d\n", ip->inum, bn);
  if (bn < NDIRECT) {
      if (ip->datab_idx[bn] == 0) {
          ip->datab_idx[bn] = balloc(ip->dev);
      }
      return ip->datab_idx[bn];
  } else if (bn < NDIRECT + NINDIRECT) {
      struct buf *bp;
      if (ip->indirectb_idx == 0) {
          ip->indirectb_idx = balloc(ip->dev);
      }
      bp = bread(ip->dev, ip->indirectb_idx);
      union double_dinode *indirect = (union double_dinode*)bp->data;
      if (indirect->asindirect.datab_idx[bn-NDIRECT] == 0) {
          indirect->asindirect.datab_idx[bn-NDIRECT] = balloc(ip->dev);
          bwrite(bp);
      }
      uint ret = indirect->asindirect.datab_idx[bn-NDIRECT];
      brelse(bp);
      return ret;
  } else if (bn < MAXFILE) {
      uint dir_idx = DINDIRECT_DIR_INDEX(bn);
      uint dindirect_idx = DINDIRECT_INDEX(bn);
      struct buf *buf_indirect;
      if (ip->indirectb_idx == 0) {
          ip->indirectb_idx = balloc(ip->dev);
      }
      buf_indirect = bread(ip->dev, ip->indirectb_idx);
      union double_dinode *indirect = (union double_dinode*)buf_indirect->data;
      if (indirect->asindirect.dindirect_dir == 0) {
          indirect->asindirect.dindirect_dir = balloc(ip->dev);
          bwrite(buf_indirect);
      }
      struct buf *buf_dir = bread(ip->dev, indirect->asindirect.dindirect_dir);
      union double_dinode *dir = (union double_dinode*) buf_dir->data;
      if (dir->asdir[dir_idx] == 0) {
          dir->asdir[dir_idx] = balloc(ip->dev);
          bwrite(buf_dir);
      }
      struct buf *buf_dindirect = bread(ip->dev, dir->asdir[dir_idx]);
      union double_dinode *dindirect = (union double_dinode*) buf_dindirect->data;
      if (dindirect->asdindirect.datab_idx[dindirect_idx] == 0) {
          dindirect->asdindirect.datab_idx[dindirect_idx] = balloc(ip->dev);
          bwrite(buf_dindirect);
      }
      uint ret = dindirect->asdindirect.datab_idx[dindirect_idx];
      brelse(buf_dindirect);
      brelse(buf_dir);
      brelse(buf_indirect);
      return ret;
  } else {
      panic("bmap: out of range");
  }
}

// return the checksum of `bn`th block of the inode
static uint get_checksum(struct inode *ip, uint bn) {
      if (bn < NDIRECT) {
          return ip->datab_chk[bn];
      } else if (bn < NDIRECT + NINDIRECT) {
          struct buf *bp;
          if (ip->indirectb_idx == 0) {
              panic("get_checksum: indirect not allocated");
          }
          bp = bread(ip->dev, ip->indirectb_idx);
          union double_dinode *indirect = (union double_dinode*)bp->data;
          uint ret = indirect->asindirect.datab_chk[bn-NDIRECT];
          brelse(bp);
          return ret;
      } else if (bn < MAXFILE) {
          uint dir_idx = DINDIRECT_DIR_INDEX(bn);
          uint dindirect_idx = DINDIRECT_INDEX(bn);
          struct buf *buf_indirect;
          if (ip->indirectb_idx == 0) {
              panic("get_checksum: indirect not allocated");
          }
          buf_indirect = bread(ip->dev, ip->indirectb_idx);
          union double_dinode *indirect = (union double_dinode*)buf_indirect->data;
          if (indirect->asindirect.dindirect_dir == 0) {
              panic("get_checksum: dindirect_dir not allocated");
          }
          struct buf *buf_dir = bread(ip->dev, indirect->asindirect.dindirect_dir);
          union double_dinode *dir = (union double_dinode*) buf_dir->data;
          if (dir->asdir[dir_idx] == 0) {
              panic("get_checksum: dindirect not allocated");
          }
          struct buf *buf_dindirect = bread(ip->dev, dir->asdir[dir_idx]);
          union double_dinode *dindirect = (union double_dinode*) buf_dindirect->data;
          uint ret = dindirect->asdindirect.datab_chk[dindirect_idx];
          brelse(buf_dindirect);
          brelse(buf_dir);
          brelse(buf_indirect);
          return ret;
      } else {
          panic("get_checksum: out of range");
      }
}

// write the checksum of `bn`th block of the inode
static void set_checksum(struct inode *ip, uint bn, uint checksum) {
    if (bn < NDIRECT) {
        ip->datab_chk[bn] = checksum;
        return;
    } else if (bn < NDIRECT + NINDIRECT) {
        struct buf *bp;
        if (ip->indirectb_idx == 0) {
            panic("get_checksum: indirect not allocated");
        }
        bp = bread(ip->dev, ip->indirectb_idx);
        union double_dinode *indirect = (union double_dinode*)bp->data;
        indirect->asindirect.datab_chk[bn-NDIRECT]=checksum;
        bwrite(bp);
        brelse(bp);
    } else if (bn < MAXFILE) {
        uint dir_idx = DINDIRECT_DIR_INDEX(bn);
        uint dindirect_idx = DINDIRECT_INDEX(bn);
        struct buf *buf_indirect;
        if (ip->indirectb_idx == 0) {
            panic("get_checksum: indirect not allocated");
        }
        buf_indirect = bread(ip->dev, ip->indirectb_idx);
        union double_dinode *indirect = (union double_dinode*)buf_indirect->data;
        if (indirect->asindirect.dindirect_dir == 0) {
            panic("get_checksum: dindirect_dir not allocated");
        }
        struct buf *buf_dir = bread(ip->dev, indirect->asindirect.dindirect_dir);
        union double_dinode *dir = (union double_dinode*) buf_dir->data;
        if (dir->asdir[dir_idx] == 0) {
            panic("get_checksum: dindirect not allocated");
        }
        struct buf *buf_dindirect = bread(ip->dev, dir->asdir[dir_idx]);
        union double_dinode *dindirect = (union double_dinode*) buf_dindirect->data;
        dindirect->asdindirect.datab_chk[dindirect_idx] = checksum;
        bwrite(buf_dindirect);
        brelse(buf_dindirect);
        brelse(buf_dir);
        brelse(buf_indirect);
    } else {
        panic("get_checksum: out of range");
    }
}

#define ADLER32_BASE 65521U

static inline uint adler32(void* data, uint len)
{
  uint i, a = 1, b = 0;

  for (i = 0; i < len; i++) {
    a = (a + ((uchar*)data)[i]) % ADLER32_BASE;
    b = (b + a) % ADLER32_BASE;
  }

  return (b << 16) | a;
}

static void free_dindirect_table(union double_dinode *dir, struct inode *ip) {
    for (int i=0;i<NDINDIR;i++) {
        if (dir->asdir[i]) {
            struct buf *buf_dindirect = bread(ip->dev,dir->asdir[i]);
            union double_dinode *dindirect = (union double_dinode *)buf_dindirect->data;
            for (int j=0;j<NDINDIRECT;j++) {
                if (dindirect->asdindirect.datab_idx[j]) {
                    bfree(ip->dev, dindirect->asdindirect.datab_idx[j]);
                    dindirect->asdindirect.datab_idx[j]=0;
                }
            }
            brelse(buf_dindirect);
            bfree(ip->dev, dir->asdir[i]);
            dir->asdir[i]=0;
        }
    }
}

// Truncate inode (discard contents).
// Only called after the last dirent referring
// to this inode has been erased on disk.
static void
itrunc(struct inode *ip)
{
  for(int i = 0; i < NDIRECT; i++){
    if(ip->datab_idx[i]){
      bfree(ip->dev, ip->datab_idx[i]);
      ip->datab_idx[i] = 0;
    }
  }

  if(ip->indirectb_idx){
    struct buf *bp = bread(ip->dev, ip->indirectb_idx);
    union double_dinode *indirect = (union double_dinode *)bp->data;
    for(int j = 0; j < NINDIRECT; j++){
        if(indirect->asindirect.datab_idx[j]) {
            bfree(ip->dev, indirect->asindirect.datab_idx[j]);
            indirect->asindirect.datab_idx[j]=0;
        }
    }
    if (indirect->asindirect.dindirect_dir) {
        struct buf *buf_dir = bread(ip->dev, indirect->asindirect.dindirect_dir);
        union double_dinode *dir = (union double_dinode *)buf_dir->data;
        free_dindirect_table(dir, ip);
        brelse(buf_dir);
        bfree(ip->dev, indirect->asindirect.dindirect_dir);
        indirect->asindirect.dindirect_dir = 0;
    }
    brelse(bp);
    bfree(ip->dev, ip->indirectb_idx);
    ip->indirectb_idx = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
  st->checksum = 0;
  if (ip->size == 0) return;
  for (uint i=0;i<(ip->size-1)/BSIZE+1;i++) {
      st->checksum ^= get_checksum(ip, i);
  }
}

// Read data from inode.
int
readi(struct inode *ip, char *dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].read)
      return -1;
    return devsw[ip->major].read(ip, dst, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint sector_number = bmap(ip, off/BSIZE);
    uint checksum_disk = get_checksum(ip, off/BSIZE);
    if(sector_number == 0){ //failed to find block
      panic("readi: trying to read a block that was never allocated");
    }

    bp = bread(ip->dev, sector_number);
    uint chk = adler32(bp->data, 512);
    // cprintf("block:%d checksum:0x%x checksum_disk:0x%x\n", sector_number, chk, checksum_disk);
    if (chk!=checksum_disk) {
        cprintf("Error: checksum mismatch, block %d\n", sector_number);
        brelse(bp);
        return -1;
    }
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(dst, bp->data + off%BSIZE, m);
    brelse(bp);
  }
  return n;
}

// Write data to inode.
int
writei(struct inode *ip, char *src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(ip->type == T_DEV){
    if(ip->major < 0 || ip->major >= NDEV || !devsw[ip->major].write)
      return -1;
    return devsw[ip->major].write(ip, src, n);
  }

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    n = MAXFILE*BSIZE - off;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint sector_number = bmap(ip, off/BSIZE);
    if(sector_number == 0){ //failed to find block
      n = tot; //return number of bytes written so far
      break;
    }

    bp = bread(ip->dev, sector_number);
    m = min(n - tot, BSIZE - off%BSIZE);
    memmove(bp->data + off%BSIZE, src, m);
    set_checksum(ip, off/BSIZE, adler32(bp->data, 512));
    iupdate(ip);
    bwrite(bp);
    brelse(bp);
  }

  if(n > 0 && off > ip->size){
    ip->size = off;
    iupdate(ip);
  }
  return n;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
// Caller must have already locked dp.
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct buf *bp;
  struct dirent *de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += BSIZE){
    uint sector_number = bmap(dp, off / BSIZE);
    bp = bread(dp->dev, sector_number);
    uint disk_chk = get_checksum(dp, off/BSIZE);
    uint chk = adler32(bp->data, BSIZE);
    if (disk_chk != chk) {
        cprintf("Error: checksum mismatch, block %d\n", sector_number);
        brelse(bp);
        return 0;
    }
    for(de = (struct dirent*)bp->data;
        de < (struct dirent*)(bp->data + BSIZE);
        de++){
      if(de->inum == 0)
        continue;
      if(namecmp(name, de->name) == 0){
        // entry matches path element
        if(poff)
          *poff = off + (uchar*)de - bp->data;
        inum = de->inum;
        brelse(bp);
        return iget(dp->dev, inum);
      }
    }
    brelse(bp);
  }
  return 0;
}

// Write a new directory entry (name, inum) into the directory dp.
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // Check that name is not present.
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // Look for an empty dirent.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, (char*)&de, off, sizeof(de)) != sizeof(de))
    panic("dirlink");

  return 0;
}

// Paths

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// Look up and return the inode for a path name.
// If parent != 0, return the inode for the parent and copy the final
// path element into name, which must have room for DIRSIZ bytes.
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;
  //cprintf("namex: %s\n", path);
  if(*path == '/')
    ip = iget(ROOTDEV, ROOTINO);
  else
    ip = idup(proc->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    iunlockput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
