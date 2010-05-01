#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include "minixfs.h"
#include "minix_superblock.h"
#include "minix_dentry.h"
#include "minix_inode.h"

static const char * const test_file = "./minix.img";
static unsigned char *file_system;

static void *map2memory(unsigned long size);
static unsigned long get_file_size(void);
static void read_superblock(struct minix_superblock *sb);
static void print_superblock(const struct minix_superblock *sb);
static void read_dentry(struct minix_dentry *dentry, unsigned long address, unsigned long offset);
static void read_inode(u_int16_t inode_num, struct minix_inode *inode, unsigned long addr);
static void directory_walk(struct minix_superblock *sb, unsigned long address);
static void find_file_test(struct minix_superblock *sb);
static u_int16_t find_file(struct minix_superblock *sb, unsigned long address, const char *fname);
static void read_file(struct minix_superblock *sb, const char *fname);
static void read_file_test(struct minix_superblock *sb);

#define get_first_data_zone(sb) (sb).s_firstdatazone * 0x400
#define get_inode_table_address(sb) 0x800 + ((sb).s_imap_blocks * 0x400) + ((sb).s_zmap_blocks * 0x400)
#define get_data_zone(zone) (zone) * 0x400

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

static int get_file_type(struct minix_inode *inode)
{
	u_int16_t mode = inode->i_mode;

	if ((mode & I_TYPE) == I_REGULAR)
		return I_FT_REGULAR;
	else if ((mode & I_TYPE) == I_BLOCK_SPECIAL)
		return I_FT_BLOCK;
	else if ((mode & I_TYPE) == I_DIRECTORY)
		return I_FT_DIR;
	else if ((mode & I_TYPE) == I_CHAR_SPECIAL)
		return I_FT_CHAR;
	else if ((mode & I_TYPE) == I_NAMED_PIPE)
		return I_FT_NAMED_PIPE;

	return I_FT_UNKNOWN;
}

static void read_superblock(struct minix_superblock *sb)
{
	// ignore boot block.
	memcpy(sb, file_system + 0x400, sizeof(*sb));
}

static void read_dentry(struct minix_dentry *dentry, unsigned long address, unsigned long offset)
{
	// dentry->name is 15 bytes which reserved for '\0'.
	memcpy(dentry, file_system + address + offset, sizeof(*dentry) - 1);
}

static void read_inode(u_int16_t inode_num, struct minix_inode *inode, unsigned long addr)
{
	memcpy(inode, file_system + addr + ((inode_num - 1) * sizeof(*inode)), sizeof(*inode));
}

static void directory_walk(struct minix_superblock *sb, unsigned long address)
{
	unsigned long offset = 0;
	struct minix_dentry dentry;
	struct minix_inode inode;
	unsigned long inode_tbl_bass = get_inode_table_address(*sb);
	int i;

	while (1) {
		// read first entry.
		read_dentry(&dentry, address, offset);

		if (dentry.inode == 0)
			break;

		read_inode(dentry.inode, &inode, inode_tbl_bass);

		printf("inode:0x%x name %s\n", dentry.inode, dentry.name);
		printf("i_mode: 0x%x(0x%x)\n", inode.i_mode, get_file_type(&inode));
		printf("i_nlinks: 0x%x\n", inode.i_nlinks);
		printf("uid: 0x%x\n", inode.i_uid);
		printf("gid: 0x%x\n", inode.i_gid);
		printf("i_size: 0x%x\n", inode.i_size);
		printf("i_atime: 0x%x\n", inode.i_atime);
		printf("i_mtime: 0x%x\n", inode.i_mtime);
		printf("i_ctime: 0x%x\n", inode.i_ctime);
		for (i = 0; i < NR_I_ZONE; i++) {
			if (inode.i_zone[i])
				printf("zone[%d]: 0x%x(0x%x)\n", i, inode.i_zone[i], get_data_zone(inode.i_zone[i]));
		}

		if ((get_file_type(&inode) == I_FT_DIR) &&
		    (strcmp(dentry.name, ".")) &&
		    (strcmp(dentry.name, "..")))
			directory_walk(sb, get_data_zone(inode.i_zone[0]));

		offset += sizeof(dentry) - 1;
	}
}

static void read_file(struct minix_superblock *sb, const char *fname)
{
	u_int16_t ino;
	struct minix_inode inode;
	unsigned long inode_tbl_bass = get_inode_table_address(*sb);
	char *data;
	int i;

	ino = find_file(sb, get_first_data_zone(*sb), fname);
	if (!ino) {
		printf("file %s not found\n", fname);
		return ;
	}

	read_inode(ino, &inode, inode_tbl_bass);

	printf("file %s: size is 0x%x\n", fname, inode.i_size);
	data = malloc(inode.i_size);
	assert(data != NULL);
	memcpy(data, file_system + get_data_zone(inode.i_zone[0]),inode.i_size);

	for (i = 0; i < inode.i_size; i++)
		printf("0x%02x ", data[i]);

	printf("\n");

	
}

static int count_delimita_length(const char *str, char c)
{
	int i = 0;
	int len = strlen(str);

	for (i = 0; i < len; i++) {
		if (str[i] == c)
			return i;
	}

	return -1;
	
}

static u_int16_t find_file(struct minix_superblock *sb, unsigned long address, const char *fname)
{
	unsigned long offset = 0;
	struct minix_dentry dentry;
	struct minix_inode inode;
	unsigned long inode_tbl_bass = get_inode_table_address(*sb);
	const char *tmp;
	u_int16_t ret = 0;

	int len = 0;
	int ftype;
	while (1) {
		// read first entry.
		read_dentry(&dentry, address, offset);

		if (dentry.inode == 0)
			break;

		read_inode(dentry.inode, &inode, inode_tbl_bass);

		tmp = fname;
		if (tmp[0] == '/') 
			tmp = tmp + 1;

		ftype = get_file_type(&inode); 
		if (ftype == I_FT_DIR) {
			len = count_delimita_length(tmp, '/');
			if (len == -1) {
				if (!strcmp(tmp, dentry.name))
					return dentry.inode;
			} else if (!strncmp(tmp, dentry.name, len)) {
				ret = find_file(sb, get_data_zone(inode.i_zone[0]), tmp + len);
			} else {
				// if final character is '/', finish searching.
				if (!strcmp(tmp + len, "/"))
					return dentry.inode;
			}
		} else if (ftype == I_FT_REGULAR) {
			if (!strcmp(dentry.name, tmp))
				return dentry.inode;
		}
		if (ret)
			return ret;

		offset += sizeof(dentry) - 1;
	}

	return 0;

}

int main(int argc, char **argv)
{
	struct minix_superblock sb;
	unsigned long size = 0;

	size = get_file_size();
	file_system = map2memory(size);
	assert(file_system != NULL);

	read_superblock(&sb);

	print_superblock(&sb);

	printf("first data zone is 0x%x\n", get_first_data_zone(sb));

	directory_walk(&sb, get_first_data_zone(sb));

	find_file_test(&sb);
	read_file_test(&sb);

	munmap(file_system, size);

	return 0;
}

static void read_file_test(struct minix_superblock *sb)
{
	printf(">>>>>>>>>>read_file() test <<<<<<<<<<<<<<<<\n");
	read_file(sb, "/dir_a/dir_b/foobar.txt");
	printf(">>>>>>>>>>read_file() test <<<<<<<<<<<<<<<<\n");
	read_file(sb, "/test.txt");
	printf(">>>>>>>>>>read_file() test <<<<<<<<<<<<<<<<\n");

}

static void find_file_test(struct minix_superblock *sb)
{
	u_int16_t ret;

	printf(">>>>>>>>>>find_file() test <<<<<<<<<<<<<<<<\n");
	ret = find_file(sb, get_first_data_zone(*sb), "/dir_a/dir_b/foobar.txt");
	printf("file %s %s\n", "/dir_a/dir_b/foobar.txt", ret != 0 ? "Found" : "Not found");

	ret = find_file(sb, get_first_data_zone(*sb), "/dir_A/dir_B");
	printf("file %s %s\n", "/dir_A/dir_B", ret != 0 ? "Found" : "Not found");

	ret = find_file(sb, get_first_data_zone(*sb), "/dir_A/dir_B/");
	printf("file %s %s\n", "/dir_A/dir_B/", ret != 0 ? "Found" : "Not found");
	
	ret = find_file(sb, get_first_data_zone(*sb), "/test.txt");
	printf("file %s %s\n", "/test.txt", ret != 0 ? "Found" : "Not found");

	ret = find_file(sb, get_first_data_zone(*sb), "/dir_A/dir_C");
	printf("file %s %s\n", "/dir_A/dir_C", ret != 0 ? "Found" : "Not found");
	printf(">>>>>>>>>>find_file() test <<<<<<<<<<<<<<<<\n");
}


static void print_superblock(const struct minix_superblock *sb)
{
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n");
	printf("Superblock info\n");
	printf("s_ninodes: 0x%x\n", sb->s_ninodes);
	printf("s_nzones:  0x%x\n", sb->s_nzones);
	printf("s_imap_blocks: 0x%x\n", sb->s_imap_blocks);
	printf("s_zmap_blocks: 0x%x\n", sb->s_zmap_blocks);
	printf("s_firstdatazone: 0x%x\n", sb->s_firstdatazone);
	printf("s_log_zone_size: 0x%x\n", sb->s_log_zone_size);
	printf("s_max_size: 0x%x\n", sb->s_max_size);
	printf("s_magic: 0x%x\n", sb->s_magic);
	printf("s_pad: 0x%x\n", sb->s_pad);
	printf("s_zones: 0x%x\n", sb->s_zones);
	printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>\n\n");
}
