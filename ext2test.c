#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <assert.h>
#include "ext2fs.h"
#include "ext2_blockgroup.h"
#include "ext2_inode.h"
#include "ext2_dentry.h"

static const char const *test_file = "./hda.img";
static unsigned char *file_system;

static unsigned long get_file_size(void);
static void *map2memory(unsigned long size);
static const char *get_os_name(struct ext2_superblock *sb);
static void read_block_group(struct ext2_superblock *sb,  struct ext2_blockgroup *bg, unsigned long offset);
static void read_super_block(struct ext2_superblock *sb);
static u_int32_t blockid2address(struct ext2_superblock *sb, u_int32_t id);
static unsigned long get_block_data_address(struct ext2_superblock *sb, struct ext2_blockgroup *bg);
static int get_all_directories(struct dentry_list *head, unsigned long address, struct ext2_blockgroup *blk_group);
static u_int16_t read_dentry_rec_len(unsigned long address, unsigned long offset);
static void read_dentry(struct ext2_dentry *dentry, unsigned address, 
			unsigned long offset, u_int16_t rec_len);
static void directory_walk(struct dentry_list *head, struct ext2_superblock *sb, struct ext2_blockgroup *bg);
static u_int8_t get_file_type(struct ext2_dentry *dentry);

static unsigned long get_file_size(void)
{
	struct stat st;

	assert(stat(test_file, &st) != -1);

	return st.st_size;
}

static void *map2memory(unsigned long size)
{
	int fd;
	void *ret;

	fd = open(test_file, O_RDONLY);
	assert(fd >= 0);

	ret = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	assert(ret != MAP_FAILED);

	close(fd);

	return ret;
}

static u_int8_t get_file_type(struct ext2_dentry *dentry)
{
	return dentry->file_type;
}

static void directory_walk(struct dentry_list *head, struct ext2_superblock *sb, struct ext2_blockgroup *bg)
{
	struct dentry_list *p;
	u_int8_t ftype;
	unsigned long block_address = 0;
	unsigned long bass_address = blockid2address(sb, bg->bg_inode_table);

	for (p = head->next; p; p = p->next) {
		ftype = get_file_type(p->dentry);
		switch (ftype) {
		case EXT2_FT_UNKNOWN:
			printf("unknown file type %s\n", p->dentry->name);
			break;
		case EXT2_FT_REG_FILE:
			printf("%s is a regular file: inode[0x%x]\n", p->dentry->name, p->dentry->inode);
			block_address = (p->dentry->inode * sizeof(struct ext2_inode)) + bass_address;
			printf("file's block address is %lx(%x * %x + %x)\n", block_address, 
			       p->dentry->inode, sizeof(struct ext2_inode),
			       bass_address);
			break;
		case EXT2_FT_DIR:
			printf("%s is a directory: inode[0x%x]\n", p->dentry->name, p->dentry->inode);
			block_address = (p->dentry->inode * sizeof(struct ext2_inode)) + bass_address;
			printf("dir's block address is %lx\n", block_address);
			break;
		default:
			break;
		}
	}

}

static void read_dentry(struct ext2_dentry *dentry, unsigned address, 
			unsigned long offset, u_int16_t rec_len)
{
	memcpy(dentry, file_system + address + offset, rec_len);
}

static u_int16_t read_dentry_rec_len(unsigned long address, unsigned long offset)
{
	u_int16_t len = 0;

	// ignore inode.
	memcpy(&len, file_system + address + offset + 4, sizeof(len));

	return len;
}

static int get_all_directories(struct dentry_list *head, unsigned long address, struct ext2_blockgroup *blk_group)
{
	int i;
	u_int16_t rec_len;
	unsigned long offset = 0;
	u_int16_t count = blk_group->bg_used_dirs_count + 2; // Add "." and ".." to directory count.
	
	for (i = 0; i < count; i++) {
		struct ext2_dentry *dentry;
		struct dentry_list *p;

		rec_len = read_dentry_rec_len(address, offset);

		// added one byte for '\0'.
		dentry = malloc(rec_len + 1);
		assert(dentry != NULL);

		read_dentry(dentry, address, offset, rec_len);

		p = malloc(sizeof(*p));
		assert(p != NULL);

		p->dentry = dentry;
		p->next = head->next;
		head->next = p;

		printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
		printf("dentry->inode: 0x%x\n", dentry->inode);
		printf("dentry->rec_len: 0x%x\n", dentry->rec_len);
		printf("dentry->name_len:0x%x\n", dentry->name_len);
		printf("dentry->file_type:0x%x\n", dentry->file_type);
		printf("dentry->name:%s\n", dentry->name);
		printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");
		offset += rec_len;
	}

	return 0;
}

static unsigned long get_block_data_address(struct ext2_superblock *sb, struct ext2_blockgroup *bg)
{
	return (unsigned long) sb->s_inodes_per_group * sizeof(struct ext2_inode) 
				+  blockid2address(sb, bg->bg_inode_table);
}

static u_int32_t blockid2address(struct ext2_superblock *sb, u_int32_t id)
{
	return get_block_size(*sb) * id;
}

static void read_block_group(struct ext2_superblock *sb,  struct ext2_blockgroup *bg, unsigned long offset)
{
	memcpy(bg, file_system + offset, sizeof(struct ext2_blockgroup));
}

static void read_super_block(struct ext2_superblock *sb)
{
	// Super block starts at address 1024. 
	memcpy(sb, file_system + SUPER_BLOCK_SIZE, sizeof(struct ext2_superblock));
}

static const char *get_os_name(struct ext2_superblock *sb)
{
	static const char const *os[] = {
		"Linux",
		"GNU HURD",
		"MASIX",
		"FreeBSD",
		"Lites"
	};

	if (sb->s_creator_os < 0 || sb->s_creator_os > EXT2_OS_LITES)
		return "unknown os";

	return os[sb->s_creator_os];
}

int main(int argc, char **argv)
{
	unsigned long size = 0;
	int block_cnt = 0;
	struct ext2_superblock sb;
	struct ext2_blockgroup *block_group;
	struct dentry_list head;
	struct dentry_list *p, *q;
	int i;

	// Get HDD image file size and mmap the image.
	size = get_file_size();
	file_system = map2memory(size);

	printf("file size is %ld\n", size);

	// Read super block which in block group zero.
	read_super_block(&sb);

	// print some information.
	printf("-----------------------------------------------------\n");
	printf("The file system was created by %s\n", get_os_name(&sb));
	printf("Inodes count %u : Block count %u\n", sb.s_inodes_count, sb.s_blocks_count);
	printf("Reserved block count %u\n", sb.s_r_blocks_count);
	printf("Free block count %u\n", sb.s_free_blocks_count);
	printf("Free inode count %u\n", sb.s_free_inodes_count);
	printf("First block %u\n", sb.s_first_data_block);
	printf("Block size %u\n", get_block_size(sb));
	printf("Block group nr is %d\n", sb.s_block_group_nr);
	printf("Fragment size %u\n", get_fragment_size(sb));
	printf("First data block is 0x%x\n", sb.s_first_data_block);
	printf("First inode %u\n", sb.s_first_ino);
	printf("Inode size %u\n", sb.s_inode_size);
	printf("Block per group 0x%x\n", sb.s_blocks_per_group);
	printf("Inode per group 0x%x\n", sb.s_inodes_per_group);
	printf("-----------------------------------------------------\n");

	// how many block groups do I have?
	block_cnt = sb.s_blocks_count / sb.s_blocks_per_group;
	if (sb.s_blocks_count > 8192)
		block_cnt++;

	// Allocate memory to store block group data.
	block_group = malloc(sizeof(*block_group) * (block_cnt + 1));
	assert(block_group != NULL);
	memset(block_group, 0x0, sizeof(*block_group));

	// Setup dentry_list.
	head.next = NULL;
 
	// Read first block group descriptor.
	read_block_group(&sb, block_group, SUPER_BLOCK_SIZE * 2);
	printf("There is %d Block group\n", block_cnt);

	for (i = 0; i < block_cnt ; i++) {
		if (block_group[i].bg_block_bitmap != 0 &&
			block_group[i].bg_inode_bitmap != 0 &&
			block_group[i].bg_inode_table != 0) {
			printf("block group[%d]: bg_block_bitmap is 0x%x(0x%x) : bg_inode_bitmap is 0x%x(0x%x) : bg_inode_table is0x%x(0x%x)\n",
				i, 
				block_group[i].bg_block_bitmap, blockid2address(&sb, block_group[i].bg_block_bitmap),
				block_group[i].bg_inode_bitmap, blockid2address(&sb, block_group[i].bg_inode_bitmap),
				block_group[i].bg_inode_table, blockid2address(&sb, block_group[i].bg_inode_table));
				printf("block group[%d] has %d directories\n", i, block_group[i].bg_used_dirs_count);
				printf("Block Data starts 0x%lx\n", get_block_data_address(&sb, block_group + i));
				printf("Free blocks 0x%x\n",  block_group[i].bg_free_blocks_count);
				printf("Free inodes 0x%x\n",  block_group[i].bg_free_inodes_count);
				printf("-----------------------------------------------------\n");

				// Get directory entries.
				if (get_all_directories(&head, get_block_data_address(&sb, block_group + i), block_group + i) < 0)
					exit(-1);

				
				// Copy of super block which exists block group zero, one and so on.
				if (i == 0) {
					printf("Next is 0x%x\n", (sb.s_blocks_per_group * 0x400) + (0x400 * ((i + 1) * 2)));
					read_block_group(&sb, block_group + 1, (sb.s_blocks_per_group * 0x400) + 0x800);
				}
		}
	
	}
	printf("-----------------------------------------------------\n");

	directory_walk(&head, &sb, block_group);

	for (p = head.next; p != NULL; p = q) {
//		printf("inode[%u]= [%s]\n", p->dentry->inode, p->dentry->name);
		q = p->next;
		free(p->dentry);
		free(p);
	}

	munmap(file_system, size);
	free(block_group);

	return 0;
}
