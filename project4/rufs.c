/* Project
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26


#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>



#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
// superblock
bitmap_t inodeBitmap;
bitmap_t dataBitmap;

struct superblock *superblock;

/*
 * Get available inode number from bitmap
 */
int get_avail_ino() {
    bio_read(superblock->i_bitmap_blk, inodeBitmap);

    for (int i = 0; i < MAX_INUM; ++i) {
        if (get_bitmap(inodeBitmap, i) == 0) {
            set_bitmap(inodeBitmap, i);
            bio_write(superblock->i_bitmap_blk, inodeBitmap);
            return i;
        }
    }

    return -1;
}

/*
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
    bio_read(superblock->d_bitmap_blk, dataBitmap);
    for (int i = 0; i < MAX_DNUM; ++i) {
        if (get_bitmap(dataBitmap, i) == 0) {
            set_bitmap(dataBitmap, i);
            bio_write(superblock->d_bitmap_blk, dataBitmap);

            return superblock->d_start_blk + i;
        }
    }
    return -1;
}

/*
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
    int block =
        ino / (BLOCK_SIZE / sizeof(struct inode)) + superblock->i_start_blk;
    int offset = ino % (BLOCK_SIZE / sizeof(struct inode));

    struct inode tempblock[BLOCK_SIZE / sizeof(struct inode)];

    bio_read(block, tempblock);

    *inode = tempblock[offset];
    return 0;
}

int writei(uint16_t ino, struct inode *inode) {
    int block =
        ino / (BLOCK_SIZE / sizeof(struct inode)) + superblock->i_start_blk;

    int offset = ino % (BLOCK_SIZE / sizeof(struct inode));

    struct inode *tempblock = malloc(BLOCK_SIZE);

    bio_read(block, tempblock);

    memcpy(tempblock + offset, inode, sizeof(struct inode));

    bio_write(block, tempblock);

    free(tempblock);

    return 0;
}

/*
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len,
             struct dirent *dirent) {
    struct inode *currenti = malloc(sizeof(struct inode));

    readi(ino, currenti);

    for (int i = 0; i < 16; i++) {
        if (currenti->direct_ptr[i] == 0) {
            free(currenti);
            return -1;
        }

        struct dirent *block = malloc(BLOCK_SIZE);
        bio_read(currenti->direct_ptr[i], block);

        struct dirent *entry = block;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); j++, entry++) {
            if (entry->valid && strcmp(entry->name, fname) == 0) {
                *dirent = *entry;
                free(block);
                free(currenti);
                return 0;
            }
        }
        free(block);
    }

    free(currenti);
    return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname,
            size_t name_len) {
    struct dirent *currentd = malloc(BLOCK_SIZE);

    for (int i = 0; i < 16; ++i) {
        if (dir_inode.direct_ptr[i] == 0) break;

        bio_read(dir_inode.direct_ptr[i], currentd);
        struct dirent *entry = currentd;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); ++j) {
            if (entry->valid && strcmp(entry->name, fname) == 0) {
                free(currentd);
                return -1;
            }
            entry++;
        }
    }

    bool added = false;
    for (int i = 0; i < 16 && !added; ++i) {
        if (dir_inode.direct_ptr[i] == 0) {
            dir_inode.direct_ptr[i] = get_avail_blkno();
            memset(currentd, 0, BLOCK_SIZE);
            bio_write(dir_inode.direct_ptr[i], currentd);
        } else {
            bio_read(dir_inode.direct_ptr[i], currentd);
        }

        struct dirent *entry = currentd;
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct dirent); ++j) {
            if (!entry->valid) {
                entry->ino = f_ino;
                strncpy(entry->name, fname, name_len);
                entry->name[name_len] = '\0';
                entry->valid = 1;
                bio_write(dir_inode.direct_ptr[i], currentd);
                added = true;
                break;
            }
            entry++;
        }
    }

    free(currentd);

    if (!added) return -1;

    dir_inode.size += sizeof(struct dirent);
    dir_inode.vstat.st_size += sizeof(struct dirent);
    time(&dir_inode.vstat.st_mtime);
    writei(dir_inode.ino, &dir_inode);

    return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {
    struct dirent *currentd = malloc(BLOCK_SIZE);

    int i;
    struct dirent *entry = NULL;
    bool found = false;

    for (i = 0; i < 16; ++i) {
        if (dir_inode.direct_ptr[i] == 0) break;

        bio_read(dir_inode.direct_ptr[i], currentd);
        entry = currentd;

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); ++j) {
            if (entry->valid && strcmp(entry->name, fname) == 0) {
                found = true;
                break;
            }
            entry++;
        }
        if (found) {
            break;
        }
    }

    if (!found) {
        free(currentd);
        return -1;
    }

    entry->valid = 0;
    bio_write(dir_inode.direct_ptr[i], currentd);

    dir_inode.size -= sizeof(struct dirent);
    dir_inode.vstat.st_size -= sizeof(struct dirent);
    writei(dir_inode.ino, &dir_inode);

    free(currentd);
    return 0;
}

/*
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
    if (path == NULL || strcmp(path, "/") == 0) {
        readi(ino, inode);
        return 0;
    }

    char *head = strtok(path, "/");
    char *tail = strtok(NULL, "");

    struct dirent *entry = malloc(sizeof(struct dirent));
    if (dir_find(ino, head, strlen(head), entry) == -1) {
        free(entry);
        return -1;
    }

    int result = get_node_by_path(tail, entry->ino, inode);
    free(entry);
    return result;
}


/*
 * Make file system
 */
/*
 * Make file system
 */
void init_superblock() {
	superblock = malloc(BLOCK_SIZE);
    superblock->i_bitmap_blk = 1;
    superblock->d_bitmap_blk = 2;
    superblock->i_start_blk = 3;
    superblock->d_start_blk =
        3 + ((sizeof(struct inode) * MAX_INUM) / BLOCK_SIZE);
    superblock->magic_num = MAGIC_NUM;
    superblock->max_inum = MAX_INUM;
    superblock->max_dnum = MAX_DNUM;
    bio_write(0, superblock);
}

void init_bitmaps() {
    inodeBitmap = malloc(BLOCK_SIZE);
    dataBitmap = malloc(BLOCK_SIZE);

    set_bitmap(inodeBitmap, 0);
    set_bitmap(dataBitmap, 0);

    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    bio_write(superblock->d_bitmap_blk, dataBitmap);
}

void init_root_inode(){
    struct inode *rootNode = malloc(BLOCK_SIZE);
    bio_read(superblock->i_start_blk, rootNode);
	rootNode->direct_ptr[0] = superblock->d_start_blk;
    rootNode->direct_ptr[1] = 0;
    rootNode->indirect_ptr[0] = 0;
	rootNode->ino = 0;
	rootNode->link = 0;
    rootNode->type = 1;
	rootNode->valid = 1;

    struct stat *r = malloc(sizeof(struct stat));
    r->st_mode = __S_IFDIR | 0755;
    r->st_nlink = 2;
    time(&r->st_mtime);
	r->st_blksize = BLOCK_SIZE;
    r->st_blocks = 1;
    rootNode->vstat = *r;
    bio_write(superblock->i_start_blk, rootNode);
    free(rootNode);
}

void init_root_dir(){
	struct dirent *rootDir = malloc(BLOCK_SIZE);
	rootDir->valid = 1;
    rootDir->ino = 0;
    char c[] = {'.', '\0'};
    strncpy(rootDir->name, c, 2);

    struct dirent *parent = rootDir + 1;
	parent->valid = 1;
    parent->ino = 0;
    char p[] = {'.', '.', '\0'};
    strncpy(parent->name, p, 3);

    bio_write(superblock->d_start_blk, rootDir);
    free(rootDir);
}

int rufs_mkfs() {
    dev_init(diskfile_path);

    // init stuff lol
    init_superblock();
	init_bitmaps();
	init_root_inode();
    init_root_dir();

    return 0;
}

/*
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {
    if (dev_open(diskfile_path) == -1) {
        rufs_mkfs();
        return NULL;
    }

    superblock = malloc(BLOCK_SIZE);
    inodeBitmap = malloc(BLOCK_SIZE);
    dataBitmap = malloc(BLOCK_SIZE);

	bio_read(0, superblock);
	bio_read(superblock->i_bitmap_blk, inodeBitmap);
    bio_read(superblock->d_bitmap_blk, dataBitmap);

    return NULL;
}

static void rufs_destroy(void *userdata) {
    free(superblock);
    free(inodeBitmap);
    free(dataBitmap);

    dev_close();
}
static int rufs_getattr(const char *path, struct stat *stbuf) {
	int rc = 0;
    struct inode *in = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, in) != 0) rc = -2;
    *stbuf = in->vstat;
    free(in);
    return rc;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
    int rc = -1;
    struct inode *in = malloc(sizeof(struct inode));
    if ((get_node_by_path(path, 0, in) == 0) && in->valid) rc = 0;
    free(in);
    return rc;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi) {
    struct inode *in = malloc(sizeof(struct inode));
	
    if (get_node_by_path(path, 0, in) != 0) return -2;

    for (int i = 0; i < 16; ++i) {
        if (in->direct_ptr[i] == 0) break;

        struct dirent *entry = malloc(BLOCK_SIZE);
        bio_read(in->direct_ptr[i], entry);

        for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); ++j, entry++) {
            if (entry->valid != 1) continue;
			
			struct inode *k = malloc(sizeof(struct inode));
			readi(entry->ino, k);
			filler(buffer, entry->name, &k->vstat, 0);
			free(k);
        }
    }
	
	free(in);
    return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {
    char *dirPath = strdup(path);
    char *dirName = strdup(path);
    char *parentPath = dirname(dirPath);
    char *targetName = basename(dirName);

    struct inode *parentNode = malloc(sizeof(struct inode));
    if (get_node_by_path(parentPath, 0, parentNode) != 0) {
        free(dirPath);
        free(dirName);
        free(parentNode);
        return -2;
    }

    int availInode = get_avail_ino();
    if (dir_add(*parentNode, availInode, targetName, strlen(targetName)) != 0) {
        free(dirPath);
        free(dirName);
        free(parentNode);
        return -5;
    }

    struct inode *newInode = malloc(sizeof(struct inode));
    memset(newInode, 0, sizeof(struct inode));
    newInode->valid = 1;
    newInode->ino = availInode;
    newInode->type = 1;
    newInode->direct_ptr[0] = get_avail_blkno();
    newInode->size = sizeof(struct dirent) * 2;

    struct stat st = {
        .st_mode = __S_IFDIR | mode,
        .st_nlink = 1,
        .st_ino = newInode->ino,
        .st_blocks = 1,
        .st_blksize = BLOCK_SIZE,
        .st_size = newInode->size
    };
    time(&st.st_mtime);
    newInode->vstat = st;

    writei(availInode, newInode);

    struct dirent entries[2] = {
        { .ino = availInode, .valid = 1, .name = "." },
        { .ino = parentNode->ino, .valid = 1, .name = ".." }
    };
    bio_write(newInode->direct_ptr[0], entries);

    free(newInode);
    free(dirPath);
    free(dirName);
    free(parentNode);

    return 0;
}

static int rufs_rmdir(const char *path) {
    char *pathCopy1 = strdup(path);
    char *pathCopy2 = strdup(path);

    char *dirPath = dirname(pathCopy1);
    char *baseName = basename(pathCopy2);

    struct inode *target = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, target) != 0) {
        free(pathCopy1);
        free(pathCopy2);
        free(target);
        return -2; 
    }

    for (int i = 0; i < 16 && target->direct_ptr[i] != 0; i++) {
        unset_bitmap(dataBitmap, target->direct_ptr[i] - superblock->d_start_blk);
    }
    bio_write(superblock->d_bitmap_blk, dataBitmap);

    target->valid = 0;
    unset_bitmap(inodeBitmap, target->ino);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    writei(target->ino, target);

    struct inode *parent = malloc(sizeof(struct inode));

    if (get_node_by_path(dirPath, 0, parent) != 0) {
        free(pathCopy1);
        free(pathCopy2);
        free(target);
        free(parent);
        return -2;
    }

    if (dir_remove(*parent, baseName, strlen(baseName)) != 0) {
        free(pathCopy1);
        free(pathCopy2);
        free(target);
        free(parent);
        return -5;
    }

    free(pathCopy1);
    free(pathCopy2);
    free(target);
    free(parent);
    return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode,
                       struct fuse_file_info *fi) {
    char dirPath[strlen(path) + 1];
    strcpy(dirPath, path);
    dirname(dirPath);

    char *baseName = malloc(strlen(path) + 1);
    strcpy(baseName, path);
    baseName = basename(baseName);

    struct inode parent;
    if (get_node_by_path(dirPath, 0, &parent) != 0) {
        return -ENOENT;
    }

    int avail = get_avail_ino();
    dir_add(parent, avail, baseName, strlen(baseName));

    struct inode update = {0};
    update.valid = 1;
    update.ino = avail;
    update.direct_ptr[0] = get_avail_blkno();

    struct stat ustat = {0};
    ustat.st_mode = __S_IFREG | 0644;
    ustat.st_nlink = 1;
    ustat.st_ino = update.ino;
    ustat.st_blocks = 1;
    time(&ustat.st_mtime);
    update.vstat = ustat;

    writei(avail, &update);

    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {
	int rc = -1;
    struct inode *in = malloc(sizeof(struct inode));
    if ((get_node_by_path(path, 0, in) == 0) && in->valid) {
        rc = 0;
    }
    free(in);
    return rc;
}


static int rufs_read(const char *path, char *buffer, size_t size, off_t offset,
                     struct fuse_file_info *fi) {
    struct inode *node = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, node) != 0) {
        free(node);
        return -2; 
    }

    int bytesRead = 0;
    char *block_buffer = malloc(BLOCK_SIZE);

    while (bytesRead < size) {
        int block_index = (offset / BLOCK_SIZE);
        int block_offset = offset % BLOCK_SIZE;
        int remaining = size - bytesRead;
        int bytesToRead = (BLOCK_SIZE - block_offset < remaining) ? BLOCK_SIZE - block_offset : remaining;

        if (block_index > 15) {
            int indirect_index = (block_index - 16) / (BLOCK_SIZE / sizeof(int));
            int indirect_offset = (block_index - 16) % (BLOCK_SIZE / sizeof(int));

            bio_read(node->indirect_ptr[indirect_index], block_buffer);
            int *block_ptr = (int *)(block_buffer + indirect_offset * sizeof(int));
            bio_read(*block_ptr, block_buffer);
        } else {
            bio_read(node->direct_ptr[block_index], block_buffer);
        }

        memcpy(buffer + bytesRead, block_buffer + block_offset, bytesToRead);
        bytesRead += bytesToRead;
        offset += bytesToRead;
    }

    free(block_buffer);
    free(node);

    return bytesRead;
}

static int rufs_write(const char *path, const char *buffer, size_t size,
                      off_t offset, struct fuse_file_info *fi) {
    struct inode *node = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, node) != 0) {
        free(node);
        return -2;
    }

    char *block_buffer = malloc(BLOCK_SIZE);

    int bytesWritten = 0;
    while (bytesWritten < size) {
        int block_index = (offset / BLOCK_SIZE);
        int block_offset = offset % BLOCK_SIZE;
        int remaining = size - bytesWritten;
        int bytesToWrite = (BLOCK_SIZE - block_offset < remaining) ? BLOCK_SIZE - block_offset : remaining;

        int actual_block;
        if (block_index > 15) {
            int indirect_index = (block_index - 16) / (BLOCK_SIZE / sizeof(int));
            int indirect_offset = (block_index - 16) % (BLOCK_SIZE / sizeof(int));
            if (node->indirect_ptr[indirect_index] == 0) {
                node->indirect_ptr[indirect_index] = get_avail_blkno();
                memset(block_buffer, 0, BLOCK_SIZE);
                bio_write(node->indirect_ptr[indirect_index], block_buffer);
            }
            bio_read(node->indirect_ptr[indirect_index], block_buffer);
            int *block_ptr = (int *)(block_buffer + indirect_offset * sizeof(int));
            if (*block_ptr == 0) {
                *block_ptr = get_avail_blkno();
                bio_write(node->indirect_ptr[indirect_index], block_buffer);
            }
            actual_block = *block_ptr;
        } else {
            if (node->direct_ptr[block_index] == 0) {
                node->direct_ptr[block_index] = get_avail_blkno();
                memset(block_buffer, 0, BLOCK_SIZE);
                bio_write(node->direct_ptr[block_index], block_buffer);
            }
            actual_block = node->direct_ptr[block_index];
        }

        bio_read(actual_block, block_buffer);
        memcpy(block_buffer + block_offset, buffer + bytesWritten, bytesToWrite);
        bio_write(actual_block, block_buffer);

        offset += bytesToWrite;
        bytesWritten += bytesToWrite;
        node->size = (node->size > offset) ? node->size : offset;
        node->vstat.st_size = node->size;
        node->vstat.st_blocks = (node->size + BLOCK_SIZE - 1) / BLOCK_SIZE;
        time(&node->vstat.st_mtime);
    }

    writei(node->ino, node);

    free(block_buffer);
    free(node);
    return bytesWritten;
}

static int rufs_unlink(const char *path) {
	char *baseName = malloc(strlen(path) + 1);
    strcpy(baseName, path);
    baseName = basename(baseName);

    char *dirPath = malloc(strlen(path) + 1);
    strcpy(dirPath, path);
    dirname(dirPath);

    struct inode *target = malloc(sizeof(struct inode));
    if (get_node_by_path(path, 0, target) != 0) return -2; 

    for (int i = 0; i < 8; ++i) {
        if (target->indirect_ptr[i] == 0) break;

		int *amongusBlock = malloc(BLOCK_SIZE);

        unset_bitmap(dataBitmap,
                     target->indirect_ptr[i] - superblock->d_start_blk);
        target->indirect_ptr[i] = 0;
        
        bio_read(target->indirect_ptr[i], amongusBlock);

        for (int j = 0; j < (BLOCK_SIZE / sizeof(int)); ++j) {
            if (*amongusBlock != 1) break;

            unset_bitmap(dataBitmap, *amongusBlock - superblock->d_start_blk);
			amongusBlock++;
        }
    }

    for (int i = 0; i < 16; ++i) {
        if (target->direct_ptr[i] == 0) break;

        unset_bitmap(dataBitmap,
                     target->direct_ptr[i] - superblock->d_start_blk);
        target->direct_ptr[i] = 0;
    }

    bio_write(superblock->d_bitmap_blk, dataBitmap);

    target->valid = 0;
    unset_bitmap(inodeBitmap, target->ino);
    bio_write(superblock->i_bitmap_blk, inodeBitmap);
    writei(target->ino, target);
    free(target);

    struct inode *parent = malloc(sizeof(struct inode));
    if (get_node_by_path(dirPath, 0, parent) != 0) return -2; 

    dir_remove(*parent, baseName, strlen(baseName));

    return 0;
}

static int rufs_truncate(const char *path, off_t size) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_flush(const char *path, struct fuse_file_info *fi) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static struct fuse_operations rufs_ope = {.init = rufs_init,
                                          .destroy = rufs_destroy,

                                          .getattr = rufs_getattr,
                                          .readdir = rufs_readdir,
                                          .opendir = rufs_opendir,
                                          .releasedir = rufs_releasedir,
                                          .mkdir = rufs_mkdir,
                                          .rmdir = rufs_rmdir,

                                          .create = rufs_create,
                                          .open = rufs_open,
                                          .read = rufs_read,
                                          .write = rufs_write,
                                          .unlink = rufs_unlink,

                                          .truncate = rufs_truncate,
                                          .flush = rufs_flush,
                                          .utimens = rufs_utimens,
                                          .release = rufs_release};

int main(int argc, char *argv[]) {
    int fuse_stat;

    getcwd(diskfile_path, PATH_MAX);
    strcat(diskfile_path, "/DISKFILE");

    fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

    return fuse_stat;
}
