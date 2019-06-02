/*
Filesystem Lab disigned and implemented by Liang Junkai,RUC
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fuse.h>
#include <errno.h>
#include<sys/types.h>
#include "disk.h"

#define DIRMODE (S_IFDIR|0755)
#define REGMODE (S_IFREG|0644)

#define DISK "vdisk/"  /* 包含文件系统信息的映射文件的路径，请使用绝对路径*/

#define MAX_FILENAME 24          /* 文件名的最大长度*/
#define MAX_EXTENSION 3         /* 文件扩展名的最大长度 */
#define MAX_DATA_IN_BLOCK 4096		/* 数据块能容纳的最大数据  4kb*/
#define BLOCK_BYTES (0x1<<12)         /* 每块的容量 4kb*/       

#define superblock_number 0
#define Ibm_block_numebr 1
#define Dbm_block_numebra 2
#define Dbm_block_numebrb 3

#define dir_type 0
#define file_type 1
#define root_inode_number 0


#define _blockid(inode_number) (inode_number/32)
#define _BID(inode_number) (inode_number/32 +4)
#define _blockoffset(inode_number) (inode_number%32)

#define _bitsize 8

#define DP_NUMBER 14
#define IP_NUMBER 2
#define FN_IN_DIR 128

#define disk_size 4096
#define parent_len 512


#define dir_file_size 32

#define F_namemax 24
#define inode_numbermax 32768


typedef long inode_t;
typedef long datablock_t;

typedef struct{
	mode_t  mode;
	nlink_t nlink;
	uid_t   uid;
	gid_t   gid;
	off_t   size;
	time_t atime;
	time_t mtime;
	time_t ctime;
	size_t inode_number;
	int direct_pointer[DP_NUMBER];
	int indirect_pointer[IP_NUMBER];
}my_stat;

typedef struct {
     long fs_size;                     //size of file system, in blocks
     long first_blk;                   //first block of root directory
}superblock;

typedef struct{
	char filename[24];
	int filetype;
	int inode_number;
}dir_files;

typedef struct{
	//int dir_count;
	//char padding[28];
	dir_files files[128];
}dir_data; 


typedef union
{
	int pointer_nextblock[MAX_DATA_IN_BLOCK/sizeof(int)];
	char file_data[MAX_DATA_IN_BLOCK];     // And all the rest of the space in the block can be used for actual data storage
	dir_data dir;
}disk_block;        


char* strrev(char* s)

{
	/* h指向s的头部 */
	char* h = s;

	char* t = s;
	char ch;
	/* t指向s的尾部 */
	while (*t++) {};

	t--;    /* 与t++抵消 */

	t--;    /* 回跳过结束符'\0' */
	/* 当h和t未重合时，交换它们所指向的字符 */
	while (h < t)
	{
		ch = *h;

		*h++ = *t;    /* h向尾部移动 */

		*t-- = ch;    /* t向头部移动 */
	}
	return(s);

}




int match_Ibm(int inode_number)
{
	char buff[disk_size];
	disk_read(Ibm_block_numebr, buff);
	
	int bit_position = inode_number % _bitsize;
	int byte_position = inode_number / _bitsize;
	

	return buff[byte_position] & (0x1 << bit_position);
}


int find_empty_Ibm()
{
	char buff[disk_size];
	disk_read(Ibm_block_numebr, buff);

	for (int byte_position = 0; byte_position < disk_size; byte_position++)
	{
		for (int bit_position = 0; bit_position < _bitsize; bit_position++)
		{
			if (!(buff[byte_position] & (0x1 << bit_position)))
			{
				return byte_position * _bitsize + bit_position;
			}
		}
	}

	return -1;
}

void set_Ibm(int inode_number)
{
	char buff[disk_size];
	disk_read(Ibm_block_numebr, buff);

	int bit_position = inode_number % _bitsize;
	int byte_position = inode_number / _bitsize;


	buff[byte_position] |= (0x1 << bit_position);

	disk_write(Ibm_block_numebr, buff);
	return;
}

void empty_Ibm(int inode_number)
{
	char buff[disk_size];
	disk_read(Ibm_block_numebr, buff);

	int bit_position = inode_number % _bitsize;
	int byte_position = inode_number / _bitsize;


	buff[byte_position] &= (~(0x1 << bit_position));
	disk_write(Ibm_block_numebr, buff);
	return;
}

int match_Dbm(int data_number) 
{
	if (data_number < 32768)
	{
		char buff[disk_size];
		disk_read(Dbm_block_numebra, buff);

		int bit_position = data_number % _bitsize;
		int byte_position = data_number / _bitsize;


		return buff[byte_position] & (0x1 << bit_position);
	}
	else if (data_number < 65536)
	{
		int new_data_numer = data_number - 32768;
		char buff[disk_size];
		disk_read(Dbm_block_numebrb, buff);

		int bit_position = new_data_numer % _bitsize;
		int byte_position = new_data_numer / _bitsize;
		return buff[byte_position] & (0x1 << bit_position);
	}
	else return -1;
}

int find_empty_Dbm() 
{
	char buff[disk_size];
	disk_read(Dbm_block_numebra, buff);

	for (int byte_position = 0; byte_position < disk_size; byte_position++)
	{
		for (int bit_position = 0; bit_position < _bitsize; bit_position++)
		{
			if (!(buff[byte_position] & (0x1 << bit_position)))
			{
				return byte_position * _bitsize + bit_position;
			}
		}
	}

	char buffb[disk_size];
	disk_read(Dbm_block_numebrb, buffb);

	for (int byte_position = 0; byte_position < disk_size; byte_position++)
	{
		for (int bit_position = 0; bit_position < _bitsize; bit_position++)
		{
			if (buff[byte_position] & (0x1 << bit_position))
			{
				return byte_position * _bitsize + bit_position+32768;
			}
		}
	}

	return -1;
}

void empty_Dbm(int data_number)
{
	if (data_number < 32768)
	{
		char buff[disk_size];
		disk_read(Dbm_block_numebra, buff);

		int bit_position = data_number % _bitsize;
		int byte_position = data_number / _bitsize;


		buff[byte_position] &= (~(0x1 << bit_position));
		disk_write(Dbm_block_numebra, buff);
	}
	else if (data_number < 65536)
	{
		int new_data_numer = data_number - 32768;
		char buff[disk_size];
		disk_read(Dbm_block_numebrb, buff);

		int bit_position = new_data_numer % _bitsize;
		int byte_position = new_data_numer / _bitsize;
		buff[byte_position] &= (~(0x1 << bit_position));
		disk_write(Dbm_block_numebrb, buff);
	}

	return;
}

void set_Dbm(int data_number)
{
	if (data_number < 32768)
	{
		char buff[disk_size];
		disk_read(Dbm_block_numebra, buff);

		int bit_position = data_number % _bitsize;
		int byte_position = data_number / _bitsize;


		buff[byte_position] |= (0x1 << bit_position);
		disk_write(Dbm_block_numebra, buff);
	}
	else if (data_number < 65536)
	{
		int new_data_numer = data_number - 32768;
		char buff[disk_size];
		disk_read(Dbm_block_numebrb, buff);

		int bit_position = new_data_numer % _bitsize;
		int byte_position = new_data_numer / _bitsize;
		buff[byte_position] |= (0x1 << bit_position);
		disk_write(Dbm_block_numebrb, buff);
	}
	return;
}

inode_t match_path(const char*path)
{

	if (strcmp(path, "/") == 0)
	{
		//printf("enter\n");
		return root_inode_number;
	}
	else
	{
		//char buff_inode[4048];
		//char buff_datablock[4048];
		printf("called match_path:%s\n", path);
		char *p = path;
		//p = +1;//jump to the /
		inode_t dir_inode = root_inode_number;


		//int inode_blockid = dir_inode / 32;
		//int inode_blockoffset = dir_inode % 32;

		int pause_time = 0;
		size_t file_number = 0;
		datablock_t datablock_id = 1028;

		while (*p != '\0')
		{
			pause_time++;
			if (pause_time > 10)
			{
				return 0;
			}

			printf("inode_number is:%d\n", dir_inode);
			long inode_blockid = dir_inode / 32;
			long inode_blockoffset = dir_inode % 32;

			my_stat inode_data[32];

			p++;
			//memset(buff_inode, 0, sizeof(buff_inode));
			disk_read(inode_blockid + 4, inode_data);
			//printf("iiiiiiiiiiiii:%d %d\n", inode_blockid, inode_blockoffset);
			//if(inode_blockid == 0)

			if ((unsigned int)(inode_data[inode_blockoffset].mode) != (unsigned int)(DIRMODE) )
			{
				printf("wrong in match path! not the dir 00000000000000 mode:%d right is:%d!\n", inode_data[inode_blockoffset].mode, DIRMODE);
				//printf("%d:--------------\n", (unsigned int)(inode_data[inode_blockoffset].mode) ^ (DIRMODE));
				return -1;
				//exit(0);
				//return 0;
			}

			size_t count_file = inode_data[inode_blockoffset].size /32 ;
			//printf("get 352!\n");
			printf("pppppppppppppppppppppppp:size:%d\n\n\n", inode_data[inode_blockoffset].size);

			if (inode_data[inode_blockoffset].size == 0)
			{
				return -1;
			}

			if (count_file > DP_NUMBER*FN_IN_DIR)
			{
				printf("can't support such large dir or file !\n");
				//exit(0);
				//return 0;
			}

			size_t block_numer = count_file / FN_IN_DIR;
			size_t block_left = count_file % FN_IN_DIR;



			printf("block_left:%d!\n\n\n\n", block_left);


			char dir_name[24];
			char *index = dir_name;
			while (*p != '\0' && *p != '/')
			{
				*index = *p;
				index++;
				p++;
			}
			*index = '\0';

			//p--;
			printf("index:%s\\n\n\n", dir_name);
			//exit(0);

			if (*p == '\0')
			{//找到了根的底部

				//printf("arrive end!!!\n");
				for (int i = 0; i < block_numer; i++)
				{
					disk_block dir_search;
					disk_read(inode_data[inode_blockoffset].direct_pointer[i], &dir_search);
					for (int j = 0; j < 128; j++)
					{
						if (strcmp(dir_search.dir.files[j].filename, dir_name) == 0)
						{
							return 0;
						}
					}

				}
				if (block_left != 0)
				{
					printf("get left!!!\n");
					fflush(stdout);
					disk_block dir_search;
					disk_read(inode_data[inode_blockoffset].direct_pointer[block_numer], &dir_search);
					for (int j = 0; j < block_left; j++)
					{
						printf("dir_file:name:%s\n", dir_search.dir.files[j].filename);
						if (strcmp(dir_search.dir.files[j].filename, dir_name) == 0)
						{
							printf("match inode:%d name:%s!!\n", dir_search.dir.files[j].inode_number, dir_search.dir.files[j].filename);
							


							return dir_search.dir.files[j].inode_number;
						}
					}
				}

				return -1;
			}
			else
			{
				printf("get right!!!\n");
				int flag = 0;
				
				for (int i = 0; i < block_numer; i++)
				{
					disk_block dir_search[1];
					disk_read(inode_data[inode_blockoffset].direct_pointer[i], dir_search);
					printf("get 427!\n");
					for (int j = 0; j < 128; j++)
					{
						if (strcmp(dir_search[0].dir.files[j].filename, dir_name) == 0)
						{
							if ((unsigned int)(dir_search[0].dir.files[j].filetype) == (unsigned int)(DIRMODE))
							{
								printf("match_dir:%s\n", dir_search[0].dir.files[j]);
								dir_inode = dir_search[0].dir.files[j].inode_number;
								flag = 1;
								break;
							}
							else
							{
								printf("match_dir:%s \n", dir_search[0].dir.files[j]);
								printf("mode:%ld right is :%ld", dir_search[0].dir.files[j].filetype, DIRMODE);
								printf("------------------------wrong in match path! not the dir!\n");
								//exit(0);
							}
						}

					}
				}

					if (!flag && block_left != 0)
					{
						printf("get right222222!!!\n");
						disk_block dir_search[1];
						disk_read(inode_data[inode_blockoffset].direct_pointer[block_numer], dir_search);
						for (int j = 0; j < block_left; j++)
						{
							printf("filename:%s dir_name:%s\n", dir_search[0].dir.files[j].filename, dir_name);
							if (strcmp(dir_search[0].dir.files[j].filename, dir_name) == 0)
							{


								if ((unsigned int)(dir_search[0].dir.files[j].filetype) == (unsigned int)(DIRMODE))
								{
									dir_inode = dir_search[0].dir.files[j].inode_number;
									flag = 1;
									break;
								}
								else
								{
									printf("match_dir:%s \n", dir_search[0].dir.files[j]);
									printf("mode:%ld right is :%ld", dir_search[0].dir.files[j].filetype, DIRMODE);
									printf("asdfasdfasdf wrong in match path! not the dir!\n");
									//exit(0);
								}
							}
						}
					}
					if (flag == 0)
					{
						printf("the file or dir is not exits!\n");
						return -1;
					}




				
			}
		}
	}
}

       
int collect_parent_now_path(const char *path, char *parent_path, char *file_name)
{
	printf("enter_collect_parent_now_path!:%s\n", path);
	char *path_pointer = path;
	int pathlen = strlen(path);
	path_pointer += pathlen-1;

	char temp_filename[24] = { 0 };
	int index = 0;

	if (*path_pointer == '/')
	{
		return -1;
	}
	while (*path_pointer != '/')
	{
		temp_filename[index++] = *path_pointer;
		path_pointer--;
	}
	//temp_filename[index] = '\0';
	strcpy(file_name, strrev(temp_filename));
	strncpy(parent_path, path, pathlen - index);
	
	parent_path[pathlen - index] = '\0';
	if (strcmp(parent_path, "/") == 0) return 0;
	else
	{
		parent_path[pathlen - index-1] = '\0';
	}
	printf("parent_path file_name:%s %s\n", parent_path, file_name);
	return 0;
}
      
//Format the virtual block device in the following function
int mkfs() 
{
	printf("calling mkfs!\n");
	printf("mode_t%d\n\n",sizeof(mode_t));
	printf("dir_data%d\n\n", sizeof(dir_data));
	printf("dir_data%d\n\n", sizeof(dir_files));
	//dir_files
	//my_stat *root_stat = (my_stat *)malloc(sizeof(my_stat));
	my_stat root_stat[32];
	char buff[disk_size];
	memset(buff, 0, sizeof(buff));
	buff[0] |= 0x1;
	disk_write(Ibm_block_numebr, buff);

	//char buff_root[4048];

	//char buff_data_bitmapa[4048];
	memset(buff, 0, sizeof(buff));
	for (int i = 0; i < 128; i++)
	{
		buff[i] = 0xFF;
	}
	buff[128] = 0xF;
	disk_write(Dbm_block_numebra, buff);

	//char buff_data_bitmapb[4048];
	memset(buff, 0, sizeof(buff));
	disk_write(Dbm_block_numebrb, buff);

	

	root_stat[0].mode = DIRMODE;
	root_stat[0].nlink = 1;
	root_stat[0].uid = getuid();
	root_stat[0].gid = getgid();
	root_stat[0].size = 0;
	root_stat[0].atime = time(NULL);
	root_stat[0].mtime = time(NULL);
	root_stat[0].ctime = time(NULL);
	root_stat[0].inode_number = root_inode_number;
	memset(root_stat->direct_pointer, 0, sizeof(root_stat->direct_pointer));
	memset(root_stat->indirect_pointer, 0, sizeof(root_stat->indirect_pointer));

	//root_stat[0].direct_pointer[0] = 1028;

	disk_write((root_inode_number + 4), root_stat);

	//free(root_stat);

	//disk_block* root_dir = (disk_block*)malloc(sizeof(disk_block));
	//printf("block size:%d\n", sizeof(disk_block));
	//printf("blockdata size:%d\n", sizeof(dir_data));
	//printf("blockdata size:%d\n", sizeof(dir_files));
	//dir_files
	//root_dir->dir.dir_count = 0;
	//memset(root_dir->dir.padding, 0, 28);
	//memset(root_dir->dir.files, 0, sizeof(root_dir->dir.files));
	//disk_write(1028, (void*)root_dir);
	//free(root_dir);


	//superblock
	struct statvfs s1;

	s1.f_bsize = disk_size;
	s1.f_blocks = BLOCK_NUM;
	
	s1.f_bfree = BLOCK_NUM - 4 - 1024;
	s1.f_bavail = BLOCK_NUM - 4 - 1024;
	s1.f_files = 1;
	//inode_numbermax
	s1.f_ffree = inode_numbermax - 1;
	s1.f_favail = inode_numbermax - 1;
	s1.f_namemax = F_namemax;

	disk_write(superblock_number, &s1);

	return 0;
}


//Filesystem operations that you need to implement
int fs_getattr (const char *path, struct stat *attr)
{
	
	//printf("Getattr is called:%s\n",path);
	//printf("sizeof_inode:--------%d\n",sizeof(my_stat));
	inode_t inode_number = match_path(path);
	//printf("inode number is:%d\n", inode_number);

	
	
	if (inode_number == -1) return -ENOENT;
	//printf("inode number is:%d\n", inode_number);

	my_stat buff[32];
	memset(buff, 0, sizeof(buff));
	disk_read(_BID(inode_number), buff);
	//printf("attr_info:%d %d\n", buff[0].mode, buff[0].mode);
	//fflush(stdout);
	//my_stat* buff = (my_stat*)buff_data;
	int offset = _blockoffset(inode_number);

	attr->st_mode = buff[offset].mode;
	attr->st_nlink = buff[offset].nlink;
	
	attr->st_uid = buff[offset].uid;
	attr->st_gid = buff[offset].gid;
	attr->st_size = buff[offset].size;
	attr->st_atime = buff[offset].atime;
	attr->st_mtime = buff[offset].mtime;
	//printf("attr_info:%d %d\n", attr->st_mode, attr->st_nlink);
	//fflush(stdout);
	return 0;
}

int fs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{

	//printf("Readdir is called:%s\n", path);

	size_t dir_inode_numer = match_path(path);
	//char dir_data[disk_size];
	my_stat dir_data[32];
	disk_read(_blockid(dir_inode_numer) + 4, dir_data);
	

	size_t count = dir_data[_blockoffset(dir_inode_numer)].size / 32 ;
	//printf("readdir_size:%d\n", count);
	//这里做了一点简化处理，只能支持128*16 = 2048个文件及目录


	size_t pointer_count = count / 128;
	size_t pointer_left = count % 128;
	//printf("readdir_size:%d\n", pointer_left);
	if (dir_data[_blockoffset(dir_inode_numer)].size > 2048)
	{
		printf("this dir can't support such large file or dir!\n");
		return 0;
	}
	if (dir_data[_blockoffset(dir_inode_numer)].size <= 0)
	{
		return 0;
	}
	else
	{
		for (int i = 0; i < pointer_count; i++)
		{
			//char blockdata[4048];
			disk_block block_dir_data;
			disk_read(dir_data[_blockoffset(dir_inode_numer)].direct_pointer[i], &block_dir_data);
			for (int j = 0; j < 128; j++)
			{
				
				filler(buffer, block_dir_data.dir.files[j].filename, NULL, 0);
			}

		}
		if (pointer_left != 0)
		{
			disk_block block_dir_data;
			disk_read(dir_data[_blockoffset(dir_inode_numer)].direct_pointer[pointer_count], &block_dir_data);
			for (int j = 0; j < pointer_left; j++)
			{
				//disk_read(dir_data[_blockoffset(dir_inode_numer)].direct_pointer[j], &dir_data);
				filler(buffer, block_dir_data.dir.files[j].filename, NULL, 0);
			}
		}
	}

	return 0;
}

int fs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("Read is called:%s\n\n\n",path);
	printf("size:%d\n", size);
	printf("offset:%d\n",offset);

	

	//memset(buffer, 0, sizeof(buffer));

	inode_t file_inode;
	file_inode = match_path(path);

	printf("%d[[[[\n\n", file_inode);

	my_stat inode_info[32];

	disk_read(_BID(file_inode), inode_info);
	long blockoffset = _blockoffset(file_inode);
	inode_info[blockoffset].atime = time(NULL);
	




	printf("size:%ld!\n", inode_info[blockoffset].size);

	printf("%d[[[[\n\n", file_inode);

	char filename[24] = { 0 };
	char parent_path[256] = { 0 };

	collect_parent_now_path(path, parent_path, filename);
	printf("read::parent_path:%s filename:%s\n", parent_path, filename);

	printf("inode:%d!\n", file_inode);
	//int real_size = size;
	datablock_t begin_block_number = offset / disk_size;
	printf("size:%ld!\n", inode_info[blockoffset].size);
	fflush(stdout);
	off_t read_size = size;

	if (( inode_info[blockoffset].size ) <= (offset + size))
	{
		printf("---------------------------------\n");
		read_size = (inode_info[blockoffset].size) - offset;
		//char read_time[1] = 0x1;
		//fi->fh = 0x1;
		disk_write(_BID(file_inode), inode_info);
		if ((int)inode_info[blockoffset].size <= 0)
		{
			printf("error!!\n\n\n\n\n");

			disk_read(_BID(file_inode), inode_info);
			printf("size:%ld\n", inode_info[blockoffset].size);

			fflush(stdout);
		}

		printf("real_size:%d size:%ld\n\n", read_size,size);
	}
		datablock_t end_block_number = (offset + read_size) / disk_size;
		char *Buff = buffer;
		off_t block_number_left = (offset + read_size) % disk_size;
		if (block_number_left == 0)
		{
			end_block_number--;
		}

		printf("---------------------------------\n");
		if (begin_block_number == end_block_number)
		{//这里是说所读的数据在一个块里
			if (begin_block_number < 14) {
				char data[disk_size];
				disk_read(inode_info[blockoffset].direct_pointer[begin_block_number], data);
				memcpy(buffer, data + (offset % disk_size), read_size);
				return read_size;
			}
			else if ((begin_block_number - 14) < 1024)
			{
				char data[disk_size];
				disk_block pointer_data[0];
				disk_read(inode_info[blockoffset].indirect_pointer[0], pointer_data);
				disk_read(pointer_data[0].pointer_nextblock[(begin_block_number - 14)], data);
				memcpy(buffer, data + (offset % disk_size), read_size);
				return read_size;

			}
			else if ((begin_block_number - 14) < 2 * 1024)
			{
				char data[disk_size];
				disk_block pointer_data[1];
				disk_read(inode_info[blockoffset].indirect_pointer[1], pointer_data);
				disk_read(pointer_data[0].pointer_nextblock[(begin_block_number - 14-1024)], data);
				memcpy(buffer, data + (offset % disk_size), read_size);
				return read_size;
			}
			
			else
			{
				return 0;
			}



			
		}

		//这里是处理一下开始时的情况
		if (begin_block_number < 14) {
			char data[disk_size] = { 0 };

			disk_read(inode_info[blockoffset].direct_pointer[begin_block_number], data);
			memcpy(Buff, data + (offset % disk_size), disk_size-((offset % disk_size)));
			//return size;
		}
		else if ((begin_block_number - 14) < 1024)
		{
			char data[disk_size];
			disk_block pointer_data[0];
			disk_read(inode_info[blockoffset].indirect_pointer[0], pointer_data);
			disk_read(pointer_data[0].pointer_nextblock[(begin_block_number - 14)], data);
			memcpy(Buff, data + (offset % disk_size), disk_size - ((offset % disk_size)));
			//return size;

		}
		else if ((begin_block_number - 14) < 2 * 1024)
		{

			char data[disk_size];
			disk_block pointer_data[1];
			disk_read(inode_info[blockoffset].indirect_pointer[1], pointer_data);
			disk_read(pointer_data[0].pointer_nextblock[(begin_block_number - 14 - 1024)], data);
			memcpy(Buff, data + (offset % disk_size), disk_size - ((offset % disk_size)));

			//return size;
		}

		else
		{
			//这里不能处理了
			return 0;
		}

		Buff += (disk_size - ((offset % disk_size)));
		//int offset_i = 0;
		//这里的所有情况都是
		for (int i = begin_block_number+1; i <= end_block_number-1; i++)
		{

			if (i < 14) {
				char data[disk_size];

				disk_read(inode_info[blockoffset].direct_pointer[i], data);
				memcpy(Buff, data , disk_size );
				//return size;
			}
			else if ((i - 14) < 1024)
			{
				char data[disk_size];
				disk_block pointer_data[0];
				disk_read(inode_info[blockoffset].indirect_pointer[0], pointer_data);
				disk_read(pointer_data[0].pointer_nextblock[(i - 14)], data);
				memcpy(Buff, data, disk_size);
				//return size;

			}
			else if ((i - 14) < 2 * 1024)
			{

				char data[disk_size];
				disk_block pointer_data[1];
				disk_read(inode_info[blockoffset].indirect_pointer[1], pointer_data);
				disk_read(pointer_data[0].pointer_nextblock[(i - 14 -1024)], data);
				memcpy(Buff, data, disk_size);
				//return size;
			}
			Buff += disk_size;
			//offset_i += 1;
		}

		if (end_block_number < 14) {
			char data[disk_size];

			disk_read(inode_info[blockoffset].direct_pointer[end_block_number], data);
			memcpy(Buff, data, ((offset+ read_size) % disk_size));


			//disk_write(_BID(file_inode), inode_info);
			return read_size;
		}
		else if ((end_block_number - 14) < 1024)
		{
			char data[disk_size];
			disk_block pointer_data[0];
			disk_read(inode_info[blockoffset].indirect_pointer[0], pointer_data);
			disk_read(pointer_data[0].pointer_nextblock[(end_block_number - 14)], data);
			memcpy(Buff, data, ((offset + read_size) % disk_size));


			//disk_write(_BID(file_inode), inode_info);
			return read_size;

		}
		else if ((end_block_number - 14) < (2 * 1024))
		{

			char data[disk_size] = { 0 };
			disk_block pointer_data[1];
			disk_read(inode_info[blockoffset].indirect_pointer[1], pointer_data);
			disk_read(pointer_data[0].pointer_nextblock[(end_block_number - 14 - 1024)], data);
			memcpy(Buff, data, ((offset + read_size) % disk_size));


			//disk_write(_BID(file_inode), inode_info);
			return read_size;
		}

		else
		{
			//这里不能处理了
			return 0;
		}

	return 0;
}

int fs_mknod (const char *path, mode_t mode, dev_t dev)
{
	printf("Mknod is called:%s\n",path);

	inode_t inode_numer = find_empty_Ibm();

	printf("inode_numer is:%d\n", inode_numer);
	if (inode_numer == -1)
	{
		printf("can't create new file!\n");
		fflush(stdout);
		return -ENOSPC;

	}

	set_Ibm(inode_numer);

	my_stat inode_info[32];
	disk_read(_blockid(inode_numer)+4, inode_info);
	//printf("inode_numer_offset is:%d\n", _blockoffset(inode_numer));
	inode_info[_blockoffset(inode_numer)].mode = REGMODE;
	inode_info[_blockoffset(inode_numer)].nlink = 1;
	inode_info[_blockoffset(inode_numer)].uid = getuid();
	inode_info[_blockoffset(inode_numer)].gid = getgid();
	inode_info[_blockoffset(inode_numer)].size = 0;
	inode_info[_blockoffset(inode_numer)].atime = time(NULL);
	inode_info[_blockoffset(inode_numer)].mtime = time(NULL);
	inode_info[_blockoffset(inode_numer)].ctime = time(NULL);
	inode_info[_blockoffset(inode_numer)].inode_number = inode_numer;
	//memset(inode_info[_blockoffset(inode_numer)].direct_pointer, 0, sizeof(inode_info[_blockoffset(inode_numer)].direct_pointer));
	//memset(inode_info[_blockoffset(inode_numer)].indirect_pointer, 0, sizeof(inode_info[_blockoffset(inode_numer)].indirect_pointer));

	inode_info[_blockoffset(inode_numer)].inode_number = inode_numer;




	disk_write(_blockid(inode_numer)+4, inode_info);



	char filename[24] = { 0 };
	char parent_path[parent_len] = { 0 };
	if (collect_parent_now_path(path, parent_path, filename) == -1)
	{
		printf("there is no file!\n");
		return 0;
	}
	else
	{
		printf("now:parent_path filename%s %s\n", parent_path, filename);
		inode_t parent_inode_number = match_path(parent_path);
		printf("now:parent_inode_number %d\n", parent_inode_number);
		my_stat parent_inode_info[32];
		disk_read(_blockid(parent_inode_number)+4, parent_inode_info);
		parent_inode_info[_blockoffset(parent_inode_number)].mtime = time(NULL);
		parent_inode_info[_blockoffset(parent_inode_number)].ctime = time(NULL);

		int file_number = parent_inode_info[_blockoffset(parent_inode_number)].size / dir_file_size;
		printf("file_number:%d\n", file_number);

		if (file_number >= DP_NUMBER*FN_IN_DIR)
		{
			printf("can't create new file!\n");
			return -ENOSPC;
		}
		
		int dp_number = file_number / FN_IN_DIR;
		int dp_left = file_number % FN_IN_DIR;

		if (dp_left != 0)
		{
			disk_block dirfile[1];
			disk_read(parent_inode_info[_blockoffset(parent_inode_number)].direct_pointer[dp_number], dirfile);
			strcpy(dirfile[0].dir.files[dp_left].filename, filename);
			dirfile[0].dir.files[dp_left].filetype = REGMODE;
			dirfile[0].dir.files[dp_left].inode_number = inode_numer;
			parent_inode_info[_blockoffset(parent_inode_number)].size += 32;
			disk_write(parent_inode_info[_blockoffset(parent_inode_number)].direct_pointer[dp_number], dirfile);
			disk_write(_blockid(parent_inode_number)+4, parent_inode_info);
			return 0;
		}
		else//重新找一个datablock
		{
			printf("222\n");
			datablock_t new_block = find_empty_Dbm();
			set_Dbm(new_block);
			printf("new_block_id:%d\n", new_block);
			parent_inode_info[_blockoffset(parent_inode_number)].direct_pointer[dp_number] = new_block;
			parent_inode_info[_blockoffset(parent_inode_number)].size += 32;
			disk_block dirfile[1];
			strcpy(dirfile[0].dir.files[0].filename, filename);
			dirfile[0].dir.files[0].filetype = REGMODE;
			dirfile[0].dir.files[0].inode_number = inode_numer;
			disk_write(new_block, dirfile);
			disk_write(_blockid(parent_inode_number)+4, parent_inode_info);
			return 0;
		}

	}


	return 0;
}

int fs_mkdir (const char *path, mode_t mode)
{
	//printf("Mkdir is called:%s\n",path);
	inode_t inode_numer = find_empty_Ibm();

	//printf("inode_numer is:%d\n", inode_numer);
	if (inode_numer == -1)
	{
		printf("can't create new file!\n");
		fflush(stdout);
		return -ENOSPC;

	}

	set_Ibm(inode_numer);

	my_stat inode_info[32];


	disk_read(_blockid(inode_numer) + 4, inode_info);


	printf("inode_numer_offset is:%d\n", _blockoffset(inode_numer));
	inode_info[_blockoffset(inode_numer)].mode = DIRMODE;
	inode_info[_blockoffset(inode_numer)].nlink = 1;
	inode_info[_blockoffset(inode_numer)].uid = getuid();
	inode_info[_blockoffset(inode_numer)].gid = getgid();
	inode_info[_blockoffset(inode_numer)].size = 0;
	inode_info[_blockoffset(inode_numer)].atime = time(NULL);
	inode_info[_blockoffset(inode_numer)].mtime = time(NULL);
	inode_info[_blockoffset(inode_numer)].ctime = time(NULL);
	inode_info[_blockoffset(inode_numer)].inode_number = inode_numer;
	//memset(inode_info[_blockoffset(inode_numer)].direct_pointer, 0, sizeof(inode_info[_blockoffset(inode_numer)].direct_pointer));
	//memset(inode_info[_blockoffset(inode_numer)].indirect_pointer, 0, sizeof(inode_info[_blockoffset(inode_numer)].indirect_pointer));

	inode_info[_blockoffset(inode_numer)].inode_number = inode_numer;

	disk_write(_blockid(inode_numer) + 4, inode_info);


	char filename[24] = { 0 };
	char parent_path[parent_len] = { 0 };
	if (collect_parent_now_path(path, parent_path, filename) == -1)
	{
		printf("there is no file!\n");
		return 0;
	}
	else
	{
		printf("now:parent_path filename%s %s\n", parent_path, filename);
		inode_t parent_inode_number = match_path(parent_path);
		printf("now:parent_inode_number %d\n", parent_inode_number);


		my_stat parent_inode_info[32];
		disk_read(_blockid(parent_inode_number) + 4, parent_inode_info);
		parent_inode_info[_blockoffset(parent_inode_number)].mtime = time(NULL);
		parent_inode_info[_blockoffset(parent_inode_number)].ctime = time(NULL);

		off_t file_number = (parent_inode_info[_blockoffset(parent_inode_number)].size) / dir_file_size;
		printf("file_number:%d\n", file_number);

		if (file_number >= DP_NUMBER * FN_IN_DIR)
		{
			printf("can't create new file!\n");
			return -ENOSPC;
		}

		long dp_number = file_number / 128;
		long dp_left = file_number % 128;

		if (dp_left != 0)
		{
			printf("111\n");
			disk_block dirfile[1];
			disk_read(parent_inode_info[_blockoffset(parent_inode_number)].direct_pointer[dp_number], dirfile);
			memcpy(dirfile[0].dir.files[dp_left].filename, filename,24);
			dirfile[0].dir.files[dp_left].filetype = DIRMODE;
			dirfile[0].dir.files[dp_left].inode_number = inode_numer;

			parent_inode_info[_blockoffset(parent_inode_number)].size += 32;
			disk_write(parent_inode_info[_blockoffset(parent_inode_number)].direct_pointer[dp_number], dirfile);
			disk_write(_blockid(parent_inode_number) + 4, parent_inode_info);
			return 0;
		}
		else//重新找一个datablock
		{
			printf("222\n");
			datablock_t new_block = find_empty_Dbm();
			set_Dbm(new_block);
			printf("new_block_id:%d\n", new_block);
			parent_inode_info[_blockoffset(parent_inode_number)].direct_pointer[dp_number] = new_block;
			parent_inode_info[_blockoffset(parent_inode_number)].size += 32;
			disk_block dirfile[1];
			memcpy(dirfile[0].dir.files[0].filename, filename,24);
			dirfile[0].dir.files[0].filetype = DIRMODE;
			dirfile[0].dir.files[0].inode_number = inode_numer;
			disk_write(new_block, dirfile);
			disk_write(_blockid(parent_inode_number) + 4, parent_inode_info);
			return 0;
		}

	}
	return 0;
}

int fs_rmdir (const char *path)
{
	//printf("Rmdir is called:%s\n\n\n\n",path);


	inode_t inode_number = match_path(path);
	if (inode_number == -1)
	{
		printf("the dir is not exits!\n");
		return 0;
	}
	empty_Ibm(inode_number);

	char parent_path[parent_len] = { 0 };
	char filename[24] = { 0 };
	if (collect_parent_now_path(path, parent_path, filename) == -1)
	{
		printf("collect_error! the dir is not exits!\n");
		return 0;
	}

	inode_t parent_inode_number = match_path(parent_path);
	my_stat parent_stat[32];
	disk_read(_blockid(parent_inode_number) + 4, parent_stat);
	long parent_blockoffset = _blockoffset(parent_inode_number);

	parent_stat[parent_blockoffset].mtime = time(NULL);
	parent_stat[parent_blockoffset].ctime = time(NULL);
	parent_stat[parent_blockoffset].size -= 32;

	disk_write(_blockid(parent_inode_number) + 4, parent_stat);
	long file_count = parent_stat[parent_blockoffset].size/32;


	
	long datablock_number = file_count / 128;
	long datablock_left = file_count % 128;

	disk_block last_dir_data[1];
	disk_read(parent_stat[parent_blockoffset].direct_pointer[datablock_number], last_dir_data);
	

	
	dir_files last_files = last_dir_data[0].dir.files[(file_count) % 128];
	inode_t last_file_inode = last_files.inode_number;

	//printf("last_files_name:%s\n", last_files.filename);
	for (int i = 0; i < datablock_number; i++)
	{
		disk_block Dir_data[1];
		disk_read(parent_stat[parent_blockoffset].direct_pointer[i], Dir_data);
		for (int j = 0; j < 128; j++)
		{
			if (Dir_data[0].dir.files[j].inode_number == inode_number)
			{//找到对应的inode_number 需要把它删除
				if(inode_number != last_file_inode)
				{//最后一个文件
		
					memcpy(&(Dir_data[0].dir.files[j]), &last_files, 32);
					disk_write(parent_stat[parent_blockoffset].direct_pointer[i], Dir_data);
					return 0;
				}
			}
		}
			
	}

	if (datablock_left == 0)
	{
		printf("delete empty dir!\n");
		empty_Dbm(parent_stat[parent_blockoffset].direct_pointer[datablock_number]);
		return 0;
	}

	//printf("get here:%d!!\n", (datablock_left + 1));
	disk_block dir_data2[1];
	disk_read(parent_stat[parent_blockoffset].direct_pointer[datablock_number], dir_data2);
	for (int j = 0; j < (datablock_left+1); j++)
	{
		if (dir_data2[0].dir.files[j].inode_number == inode_number)
		{//找到对应的inode_number 需要把它删除
			if (inode_number != last_file_inode)
			{//最后一个文件
				//printf("delete dir!%s\n", last_files.filename);
				//dir_data[0].dir.files[j] = last_files;
				memcpy(&(dir_data2[0].dir.files[j]), &last_files, 32);
				disk_write(parent_stat[parent_blockoffset].direct_pointer[datablock_number], dir_data2);
				return 0;
			}
		}
	}




	return 0;
}

int fs_unlink (const char *path)
{
	//printf("Unlink is callded:%s\n\n\n\n",path);
	
	inode_t inode_number = match_path(path);
	if (inode_number == -1)
	{
		printf("the dir is not exits!\n");
		return 0;
	}
	empty_Ibm(inode_number);

	my_stat inode_stat[32];
	disk_read(_blockid(inode_number) + 4, inode_stat);

	long now_inode_offset = _blockoffset(inode_number);

	long inode_datablock_count = inode_stat[now_inode_offset].size / disk_size;
	long inode_datablock_left = inode_stat[now_inode_offset].size % disk_size;

	if (inode_datablock_left != 0)
	{
		//printf("rm the file:%d\n\n", inode_datablock_left);
		//empty_Dbm(inode_stat[now_inode_offset].direct_pointer[inode_datablock_count]);
		inode_datablock_count++;
	}

	//删除文件和 datablock
	if (inode_datablock_count < 14)
	{
		for (int i = 0; i < inode_datablock_count; i++)
		{
			empty_Dbm(inode_stat[now_inode_offset].direct_pointer[i]);
		}
	}
	else if (inode_datablock_count < (14 + 1024))
	{
		for (int i = 0; i < 14; i++)
		{
			empty_Dbm(inode_stat[now_inode_offset].direct_pointer[i]);
		}
		disk_block indirect_pointer_data[1];
		disk_read(inode_stat[now_inode_offset].indirect_pointer[0], indirect_pointer_data);
		for (int j = 0; j < (inode_datablock_count - 14); j++)
		{
			empty_Dbm(indirect_pointer_data[0].pointer_nextblock[j]);
			empty_Dbm(inode_stat[now_inode_offset].indirect_pointer[0]);
		}
	}
	else
	{
		for (int i = 0; i < 14; i++)
		{
			empty_Dbm(inode_stat[now_inode_offset].direct_pointer[i]);
		}
		disk_block indirect_pointer_data[1];
		disk_read(inode_stat[now_inode_offset].indirect_pointer[0], indirect_pointer_data);
		for (int j = 0; j < 1024; j++)
		{
			empty_Dbm(indirect_pointer_data[0].pointer_nextblock[j]);
			empty_Dbm(inode_stat[now_inode_offset].indirect_pointer[0]);
		}
		disk_read(inode_stat[now_inode_offset].indirect_pointer[1], indirect_pointer_data);
		for (int j = 0; j < (inode_datablock_count - 14- 1024); j++)
		{
			empty_Dbm(indirect_pointer_data[1].pointer_nextblock[j]);
			empty_Dbm(inode_stat[now_inode_offset].indirect_pointer[1]);
		}
	}




	char parent_path[parent_len] = { 0 };
	char filename[24] = { 0 };
	if (collect_parent_now_path(path, parent_path, filename) == -1)
	{
		printf("collect_error! the dir is not exits!\n");
		return 0;
	}





	inode_t parent_inode_number = match_path(parent_path);
	my_stat parent_stat[32];
	disk_read(_blockid(parent_inode_number) + 4, parent_stat);
	int parent_blockoffset = _blockoffset(parent_inode_number);

	parent_stat[parent_blockoffset].mtime = time(NULL);
	parent_stat[parent_blockoffset].ctime = time(NULL);
	parent_stat[parent_blockoffset].size -= 32;

	disk_write(_blockid(parent_inode_number) + 4, parent_stat);
	long file_count = parent_stat[parent_blockoffset].size / 32;



	long datablock_number = file_count / 128;
	long datablock_left = file_count % 128;

	disk_block last_dir_data[1];
	disk_read(parent_stat[parent_blockoffset].direct_pointer[datablock_number], last_dir_data);



	dir_files last_files = last_dir_data[0].dir.files[(file_count) % 128];
	inode_t last_file_inode = last_files.inode_number;

	//printf("last_files_name:%s\n", last_files.filename);
	for (int i = 0; i < datablock_number; i++)
	{
		disk_block Dir_data[1];
		disk_read(parent_stat[parent_blockoffset].direct_pointer[i], Dir_data);
		for (int j = 0; j < 128; j++)
		{
			if (Dir_data[0].dir.files[j].inode_number == inode_number)
			{//找到对应的inode_number 需要把它删除
				if (inode_number != last_file_inode)
				{//最后一个文件
					//printf("delete dir!%s\n", last_files.filename);
					//dir_data[0].dir.files[j] = last_files;
					memcpy(&(Dir_data[0].dir.files[j]), &last_files, 32);
					disk_write(parent_stat[parent_blockoffset].direct_pointer[i], Dir_data);
					return 0;
				}
			}
		}

	}

	if (datablock_left == 0)
	{
		printf("delete empty dir!\n");
		empty_Dbm(parent_stat[parent_blockoffset].direct_pointer[datablock_number]);
		return 0;
	}

	//printf("get here:%d!!\n", (datablock_left + 1));
	disk_block dir_data2[1];
	disk_read(parent_stat[parent_blockoffset].direct_pointer[datablock_number], dir_data2);
	for (int j = 0; j < (datablock_left + 1); j++)
	{
		if (dir_data2[0].dir.files[j].inode_number == inode_number)
		{//找到对应的inode_number 需要把它删除
			if (inode_number != last_file_inode)
			{//最后一个文件
				memcpy(&(dir_data2[0].dir.files[j]), &last_files, 32);
				disk_write(parent_stat[parent_blockoffset].direct_pointer[datablock_number], dir_data2);
				return 0;
			}
		}
	}
	return 0;
}

int fs_rename (const char *oldpath, const char *newname)
{
	printf("Rename is called:%s\n",oldpath);

	


	char old_filename[24] = { 0 };
	char new_filename[24] = { 0 };
	char parent_path[parent_len] = { 0 };

	collect_parent_now_path(oldpath, parent_path, old_filename);
	collect_parent_now_path(newname, parent_path, new_filename);

	inode_t inode_number = match_path(parent_path);
	if (inode_number == -1)
	{
		printf("error rename!\n");
		return 0;
	}
	my_stat inode_stat[32];
	disk_read(_blockid(inode_number) + 4, inode_stat);
	long offset = _blockoffset(inode_number);

	long file_count = inode_stat[offset].size / 32;

	long block_numer = file_count / 128;
	long block_left = file_count % 128;

	for (int i = 0; i < block_numer; i++)
	{
		disk_block blockdata[1];
		disk_read(inode_stat[offset].direct_pointer[i], blockdata);
		for (int j = 0; j < 128; j++)
		{
			if (strcmp(blockdata[0].dir.files[j].filename, old_filename) == 0)
			{
				//memcpy(blockdata[0].dir.files[j].filename, new_filename, 24);
				memset(blockdata[0].dir.files[j].filename, 0, 24);
				strcpy(blockdata[0].dir.files[j].filename, new_filename);
				disk_write(inode_stat[offset].direct_pointer[i], blockdata);
				return 0;
			}
		}
	}
	if (block_left != 0)
	{
		disk_block blockdata2[1];
		disk_read(inode_stat[offset].direct_pointer[block_numer], blockdata2);
		for (int j = 0; j < block_left; j++)
		{
			if (strcmp(blockdata2[0].dir.files[j].filename, old_filename) == 0)
			{
				printf("\n\n\n rename succeed:old%s new%s!\n", old_filename,new_filename);
				//memcpy(blockdata[0].dir.files[j].filename, new_filename, 24);
				memset(blockdata2[0].dir.files[j].filename, 0, 24);
				strcpy(blockdata2[0].dir.files[j].filename, new_filename);
				disk_write(inode_stat[offset].direct_pointer[block_numer], blockdata2);
				return 0;
			}
		}
	}

	return 0;
}

int fs_write (const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	//printf("Write is called:%s\n\n\n",path);

	//printf("size:%d\n", size);
	//printf("offset:%d\n", offset);
	inode_t inode_number = match_path(path);


	printf("inode_number:%d bufferis:%s\n", inode_number,buffer);

	if (inode_number == -1)
	{
		printf("file_not exits!\n");
		return 0;
	}
	my_stat inode_info[32];
	disk_read(_blockid(inode_number)+4, inode_info);
	int blockoffset = _blockoffset(inode_number);

	//printf("offset:%d\n", blockoffset);

	inode_info[blockoffset].ctime = time(NULL);
	inode_info[blockoffset].mtime = time(NULL);

	off_t nowsize = inode_info[blockoffset].size;

	//if(inode_info[blockoffset].size + size <)
	inode_info[blockoffset].size += size;


	//下面这里是得到现在块的位置
	datablock_t now_block_number = nowsize / disk_size;


	//printf("the now_block_number is:%d\n", now_block_number);


	if (nowsize % disk_size != 0)
	{//同时这里也是处理追加写的方式
		//now_block_number += 1;

		int data_left_number = nowsize % disk_size;

		if (now_block_number < 14)
		{//这里只需要用到直接指针就好
			char wait2write_data[disk_size];
			disk_read(inode_info[blockoffset].direct_pointer[now_block_number], wait2write_data);
			memcpy(wait2write_data + data_left_number, buffer, size);
			disk_write(inode_info[blockoffset].direct_pointer[now_block_number], wait2write_data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}
		else if (now_block_number < 14 + 1024)
		{//这里用到了第一层间接指针
			char wait2write_data[disk_size];
			int now_indirect_block_number = now_block_number - 14;
			disk_block indirect_pointer_data[1];
			disk_read(inode_info[blockoffset].indirect_pointer[0], indirect_pointer_data);
			disk_read(indirect_pointer_data[0].pointer_nextblock[now_indirect_block_number], wait2write_data);
			memcpy(wait2write_data + data_left_number, buffer, size);
			disk_write(indirect_pointer_data[0].pointer_nextblock[now_indirect_block_number], wait2write_data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;

		}
		else if (now_block_number < 14 + 2*1024)
		{
			char wait2write_data[disk_size];
			int now_indirect_block_number = now_block_number - 14- 1024;
			disk_block indirect_pointer_data[1];
			disk_read(inode_info[blockoffset].indirect_pointer[1], indirect_pointer_data);
			disk_read(indirect_pointer_data[0].pointer_nextblock[now_indirect_block_number], wait2write_data);
			memcpy(wait2write_data + data_left_number, buffer, size);
			disk_write(indirect_pointer_data[1].pointer_nextblock[now_indirect_block_number], wait2write_data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}


	}

	//这里需要重新开一个块
	else
	{
		datablock_t new_blockid = find_empty_Dbm();

		//int collected_id[1024];
		



		set_Dbm(new_blockid);
		if (now_block_number < 14)
		{
			inode_info[blockoffset].direct_pointer[now_block_number] = new_blockid;
			char wait2data[disk_size];
			memcpy(wait2data, buffer, size);
			disk_write(new_blockid, wait2data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}

		else if (now_block_number == 14)
		{
			datablock_t new_indirect_blockid = find_empty_Dbm();
			set_Dbm(new_indirect_blockid);
			inode_info[blockoffset].indirect_pointer[0] = new_indirect_blockid;

			disk_block pointer_data[0];
			pointer_data[0].pointer_nextblock[0] = new_blockid;

			char wait2data[disk_size];
			memcpy(wait2data, buffer, size);
			disk_write(new_indirect_blockid, pointer_data);
			disk_write(new_blockid, wait2data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}
		else if (now_block_number < 14 + disk_size / (sizeof(int)))
		{
			disk_block pointer_data[0];
			disk_read(inode_info[blockoffset].indirect_pointer[0], pointer_data);
			pointer_data[0].pointer_nextblock[now_block_number - 14] = new_blockid;
			char wait2data[disk_size];
			memcpy(wait2data, buffer, size);
			disk_write(inode_info[blockoffset].indirect_pointer[0], pointer_data);
			disk_write(new_blockid, wait2data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}
		else if (now_block_number == 14 + disk_size / (sizeof(int)))
		{
			datablock_t new_indirect_blockid = find_empty_Dbm();
			set_Dbm(new_indirect_blockid);
			inode_info[blockoffset].indirect_pointer[1] = new_indirect_blockid;

			disk_block pointer_data[0];
			pointer_data[0].pointer_nextblock[0] = new_blockid;

			char wait2data[disk_size];
			memcpy(wait2data, buffer, size);
			disk_write(new_indirect_blockid, pointer_data);
			disk_write(new_blockid, wait2data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}
		else if (now_block_number < 14 + 2 * (disk_size / (sizeof(int))))
		{
			disk_block pointer_data[0];
			disk_read(inode_info[blockoffset].indirect_pointer[1], pointer_data);
			pointer_data[0].pointer_nextblock[now_block_number - 14- (disk_size / sizeof(int)) ] = new_blockid;
			char wait2data[disk_size] = { 0 };
			memcpy(wait2data, buffer, size);
			disk_write(inode_info[blockoffset].indirect_pointer[1], pointer_data);
			disk_write(new_blockid, wait2data);
			disk_write(_blockid(inode_number) + 4, inode_info);
			return size;
		}
		else
		{
			return 0;//不能写进block里了
		}


	}




	
	return 0;
}

int fs_truncate (const char *path, off_t size)
{ 
	//printf("Truncate is called:%s\n",path);
	inode_t inode_number = match_path(path);
	my_stat inode_info[32];

	disk_read(_blockid(inode_number) + 4, inode_info);
	inode_info[_blockoffset(inode_number)].ctime = time(NULL);

	int blockoffset = _blockoffset(inode_number);

	off_t nowsize = inode_info[_blockoffset(inode_number)].size;

	inode_info[_blockoffset(inode_number)].size = size;
	disk_write(_blockid(inode_number) + 4, inode_info);

	if (size == 0)
	{//不做处理

	}

	else if (nowsize > size)
	{//要释放block
		int block_num_begin = size / disk_size;
		if (size % disk_size == 0)
		{//处理一下剩余块的情况
			block_num_begin--;
		}
		int block_num_end = nowsize / disk_size;

		if (nowsize % disk_size == 0)
		{
			block_num_end--;
		}

		if (block_num_begin != block_num_end)
		{//相等的情况就不用处理了
			for (int i = block_num_begin+1; i <= block_num_end; i++)
			{
				if (i < 14) {
					empty_Dbm(inode_info[_blockoffset(inode_number)].direct_pointer[i]);
					//return size;
				}
				else if ((i - 14) < 1024)
				{
					disk_block data[1];
					//disk_block pointer_data[0];
					disk_read(inode_info[blockoffset].indirect_pointer[0],data);
					empty_Dbm(data[0].pointer_nextblock[i - 14]);

					if(i == 14)
					empty_Dbm(inode_info[blockoffset].indirect_pointer[0]);

				}
				else if ((i - 14) < 2 * 1024)
				{

					disk_block data[1];
					//disk_block pointer_data[0];
					disk_read(inode_info[blockoffset].indirect_pointer[1], data);
					empty_Dbm(data[0].pointer_nextblock[i - 14-1024]);

					if(i == (14+1024))
					empty_Dbm(inode_info[blockoffset].indirect_pointer[1]);

					//return size;
				}
			}
		}

	}
	else
	{//这里要分配block
		int block_num_end = size / disk_size;
		if (size % disk_size == 0)
		{//处理一下剩余块的情况
			block_num_end--;
		}
		int block_num_begin = nowsize / disk_size;

		if (nowsize % disk_size == 0)
		{
			block_num_begin--;
		}

		if (block_num_begin != block_num_end)
		{//相等的情况就不用处理了

			int findirect_number = 0;
			int sindirect_number = 0;

			for (int i = block_num_begin + 1; i <= block_num_end; i++)
			{
				if (i < 14) {
					int datablock_num = find_empty_Dbm();
					
					if (datablock_num == -1)
					{
						return -ENOSPC;
					}
					set_Dbm(datablock_num);
					inode_info[_blockoffset(inode_number)].direct_pointer[i] = datablock_num;
					//empty_Dbm(inode_info[_blockoffset(inode_number)].direct_pointer[i]);
					//return size;
				}
				else if ((i - 14) < 1024)
				{

					if (i == 14)
					{//这里需要分配间接指针
						findirect_number = find_empty_Dbm();

						if (findirect_number == -1)
						{
							return -ENOSPC;
						}
						set_Dbm(findirect_number);
						inode_info[blockoffset].indirect_pointer[0] = findirect_number;

					}

					int datablock_num = find_empty_Dbm();
					if (datablock_num == -1)
					{
						return -ENOSPC;
					}
					set_Dbm(datablock_num);
					disk_block data[1];
					//disk_block pointer_data[0];
					disk_read(findirect_number, data);
					data[0].pointer_nextblock[i - 14] = datablock_num;
					disk_write(findirect_number, data);

				}
				else
				{
					if (i == (14+1024))
					{//这里需要分配间接指针
						sindirect_number= find_empty_Dbm();

						if (sindirect_number == -1)
						{
							return -ENOSPC;
						}
						set_Dbm(sindirect_number);
						inode_info[blockoffset].indirect_pointer[1] = sindirect_number;

					}

					int datablock_num = find_empty_Dbm();
					if (datablock_num == -1)
					{
						return -ENOSPC;
					}
					set_Dbm(datablock_num);
					disk_block data[1];
					//disk_block pointer_data[0];
					disk_read(sindirect_number, data);
					data[0].pointer_nextblock[i - 14-1024] = datablock_num;
					disk_write(sindirect_number, data);
					//return size;
				}
			}
		}
	}

	return 0;
}

int fs_utime (const char *path, struct utimbuf *buffer)
{
	//printf("Utime is called:%s\n",path);

	inode_t inode_numer = match_path(path);
	my_stat inode_info[32];
	disk_read(_blockid(inode_numer)+4, inode_info);

	inode_info[_blockoffset(inode_numer)].ctime = time(NULL);
	

	buffer->actime = inode_info[_blockoffset(inode_numer)].atime;
	buffer->modtime = inode_info[_blockoffset(inode_numer)].mtime;

	disk_write(_blockid(inode_numer) +4, inode_info);
	return 0;
}

int fs_statfs (const char *path, struct statvfs *stat)
{
	//printf("Statfs is called:%s\n",path);
	

	//disk_read(superblock_number, stat);

	int bfree_number = 0;
	int bfiles = 0;

	char bfree_data1[disk_size];
	disk_read(Dbm_block_numebra, bfree_data1);
	char bfree_data2[disk_size];
	disk_read(Dbm_block_numebrb, bfree_data2);

	char file_data[disk_size];
	disk_read(Ibm_block_numebr, file_data);

	for (int byte_position = 0; byte_position < disk_size; byte_position++)
	{
		for (int bit_position = 0; bit_position < _bitsize; bit_position++)
		{
			if (!(bfree_data1[byte_position] & (0x1 << bit_position)))
			{
				bfree_number++;
			}
			if (!(bfree_data2[byte_position] & (0x1 << bit_position)))
			{
				bfree_number++;
			}
			if (!(file_data[byte_position] & (0x1 << bit_position)))
			{
				bfiles++;
			}
		}
	}

	stat->f_bfree = bfree_number;
	stat->f_bavail = bfree_number;

	stat->f_bsize = disk_size;
	stat->f_blocks = BLOCK_NUM;
	stat->f_files = 32768;
	stat->f_namemax = 24;

	stat->f_files = inode_numbermax- bfiles;
	stat->f_ffree = bfiles;
	stat->f_favail = bfiles;

	printf("files:%d f_blocks:%d\n\n\n", bfiles, bfree_number);

	

	disk_write(superblock_number, stat);
	return 0;
}

int fs_open (const char *path, struct fuse_file_info *fi)
{
	printf("Open is called:%s\n",path);
	return 0;
}












//Functions you don't actually need to modify
int fs_release (const char *path, struct fuse_file_info *fi)
{
	printf("Release is called:%s\n",path);
	/*if ((fi->fh) == 0x1)
	{
		(fi->fh) = 0;
		inode_t inodenumber = match_path(path);
		my_stat change[32];
		disk_read(_BID(inodenumber), change);
		change[_blockoffset(inodenumber)].atime = time(NULL);
		disk_write(_BID(inodenumber), change);
		printf("------------------------xc----\n\n\n");
	}*/
	return 0;
}

int fs_opendir (const char *path, struct fuse_file_info *fi)
{
	printf("Opendir is called:%s\n",path);
	return 0;
}

int fs_releasedir (const char * path, struct fuse_file_info *fi)
{
	printf("Releasedir is called:%s\n",path);


	return 0;
}

static struct fuse_operations fs_operations = {
	.getattr    = fs_getattr,
	.readdir    = fs_readdir,
	.read       = fs_read,
	.mkdir      = fs_mkdir,
	.rmdir      = fs_rmdir,
	.unlink     = fs_unlink,
	.rename     = fs_rename,
	.truncate   = fs_truncate,
	.utime      = fs_utime,
	.mknod      = fs_mknod,
	.write      = fs_write,
	.statfs     = fs_statfs,
	.open       = fs_open,
	.release    = fs_release,
	.opendir    = fs_opendir,
	.releasedir = fs_releasedir
};

int main(int argc, char *argv[])
{
	if(disk_init())
		{
		printf("Can't open virtual disk!\n");
		return -1;
		}
	if(mkfs())
		{
		printf("Mkfs failed!\n");
		return -2;
		}
    return fuse_main(argc, argv, &fs_operations, NULL);
}
