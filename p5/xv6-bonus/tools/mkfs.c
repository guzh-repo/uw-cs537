#include <stdio.h>
#include <malloc.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <dirent.h>

#define T_UNUSED 0
#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Special device

#define BLOCK_SIZE 512
#define NDIRECT 6 // how many direct data pointers in inode_t
#define NINDIRECT 63 // how many direct data pointers in indirect inode
#define NDINDIRECT 64 // how many data pointers in double_indirect_t
#define DINDIRECT_DIR_COUNT 128 // how many double indirect datablock in directory

#define DINDIRECT_DIR_INDEX(x) ((x-NDIRECT-NINDIRECT)/NDINDIRECT)
#define DINDIRECT_INDEX(x) ((x-NDIRECT-NINDIRECT)%NDINDIRECT)

#define MAX_FILE_SIZE ((NDIRECT + NINDIRECT + DINDIRECT_DIR_COUNT * NDINDIRECT)*BLOCK_SIZE)
#define disk_struct typedef struct __attribute__((__packed__))

disk_struct {
  uint32_t size;         // Size of file system image (blocks)
  uint32_t nblocks;      // Number of data blocks
  uint32_t ninodes;      // Number of inodes.
  uint8_t reserved[BLOCK_SIZE - 12];
} superblock_t;
static_assert(sizeof(superblock_t) == BLOCK_SIZE, "Incorrect superblock_t size");

disk_struct {
  short type;           // File type
  short major;          // Major device number (T_DEV only)
  short minor;          // Minor device number (T_DEV only)
  short nlink;          // Number of links to inode in file system
  uint32_t size;            // Size of file (bytes)
  uint32_t datab_idx[NDIRECT];
  uint32_t datab_chk[NDIRECT];
  uint32_t indirectb_idx;  // point to some indirect_t;
} inode_t;
static_assert(sizeof(inode_t) == 64, "Incorrect inode_t size");

disk_struct {
    uint32_t datab_idx[NINDIRECT];
    uint32_t datab_chk[NINDIRECT];
    uint32_t double_indirect_directory_idx;
    uint32_t reserved;
} indirect_t;
static_assert(sizeof(indirect_t) == BLOCK_SIZE, "Incorrect indirect_block_t size");

disk_struct {
    uint32_t double_indirect_idx[DINDIRECT_DIR_COUNT];
} double_indirect_directory_t;
static_assert(sizeof(double_indirect_directory_t) == BLOCK_SIZE, "Incorrect double_indirect_directory_t size");

disk_struct {
    uint32_t datab_idx[NDINDIRECT];
    uint32_t datab_chk[NDINDIRECT];
} double_indirect_t;
static_assert(sizeof(double_indirect_t) == BLOCK_SIZE, "Incorrect double_indirect_t size");

#define INODE_BLOCK_COUNT 32 // 32 blocks of inode_t (256 inodes)
#define INODE_COUNT (BLOCK_SIZE/sizeof(inode_t)*INODE_BLOCK_COUNT)
#define DATA_BLOCK_COUNT 16384 // how many data blocks
#define BITMAP_BLOCK_COUNT ((DATA_BLOCK_COUNT-1)/4096+1)
#define DATA_BLOCK_OFFSET (2 + INODE_BLOCK_COUNT + BITMAP_BLOCK_COUNT)

disk_struct {
    uint32_t next_inode; // only for mkfs
    uint32_t next_block; // only for mkfs
    uint8_t reserved_0[BLOCK_SIZE - 8]; // reserved for boot sector
    superblock_t superblock;
    inode_t inodes[INODE_COUNT];
    uint8_t bitmap[BITMAP_BLOCK_COUNT * BLOCK_SIZE];
    uint8_t datablocks[DATA_BLOCK_COUNT][BLOCK_SIZE];
} disk_t;
static_assert((sizeof(disk_t) % BLOCK_SIZE) == 0, "Incorrect disk_t size");

disk_struct {
    uint16_t inum;
    char name[14];
} directory_item_t;
static_assert((BLOCK_SIZE % sizeof(directory_item_t)) == 0, "Incorrect directory_item_t size");

#define ROOT_INODE_IDX 1

#define ADLER32_BASE 65521U

static inline uint32_t adler32(void* data, uint32_t len)
{
  uint32_t i, a = 1, b = 0;

  for (i = 0; i < len; i++) {
    a = (a + ((uint8_t*)data)[i]) % ADLER32_BASE;
    b = (b + a) % ADLER32_BASE;
  }

  return (b << 16) | a;
}

// alloc or return indirect_t block
indirect_t *alloc_indirect(disk_t *disk, inode_t *inode) {
    if (inode->indirectb_idx == 0) {
        uint32_t tmp = disk->next_block++;
        inode->indirectb_idx = tmp + DATA_BLOCK_OFFSET;
        return (indirect_t*)disk->datablocks[tmp];
    }
    return (indirect_t*)disk->datablocks[inode->indirectb_idx - DATA_BLOCK_OFFSET];
}

// block is the block_idx for the file
double_indirect_t *alloc_double_indirect(disk_t *disk, inode_t *inode, int block) {
    indirect_t *indirect = alloc_indirect(disk, inode);
    if (indirect->double_indirect_directory_idx == 0) {
        uint32_t tmp = disk->next_block++;
        indirect->double_indirect_directory_idx = tmp + DATA_BLOCK_OFFSET;
    }
    double_indirect_directory_t *dir = (double_indirect_directory_t*)disk->datablocks[indirect->double_indirect_directory_idx-DATA_BLOCK_OFFSET];
    uint32_t dir_idx = DINDIRECT_DIR_INDEX(block);
    if (dir->double_indirect_idx[dir_idx] == 0) {
        uint32_t tmp = disk->next_block++;
        dir->double_indirect_idx[dir_idx] = tmp + DATA_BLOCK_OFFSET;
    }
    double_indirect_t *dindirect = (double_indirect_t*)disk->datablocks[dir->double_indirect_idx[dir_idx]-DATA_BLOCK_OFFSET];
    return dindirect;
}

uint32_t push_inode_data_nth_block(disk_t *disk, inode_t *inode, void *data, int size, int block) {
    assert(size <= BLOCK_SIZE);
    uint32_t *checksum_slot = NULL;
    uint8_t *target = NULL;
    if (block < NDIRECT) { // block is pointed by inode_t
        assert(inode->datab_idx[block] == 0);
        uint32_t block_idx = disk->next_block++;
        inode->datab_idx[block] = block_idx + DATA_BLOCK_OFFSET;
        target = disk->datablocks[block_idx];
        checksum_slot = &inode->datab_chk[block];
    } else if (block < NDIRECT + NINDIRECT) { // pointed by indirect_t
        indirect_t *indirect = alloc_indirect(disk, inode);
        assert(indirect->datab_idx[block-NDIRECT] == 0);
        uint32_t block_idx = disk->next_block++;
        indirect->datab_idx[block-NDIRECT] = block_idx + DATA_BLOCK_OFFSET;
        target = disk->datablocks[block_idx];
        checksum_slot = &indirect->datab_chk[block-NDIRECT];
    } else { // put in double indirect
        double_indirect_t *dindirect = alloc_double_indirect(disk, inode, block);
        assert(dindirect->datab_idx[DINDIRECT_INDEX(block)] == 0);
        uint32_t block_idx = disk->next_block++;
        dindirect->datab_idx[DINDIRECT_INDEX(block)] = block_idx + DATA_BLOCK_OFFSET;
        target = disk->datablocks[block_idx];
        checksum_slot = &dindirect->datab_chk[DINDIRECT_INDEX(block)];
    }
    memcpy(target, data, size);
    *checksum_slot = adler32(target, BLOCK_SIZE);
    //printf("block %3lu checksum: 0x%x\n", ((long)target - (long)disk)/BLOCK_SIZE, *checksum_slot);
    return *checksum_slot;
}

// return file checksum
uint32_t push_inode_data(disk_t *disk, uint32_t inode_idx, void *data, int size) {
    assert(size <= MAX_FILE_SIZE);
    inode_t *inode = &disk->inodes[inode_idx];
    assert(inode->size == 0 && inode->datab_idx[0] == 0);
    inode->size = size;
    uint32_t file_checksum = 0;
    int block = 0;
    int offset = 0;
    while(offset < size) {
        int block_size = size-offset;
        if (block_size > BLOCK_SIZE) block_size = BLOCK_SIZE;
        uint32_t block_checksum = push_inode_data_nth_block(disk, inode, data + offset, block_size, block);
        block++; offset+=block_size;
        file_checksum ^= block_checksum;
    }
    return file_checksum;
}

disk_t* new_disk() {
    // init superblock
    disk_t *disk = malloc(sizeof(disk_t));
    memset(disk, 0, sizeof(disk_t));
    disk->superblock.size = sizeof(disk_t) / BLOCK_SIZE;
    disk->superblock.nblocks = DATA_BLOCK_COUNT;
    disk->superblock.ninodes = INODE_COUNT;
    disk->next_inode = 1;
    disk->next_block = 0;
    return disk;
}

uint32_t push_file(disk_t *disk, uint32_t parent_inode_idx, FILE *local_file) {
    fseek(local_file, 0, SEEK_END);
    long file_size = ftell(local_file);
    fseek(local_file, 0, SEEK_SET);

    void *file_content = malloc(file_size);
    fread(file_content, file_size, 1, local_file);

    uint32_t inode_idx = disk->next_inode++;
    inode_t *inode = &disk->inodes[inode_idx];
    inode->type = T_FILE;
    inode->nlink = 1;
    inode->size = 0;
    printf("file checksum: 0x%X\n", push_inode_data(disk, inode_idx, file_content, file_size));
    free(file_content);
    return inode_idx;
}

char* concat_path(const char* a, const char* b) {
    int len1 = strlen(a);
    int len2 = strlen(b);
    if (a[len1-1] == '/') len1--;
    char * ret = malloc((len1+len2+1)*sizeof(char));
    for (int i=0;i<len1;i++) ret[i] = a[i];
    ret[len1]='/';
    for (int i=0;i<len2;i++) ret[len1+1+i]=b[i];
    ret[len1+1+len2]='\0';
    return ret;
}

size_t file_count(DIR *dir) {
    assert(dir != NULL);
    rewinddir(dir);
    size_t count = 0;
    while (readdir(dir) != NULL) count ++;
    rewinddir(dir);
    printf("Dir count: %lu\n", count);
    return count;
}


uint32_t push_dir(disk_t *disk, uint32_t parent_inode_idx, DIR *local_dir, char *path_prefix) {
    printf("scan_dir %s\n", path_prefix);
    uint32_t inode_idx = disk->next_inode++;
    inode_t *inode = &disk->inodes[inode_idx];
    inode->type = T_DIR;
    inode->nlink = 1;
    inode->size = 0;

    directory_item_t *dir_list = malloc(sizeof(directory_item_t) * file_count(local_dir));
    dir_list[0].inum = inode_idx;
    dir_list[1].inum = parent_inode_idx;
    strcpy(dir_list[0].name, ".");
    strcpy(dir_list[1].name, "..");
    int item_count = 2;

    struct dirent *dir_item;
    while(NULL != (dir_item = readdir(local_dir))) {
        if (dir_item->d_type == DT_REG) {
            printf("push_file %s\n", dir_item->d_name);
            char *file_name_full = concat_path(path_prefix, dir_item->d_name);
            FILE *subfile = fopen(file_name_full, "rb");
            free(file_name_full);
            uint32_t sub_inode_idx = push_file(disk, inode_idx, subfile);
            fclose(subfile);
            dir_list[item_count].inum = sub_inode_idx;
            strcpy(dir_list[item_count].name, dir_item->d_name);
            item_count++;
        } else if (dir_item->d_type == DT_DIR) {
            if (strcmp(dir_item->d_name,".")==0||strcmp(dir_item->d_name,"..")==0) continue;
            char *file_name_full = concat_path(path_prefix, dir_item->d_name);
            DIR *subdir = opendir(file_name_full);
            uint32_t sub_inode_idx = push_dir(disk, inode_idx, subdir, file_name_full);
            free(file_name_full);
            closedir(subdir);
            dir_list[item_count].inum = sub_inode_idx;
            strcpy(dir_list[item_count].name, dir_item->d_name);
            item_count++;
        }
    }
    printf("push_dir %s\n", path_prefix);
    printf("dir checksum: 0x%x\n", push_inode_data(disk, inode_idx, dir_list, sizeof(directory_item_t)*item_count));
    //push_inode_data(disk, inode_idx, dir_list, sizeof(directory_item_t)*item_count);
    free(dir_list);
    return inode_idx;
}

void write_bitmap(disk_t *disk) {
    assert(disk->next_block <= DATA_BLOCK_COUNT);
    int bits = disk->next_block + 2 + INODE_BLOCK_COUNT + BITMAP_BLOCK_COUNT;
    int bitmap_idx = 0;
    while(bits > 0) {
        if (bits>8) {
            disk->bitmap[bitmap_idx++] = 0xFF;
            bits -= 8;
        } else {
            uint8_t x = 1<<bits;
            x -= 1;
            disk->bitmap[bitmap_idx] = x;
            bits = 0;
        }
    }
    disk->next_inode = 0;
    disk->next_block = 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s [fs.img] [root_dir]", argv[0]);
        return 1;
    }
    DIR *local_root = opendir(argv[2]);
    disk_t *disk = new_disk();
    char *path_prefix = concat_path(argv[2], "");
    push_dir(disk, ROOT_INODE_IDX, local_root, path_prefix);
    free(path_prefix);
    closedir(local_root);
    write_bitmap(disk);
    FILE *out = fopen(argv[1],"wb");
    fwrite(disk, 1, sizeof(disk_t), out);
    fclose(out);
    free(disk);
    return 0;
}
