/*
 *  Copyright (C) 2024 CS416/CS518 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
bitmap_t inodeBitmap;
bitmap_t blkdataBitmap;
struct superblock *superblock;


/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	// Step 1: Read inode bitmap from disk
	bio_read(superblock->i_bitmap_blk, inodeBitmap);
	
	// Step 2: Traverse inode bitmap to find an available slot
	for (int i = 0; i < MAX_INUM; i++) {
		if (get_bitmap(inodeBitmap, i) == 0) {
			// Step 3: Update inode bitmap and write to disk 
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
	// Step 1: Read data block bitmap from disk
	bio_read(superblock->i_bitmap_blk, inodeBitmap);
	
	// Step 2: Traverse data block bitmap to find an available slot
	// Step 3: Update data block bitmap and write to disk 
	for (int i = 0; i < MAX_DNUM; i++) {
		if (get_bitmap(blkdataBitmap, i) == 0) {
			set_bitmap(blkdataBitmap, i);
			bio_write(superblock->d_bitmap_blk, blkdataBitmap);

			return superblock->d_start_blk + i;
		}

	}

	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) { //GPT

  // Step 1: Get the inode's on-disk block number
	int block =  ino / (BLOCK_SIZE / sizeof(struct inode)) + superblock->i_start_blk;
	int offset = ino % (BLOCK_SIZE / sizeof(struct inode));

  // Step 2: Get offset of the inode in the inode on-disk block
	struct inode tempblock[BLOCK_SIZE / sizeof(struct inode)];

  // Step 3: Read the block from disk and then copy into inode structure
	bio_read(block, tempblock);
	*inode = tempblock[offset];

	return 0;
}

int writei(uint16_t ino, struct inode *inode) { //GPT

	// Step 1: Get the block number where this inode resides on disk
	int block =  ino / (BLOCK_SIZE / sizeof(struct inode)) + superblock->i_start_blk;
    int offset = ino % (BLOCK_SIZE / sizeof(struct inode));
	
	// Step 2: Get the offset in the block where this inode resides on disk
	struct inode tempblock[BLOCK_SIZE / sizeof(struct inode)];
	bio_read(block, tempblock);

	tempblock[offset] = *inode;

	// Step 3: Write inode to disk 
	bio_write(block, tempblock);

	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode *curri = malloc(sizeof(struct inode));
	readi(ino, curri);
  // Step 2: Get data block of current directory from inode

	struct dirent *currd = malloc(BLOCK_SIZE * sizeof(struct inode));

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure
	
	for(int i = 0; i < 16; i++) {
		bio_read(curri->direct_ptr[i], currd);

		for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
			if ((currd->valid) && (strcmp(currd->name, fname) == 0)) {
				*dirent = *currd;
				return 0;
			}
			currd++;
		}
	}

	return -1;
}
// might need to add IF 0 stuff for above and below funcs
int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode

	struct dirent *currd = malloc(BLOCK_SIZE * sizeof(struct inode));
	
	// Step 2: Check if fname (directory name) is already used in other entries

	for (int i = 0; i < 16; i++) {
		bio_read(dir_inode.direct_ptr[i], currd);

		for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
			if ((currd->valid) && (strcmp(currd->name, fname) == 0)) {
				return -1;
			}
			currd++;
		}
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	struct dirent *dirEntry;
	int i;
	for (i = 0; i < 16; i++) {
		if (dir_inode.direct_ptr[i] == 0){
			dir_inode.direct_ptr[i] = get_avail_blkno();
			dir_inode.vstat.st_blocks++;
			struct inode *tempBlock = malloc(BLOCK_SIZE);
			bio_write(dir_inode.direct_ptr[i], tempBlock);
			free(tempBlock);

			// 0-terminating direct-
			if (i < 15) {
				dir_inode.direct_ptr[i + 1] = 0;
			}
		}

		bio_read(dir_inode.direct_ptr[i], currd);
		dirEntry = currd;

		for (int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++) {
			if (dirEntry->valid == 0) {
				dirEntry->ino = f_ino;
				strncpy(dirEntry->name, fname, name_len + 1);
				dirEntry->valid = 1;

				break;
			}
			dirEntry++;
		}

		if (dirEntry->valid == 1) {
			break;
		}
		
	}

	if (i == 16) return -1;
	// Update directory inode
	struct inode *update = malloc(sizeof(struct inode));
	*update = dir_inode;
	update->vstat.st_size += sizeof(struct dirent);
	update->size += sizeof(struct dirent);
	
	time(&update->vstat.st_mtime);

	// Write directory entry
	writei(update->ino, update);
	bio_write(dir_inode.direct_ptr[i], currd);
	

	return 0;
}

// Required for 518
int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	 if (path == NULL || strcmp(path, "/") == 0) {
        readi(ino, inode);
        return 0;
    }

	char *path_copy = strdup(path);

	// Recursive case: split the path into head and tail
    char *head = strtok(path_copy, "/");
    char *tail = strtok(NULL, "");

	 // Find the directory entry corresponding to the head
    struct dirent *entry = malloc(sizeof(struct dirent));
    if (dir_find(ino, head, strlen(head), entry) == -1) {
        free(entry);
		free(path_copy);
        return -1;
    }

	// Recurse on the tail with the new inode number
    int result = get_node_by_path(tail, entry->ino, inode);
    free(entry);
	free(path_copy);
    return result;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);

	// write superblock information

	superblock = malloc(BLOCK_SIZE);

	superblock->i_bitmap_blk = 1;
	superblock->d_bitmap_blk = 2;
	superblock->i_start_blk = 3;
	superblock->d_start_blk = ((MAX_INUM) * sizeof(struct inode) / BLOCK_SIZE) + 3;
	superblock->magic_num = MAGIC_NUM;
	superblock->max_dnum = MAX_DNUM;
	superblock->max_inum = MAX_INUM;

	// update bitmap information for root directory
	inodeBitmap = malloc(BLOCK_SIZE);
	blkdataBitmap = malloc(BLOCK_SIZE);

	set_bitmap(inodeBitmap, 0);
	set_bitmap(blkdataBitmap, 0);

	bio_write(superblock->i_bitmap_blk, inodeBitmap);
	bio_write(superblock->d_bitmap_blk, blkdataBitmap);

	// update inode for root directory
	struct stat* rootStat = malloc(sizeof(struct stat));
	rootStat->st_mode = __S_IFDIR | 0755;
	rootStat->st_blksize = BLOCK_SIZE;
	rootStat->st_blocks = 1;
	rootStat->st_nlink = 2;
	time(&rootStat->st_mtime);
	
	struct inode* rootNode = malloc(BLOCK_SIZE);
	bio_read(superblock->i_start_blk, rootNode);
	rootNode->direct_ptr[0] = superblock->d_start_blk;
	rootNode->direct_ptr[1] = 0;
	rootNode->indirect_ptr[0] = 0;
	rootNode->ino = 0;
	rootNode->link = 0;
	rootNode->type = 1;
	rootNode->valid = 1;
	rootNode->vstat = *rootStat;

	bio_write(superblock->i_start_blk, rootNode);
	free(rootNode);
	
	struct dirent *rootDir = malloc(BLOCK_SIZE);
	rootDir->ino = 0;
	rootDir->valid = 1;
	char temp[3];
	temp[0] = '.'; temp[1] = '\0';
	strncpy(rootDir->name, temp, 2);

	struct dirent *parentDir = rootDir + 1;
	parentDir->ino = 0;
	parentDir->valid = 1;
	temp[1] = '.'; temp[2] = '\0';
	strncpy(parentDir->name, temp, 3);

	bio_write(superblock->d_start_blk, rootDir); 

	free(rootDir);

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	if (dev_open(diskfile_path) < 0) {rufs_mkfs(); return NULL;}
	

	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	superblock = malloc(BLOCK_SIZE);
	bio_read(0,superblock);
	inodeBitmap = malloc(BLOCK_SIZE);
	bio_read(superblock->i_bitmap_blk, inodeBitmap);
	blkdataBitmap = malloc(BLOCK_SIZE);
	bio_read(superblock->d_bitmap_blk, blkdataBitmap);
	


	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	free(inodeBitmap);
	free(blkdataBitmap);
	free(superblock);
	
	// Step 2: Close diskfile
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

	struct inode *in = malloc(sizeof(struct inode));

	if (get_node_by_path(path, 0, in) != 0) return -1; 

	*stbuf = in->vstat;
	stbuf->st_mode = __S_IFDIR | 0755;
	stbuf->st_nlink  = 2;
	time(&stbuf->st_mtime);
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid(); 
	stbuf->st_atime = time(NULL);
	stbuf->st_mtime = time(NULL);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode *in = malloc(sizeof(struct inode));
	// Step 2: If not find, return -1

	if ((get_node_by_path(path, 0, in) == 0) && (in->valid)) {
		free(in);
		return 0;
	}
	free(in);
    return -1;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode *in = malloc(sizeof(struct inode));
	if (get_node_by_path(path, 0, in) != 0) {
		return -1;
	} 

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	for(int i = 0; i<16; i++) {

		struct dirent * entry = malloc(BLOCK_SIZE);
		bio_read(in->direct_ptr[i], entry);
		for(int j = 0; j < (BLOCK_SIZE / sizeof(struct dirent)); j++){
			if (entry->valid == 1) {
				struct inode * k = malloc(sizeof(struct inode));
				readi(entry->ino, k);
				filler(buffer, entry->name, &k->vstat, 0);
			}
			entry++;
		}
	}

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	char * dirPath = malloc(strlen(path) + 1);
	strcpy(dirPath, path);
	dirname(dirPath);
	//printf("dirPath: %s\n", dirPath);
	char * baseName = malloc(strlen(path) + 1);
	strcpy(baseName, path);
	//printf("Before baseName %s\n", baseName);
	baseName = basename(baseName);
	//printf("baseName: %s\n", baseName);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * pNode = malloc(sizeof(struct inode));
	if (get_node_by_path(dirPath, 0, pNode) != 0) {
		return -1;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int avail = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	dir_add(*pNode, avail, (const char *)baseName, (size_t)strlen(baseName));

	// Step 5: Update inode for target directory
	struct inode * update = malloc(sizeof(struct inode));
	update->valid = 1;
	update->ino = avail;
	update->link = 0;
	update->direct_ptr[0] = get_avail_blkno();
	struct inode * tempblock = malloc(BLOCK_SIZE);
	bio_write(update->direct_ptr[0], (void *)tempblock);
	free(tempblock);
	update->direct_ptr[1] = 0;
	update->indirect_ptr[0] = 0;
	update->type = 1;
	update->size = sizeof(struct dirent) * 2; // Unix convention
	struct stat * r = malloc(sizeof(struct stat));
	r->st_mode = __S_IFDIR | 0755; // Directory
	r->st_nlink = 1;
	r->st_ino = update->ino;
	time(&r->st_mtime);
	r->st_blocks = 1;
	r->st_blksize = BLOCK_SIZE;
	r->st_size = update->size;
	update->vstat = *r;
	free(r);

	// Step 6: Call writei() to write inode to disk
	writei(avail, update);

	struct dirent *rootDir = malloc(BLOCK_SIZE);
	rootDir->ino = avail;
	rootDir->valid = 1;
	char temp[3];
	temp[0] = '.'; temp[1] = '\0';
	strncpy(rootDir->name, temp, 2);

	struct dirent *parentDir = rootDir + 1;
	parentDir->valid = 1;
	temp[1] = '.'; temp[2] = '\0';
	strncpy(parentDir->name, temp, 3);

	bio_write(update->direct_ptr[0], (const void *)rootDir);
	free(rootDir);

	return 0;
}

// Required for 518
static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	char *dirPath = malloc(strlen(path) + 1);
	strcpy(dirPath, path);
	dirname(dirPath);

	char *baseName = malloc(strlen(path) + 1);
	strcpy(baseName, path);
	baseName = basename(baseName);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode *parent = malloc(sizeof(struct inode));
	if (get_node_by_path(dirPath, 0, parent) != 0) {
		return -1;
	}

	// Step 3: Call get_avail_ino() to get an available inode number
	int avail = get_avail_ino();

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	dir_add(*parent, avail, (const char *)baseName, strlen(baseName));

	// Step 5: Update inode for target file
	struct inode * update = malloc(sizeof(struct inode));
	update->valid = 1;
	update->ino = avail;
	update->link = 0;
	update->direct_ptr[0] = get_avail_blkno();
	update->direct_ptr[1] = 0;
	update->indirect_ptr[0] = 0;
	update->type = 0;
	update->size = 0;
	struct stat *ustat = malloc(sizeof(struct stat));
	ustat->st_mode = __S_IFREG | 0666;
	ustat->st_nlink = 1;
	ustat->st_ino = update->ino;
	ustat->st_size = update->size;
	ustat->st_blocks = 1;
	time(&ustat->st_mtime);
	update->vstat = *ustat;

	// Step 6: Call writei() to write inode to disk
	writei(avail, update);

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode *in = malloc(sizeof(struct inode));
	// Step 2: If not find, return -1
	if ((get_node_by_path(path, 0, in) == 0)&& (in->valid)) {
		free(in);
		return 0;
	}
	free(in);
	return -1;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	//printf("READ CALLED\n");

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode *node = malloc(sizeof(struct inode));
	if (get_node_by_path(path, 0, node) != 0) {

		return -ENOENT; // “No such file or directory.”

	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int i;
	char * b = malloc(BLOCK_SIZE);
	int fileSize = size + offset;
	int bytes = ((node->vstat.st_blocks * BLOCK_SIZE) - offset);
	for (i = 0; i < (int)size && i < bytes; i++) {

		if (offset >= fileSize) {

			break;

		}

		int blk = offset / BLOCK_SIZE;
		int off;

		if (blk > 15) { // Large file support
			off = (blk - 16) % (BLOCK_SIZE / sizeof(int)); // have to change offset first since block number gets changed
			blk = (blk - 16) / (BLOCK_SIZE / sizeof(int));
			bio_read(node->indirect_ptr[blk], b);
			int * w = *(int *)(b + (off * sizeof(int)));
			bio_read(w, b);

		} else {

			bio_read(node->direct_ptr[blk], b);
			
		}
		char * a = b;
		int j = 0;
		for (; i < (int)size && i < bytes; i++) {

			if (offset >= fileSize || j >= BLOCK_SIZE) {

				break;

			}

			char c = *b; // Need this or readi gets invalid argument
			*buffer = c;
			buffer++;
			offset++;
			j++;
			b++;

		}

		b = a;

	}

	// Step 3: copy the correct amount of data from offset to buffer
	i--;

	// Note: this function should return the amount of bytes you copied to buffer
	//printf("READ returning %d\n", i );
	return i;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode *node = malloc(sizeof(struct inode));
	
	if (get_node_by_path(path, 0, node) != 0) {
		return -1;
	}

	// Step 2: Based on size and offset, read its data blocks from disk
	int i;
	char *b = malloc(BLOCK_SIZE);
	int bytesWritten = 0;
	for (i = offset; i < (offset + size); i++) {

		int blk = i / BLOCK_SIZE;
		int w = 0;

		if (blk > 15) { // Large file support

			int off = (blk - 16) % (BLOCK_SIZE / sizeof(int)); // have to set offset first since block number gets changed
			blk = (blk - 16) / (BLOCK_SIZE / sizeof(int)); // indirect block number
			if (node->indirect_ptr[blk] == 0) {

				node->indirect_ptr[blk] = get_avail_blkno();
				if (blk < 7) {

					node->indirect_ptr[blk + 1] = 0;

				}

				struct inode * newBlock = (struct inode *)malloc(BLOCK_SIZE);
				bio_write(node->indirect_ptr[blk], newBlock);
				free(newBlock);

			}

			bio_read(node->indirect_ptr[blk], b);
			b += (off * sizeof(int));
			int temp = *(int *)b;

			if (temp == 0) {
				*(int *)b = get_avail_blkno(); // indirect pointer points to block of direct pointers
				temp = *(int *)b;
				node->vstat.st_blocks++;
			}

			b -= (off * sizeof(int));
			bio_write(node->indirect_ptr[blk], b);
			bio_read(temp, b);
			w = temp; 

		} else {

			if (node->direct_ptr[blk] == 0) {
				node->direct_ptr[blk] = get_avail_blkno();
				if (blk < 15) {
					node->direct_ptr[blk + 1] = 0;
				}
				struct inode * newBlock = (struct inode *)malloc(BLOCK_SIZE);
				bio_write(node->direct_ptr[blk], newBlock);
				node->vstat.st_blocks++;
				free(newBlock);
			}
			bio_read(node->direct_ptr[blk], b);
			w = node->direct_ptr[blk];
		}
		char *a = b;
		int j = 0;
		
		for (; i < (offset + size); i++) {

			*b = *buffer;
			node->size++;
			node->vstat.st_size++;
			b++;
			j++;
			buffer++;
			bytesWritten++;
			time(&node->vstat.st_mtime);

			if (j >= BLOCK_SIZE) {
				break;
			}

		}
		
		bio_write(w, a);
		b = a;

	}

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk
	writei(node->ino, node);

	// Note: this function should return the amount of bytes you write to disk

	//printf("TFS WRITE COMPLETE\n");
	free(node);
	return bytesWritten;
}

// Required for 518
static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

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

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

