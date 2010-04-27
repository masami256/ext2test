#include <stdio.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <assert.h>
#include "ext2fs.h"
#include "ext2_blockgroup.h"
#include "ext2_inode.h"

static const char const test_file[] = "./hda.img";
static unsigned char *file_system;

static unsigned long get_file_size(void);
static void *map2memory(unsigned long size);
static const char *get_os_name(struct ext2_superblock *sb);
static void read_block_group(struct ext2_superblock *sb,  struct ext2_blockgroup *bg, unsigned long offset);
static void read_super_block(struct ext2_superblock *sb);
static u_int32_t blockid2address(struct ext2_superblock *sb, u_int32_t id);
static unsigned long get_block_data_address(struct ext2_superblock *sb, struct ext2_blockgroup *bg, int block_nr);

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

static unsigned long get_block_data_address(struct ext2_superblock *sb, struct ext2_blockgroup *bg, int block_nr)
{
	return (unsigned long) sb->s_inodes_per_group * sizeof(struct ext2_inode) 
				+  blockid2address(sb, bg[block_nr].bg_inode_table);
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
	struct ext2_blockgroup block_group[32];
	int i, j;
	
	memset(block_group, 0x0, sizeof(block_group));
	
	size = get_file_size();

	printf("file size is %ld\n", size);

	file_system = map2memory(size);

	read_super_block(&sb);

	printf("The file system was created by %s\n", get_os_name(&sb));
	printf("inodes count %u : block count %u\n", sb.s_inodes_count, sb.s_blocks_count);
	printf("reserved block count %u\n", sb.s_r_blocks_count);
	printf("free block count %u\n", sb.s_free_blocks_count);
	printf("free inode count %u\n", sb.s_free_inodes_count);
	printf("first block %u\n", sb.s_first_data_block);
	printf("Block size %u\n", get_block_size(sb));
	printf("Block group nr is %d\n", sb.s_block_group_nr);
	printf("Fragment size %u\n", get_fragment_size(sb));
	printf("first data block is 0x%x\n", sb.s_first_data_block);
	printf("first inode %u\n", sb.s_first_ino);
	printf("inode size %u\n", sb.s_inode_size);
	printf("Block per group 0x%x\n", sb.s_blocks_per_group);
	printf("Inode per group 0x%x\n", sb.s_inodes_per_group);
	
	block_cnt = sb.s_blocks_count / 8192;

	read_block_group(&sb, block_group, SUPER_BLOCK_SIZE * 2);

	for (i = 0; i < block_cnt + 1; i++) {
		if (block_group[i].bg_block_bitmap != 0 &&
			block_group[i].bg_inode_bitmap != 0 &&
			block_group[i].bg_inode_table != 0) {
			printf("block group[%d]: bg_block_bitmap is 0x%x(0x%x) : bg_inode_bitmap is 0x%x(0x%x) : bg_inode_table is0x%x(0x%x)\n",
				i, 
				block_group[i].bg_block_bitmap, blockid2address(&sb, block_group[i].bg_block_bitmap),
				block_group[i].bg_inode_bitmap, blockid2address(&sb, block_group[i].bg_inode_bitmap),
				block_group[i].bg_inode_table, blockid2address(&sb, block_group[i].bg_inode_table));
				printf("block group[%d] has %d directories\n", i, block_group[i].bg_used_dirs_count);
				printf("Block Data starts 0x%lx\n", get_block_data_address(&sb, block_group, i));
				printf("Free blocks 0x%x\n",  block_group[i].bg_free_blocks_count);
				printf("Free inodes 0x%x\n",  block_group[i].bg_free_inodes_count);
				printf("Next is 0x%lx\n", (sb.s_blocks_per_group * 0x400) + (0x400 * ((i + 1) * 2)));
		}
	
	}

		
	munmap(file_system, size);

	return 0;
}
