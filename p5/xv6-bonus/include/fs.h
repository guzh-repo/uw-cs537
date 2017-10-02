#ifndef _FS_H_
#define _FS_H_

// On-disk file system format.
// Both the kernel and user programs use this header file.

// Block 0 is unused.
// Block 1 is super block.
// Inodes start at block 2.

#define ROOTINO 1  // root i-number
#define BSIZE 512  // block size

// File system super block
struct superblock {
  uint size;         // Size of file system image (blocks)
  uint nblocks;      // Number of data blocks
  uint ninodes;      // Number of inodes.
};

#define NDIRECT 6
#define NINDIRECT (BSIZE / sizeof(uint) / 2 - 1)
#define NDINDIRECT (BSIZE / sizeof(uint) / 2)
#define NDINDIR (BSIZE / sizeof(uint))
#define DINDIRECT_DIR_INDEX(x) ((x-NDIRECT-NINDIRECT)/NDINDIRECT)
#define DINDIRECT_INDEX(x) ((x-NDIRECT-NINDIRECT)%NDINDIRECT)
#define MAXFILE (NDIRECT + NINDIRECT + NDINDIR * NDINDIRECT)

// On-disk inode structure
struct dinode {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint size;            // Size of file (bytes)
  uint datab_idx[NDIRECT];
  uint datab_chk[NDIRECT];
  uint indirectb_idx;
};

union double_dinode {
    uint asdir[NDINDIR];
    struct {
        uint datab_idx[NINDIRECT];
        uint datab_chk[NINDIRECT];
        uint dindirect_dir;
        uint reserved;
    } asindirect;
    struct {
        uint datab_idx[NDINDIRECT];
        uint datab_chk[NDINDIRECT];
    } asdindirect;
};

// Inodes per block.
#define IPB           (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i)     ((i) / IPB + 2)

// Bitmap bits per block
#define BPB           (BSIZE*8)

// Block containing bit for block b
#define BBLOCK(b, ninodes) (b/BPB + ((ninodes-1)/IPB+1) + 2)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
  ushort inum;
  char name[DIRSIZ];
};

#endif // _FS_H_
