#include<stdio.h>
#include<stdint.h>
#include<stdlib.h>

#define BLOCK_SIZE  512
#define INODE_SIZE  64
#define HEAD_LENGTH 8
#define BLOCK_DATA  (BLOCK_SIZE-11)
#define NAME_LENGTH (INODE_SIZE-9)

#if INODE_SIZE < 32
#error INODE_SIZE has to be > 32
#endif

#if HEAD_LENGTH < 1
#error HEAD_LENGTH too small
#endif

#if BLOCK_SIZE < INODE_SIZE
#error BLOCK_SIZE has to be >= INODE_SIZE
#endif

const char fs_mainfile[]="fs.img";
uint32_t FS_END=0;

uint32_t freeblocks;
uint64_t freespace;
uint64_t freeinodes;

typedef struct block
{
	uint8_t used;
	uint32_t next;
	uint16_t used_space;
	uint8_t data[BLOCK_DATA];
} block;

typedef struct inode
{
	char name[NAME_LENGTH];
	uint8_t used;
	uint32_t file_size;
	uint32_t begin; //first block number
} inode;

typedef struct head_stats
{
	uint32_t inode_size;
	uint32_t block_size;
	uint32_t head_length;
	uint32_t freeblocks;
	uint64_t freespace;
	uint64_t freeinodes;
	char fs_name[INODE_SIZE-32];
} head_stats;

typedef struct head
{
	inode inodes[BLOCK_SIZE/INODE_SIZE];
} head;

head fat[HEAD_LENGTH]; //we need to skip first inode in fat[0], since it contains head_stats
FILE *fs;

int check_fs()
{
	fseek(fs, 0, SEEK_SET);
	
	head_stats ths;
	uint8_t err=0;
	
	fread(&ths, INODE_SIZE, 1, fs);
	
	if(ths.inode_size!=INODE_SIZE)
	{
		printf("inode size doesn't match (expected %d, got %d)\n", INODE_SIZE, ths.inode_size);
		return 0;
	}
	
	if(ths.block_size!=BLOCK_SIZE)
	{
		printf("block size doesn't match (expected %d, got %d)\n", BLOCK_SIZE, ths.block_size);
		err++;
	}
	
	if(ths.head_length!=HEAD_LENGTH)
	{
		printf("head length doesn't match (expected %d, got %d)\n", HEAD_LENGTH, ths.head_length);
		err++;
	}
	
	if(FS_END<=HEAD_LENGTH)
	{
		printf("filesystem image too small to continue\n");
		err++;
	}
	
	if(!err)
	{
		freespace=ths.freespace;
		freeinodes=ths.freeinodes;
		freeblocks=ths.freeblocks;
	}
	
	return err==0;
}

void update_head_stats()
{
	head_stats ths;
	
	ths.inode_size=INODE_SIZE;
	ths.block_size=BLOCK_SIZE;
	ths.head_length=HEAD_LENGTH;
	ths.freeblocks=freeblocks;
	ths.freespace=freespace;
	ths.freeinodes=freeinodes;
	
	fseek(fs, 0, SEEK_SET);
	fwrite(&ths, INODE_SIZE, 1, fs);
}

void write_fat()
{
	fseek(fs, 0, SEEK_SET);
	fwrite(fat, sizeof(head), HEAD_LENGTH, fs);
	
	update_head_stats();
}

void stats()
{
	if(freespace<1024)
		printf("%d free blocks (%d B)\n%d free inodes\n", freeblocks, freespace, freeinodes);
	else if(freespace<1024*1024)
		printf("%d free blocks (%d kB)\n%d free inodes\n", freeblocks, freespace/1024, freeinodes);
	else if(freespace<1024*1024*1024)
		printf("%d free blocks (%d MB)\n%d free inodes\n", freeblocks, freespace/1024/1024, freeinodes);
	else if(freespace<1024*1024*1024*1024l)
		printf("%d free blocks (%d GB)\n%d free inodes\n", freeblocks, freespace/1024/1024/1024, freeinodes);
}

int name_exist(char* name)
{
	for(int i=0; i<HEAD_LENGTH; i++)
	{
		for(int j=0; j<BLOCK_SIZE/INODE_SIZE; j++)
		{
			if(!j && !i) continue;
			
			if(!fat[i].inodes[j].used)
				continue;
			
			int found=1;
			for(int k=0; k<NAME_LENGTH-1 && name[k]!=0 && found; k++)
			{
				if(name[k]!=fat[i].inodes[j].name[k])
				{
					found=0;
				}
			}
			if(found)
				return 1;
		}
	}
	return 0;
}

int find_file(char* name, uint32_t* fatnr, uint32_t* inodenr)
{
	if(!name_exist(name))
		return 0;
	
	for(int i=0; i<HEAD_LENGTH; i++)
	{
		for(int j=0; j<BLOCK_SIZE/INODE_SIZE; j++)
		{
			if(!j && !i) continue;
			
			if(!fat[i].inodes[j].used)
				continue;
			
			int found=1;
			for(int k=0; k<NAME_LENGTH-1 && name[k]!=0 && found; k++)
			{
				if(name[k]!=fat[i].inodes[j].name[k])
				{
					found=0;
				}
			}
			if(found)
			{
				*fatnr=i;
				*inodenr=j;
				return 1;
			}
		}
	}
}


int format(int blocks)
{
	if(blocks<=HEAD_LENGTH)
	{
		printf("cannot format, too small size given\n");
		return -1;
	}
	fclose(fs);
	fs=fopen(fs_mainfile, "wb");
	uint8_t empty_block[BLOCK_SIZE];
	for(int i=0; i<BLOCK_SIZE; i++){empty_block[i]=0;}
	
	fseek(fs, 0, SEEK_SET);
	
	for(int i=0; i<blocks; i++)
	{
		fwrite(empty_block, sizeof(uint8_t), BLOCK_SIZE, fs);
	}
	
	head_stats ths;
	
	ths.inode_size=INODE_SIZE;
	ths.block_size=BLOCK_SIZE;
	ths.head_length=HEAD_LENGTH;
	ths.freeblocks=blocks-HEAD_LENGTH;
	ths.freespace=ths.freeblocks*BLOCK_DATA;
	ths.freeinodes=(BLOCK_SIZE/INODE_SIZE)*HEAD_LENGTH-1;
	
	fseek(fs, 0, SEEK_SET);
	fwrite(&ths, INODE_SIZE, 1, fs);
	
	fclose(fs);
	fs=fopen(fs_mainfile, "r+b");
	FS_END=blocks;
	fread(fat, BLOCK_SIZE, HEAD_LENGTH, fs);
	
	freespace=ths.freeblocks*BLOCK_DATA;
	freeblocks=ths.freeblocks;
	freeinodes=ths.freeinodes;
	
	stats();
}

void delete_fs()
{
	fclose(fs);
	printf("Deleting whole filesystem\n");
	fs=fopen(fs_mainfile, "w");
	fclose(fs);
}

int delete_file(char* name)
{
	uint32_t fatnr, inodenr;
	if(!find_file(name, &fatnr, &inodenr))
	{
		printf("File to delete not found\n");
		return -1;
	}
	
	freeinodes++;
	
	uint32_t pos=fat[fatnr].inodes[inodenr].begin;
	
	block tb;
	
	while(pos!=0)
	{
		uint32_t oldpos=pos;
		fseek(fs, pos*BLOCK_SIZE, SEEK_SET);
		fread(&tb, BLOCK_SIZE, 1, fs);
		tb.used=0;
		pos=tb.next;
		tb.next=0;
		
		fseek(fs, oldpos*BLOCK_SIZE, SEEK_SET);
		fwrite(&tb, BLOCK_SIZE, 1, fs);
		
		freeblocks++;
		freespace+=BLOCK_DATA;
		
	}
	
	fat[fatnr].inodes[inodenr].used=0;
	
	write_fat();
	
	return 0;
}

void list()
{
	printf("\n");
	for(int i=0; i<HEAD_LENGTH; i++)
	{
		for(int j=0; j<BLOCK_SIZE/INODE_SIZE; j++)
		{
			if(!j && !i) continue;
			
			if(fat[i].inodes[j].used)
				printf("(%d, %d) %s\t%d\n", i, j, fat[i].inodes[j].name, fat[i].inodes[j].file_size);
		}
	}
}

void print_map(uint8_t mode)
{
	uint8_t map[FS_END];
	for(int i=0; i<FS_END; i++)
	{
		map[i]=0;
	}
	for(int i=0; i<HEAD_LENGTH; i++)
	{
		map[i]=1;
	}
	
	if(mode==0)
	{
		fseek(fs, HEAD_LENGTH*BLOCK_SIZE, SEEK_SET);
		for(int i=HEAD_LENGTH; i<FS_END; i++)
		{
			block tb;
			fread(&tb, BLOCK_SIZE, 1, fs);
			
			map[i]=tb.used*2;
		}
	}
	else
	{
		for(int i=0; i<HEAD_LENGTH; i++)
		{
			for(int j=0; j<BLOCK_SIZE/INODE_SIZE; j++)
			{
				if(!j && !i) continue;
				
				if(fat[i].inodes[j].used)
				{
					uint32_t pos=fat[i].inodes[j].begin;
					char l=fat[i].inodes[j].name[0];
					block tb;
					while(pos!=0)
					{
						map[pos]=l;
						fseek(fs, pos*BLOCK_SIZE, SEEK_SET);
						fread(&tb, BLOCK_SIZE, 1, fs);
						pos=tb.next;
					}
				}
			}
		}
	}
	
	for(int i=0; i<FS_END; i++)
	{
		if(map[i]==0)
			printf("\x1B[32m_\x1B[0m");
		else if(map[i]==1)
			printf("\x1B[34mH\x1B[0m");
		else if(map[i]==2)
			printf("\x1B[31mX\x1B[0m");
		else
			printf("\x1B[31m%c\x1B[0m", map[i]);
	}
	printf("\n");
}

uint32_t write_block(uint32_t prev_block, uint16_t used_space, uint8_t* data) //returns block number
{
	if(freeblocks<1)
	{
		printf("No more space left!\n");
		return 0;
	}
	
	int found=0;
	uint32_t pos;
	
	if(prev_block==0)
		pos=HEAD_LENGTH;
	else
		pos=prev_block+1;
	
	block tb;
	
	fseek(fs, pos*BLOCK_SIZE, SEEK_SET);
	
	while(pos<FS_END)
	{
		fread(&tb, BLOCK_SIZE, 1, fs);
		
		if(tb.used==0)
		{
			found=1;
			break;
		}
		pos++;
	}
	
	if(!found)
	{
		printf("(Internal error) No more space left!\n");
		return 0;
	}

	tb.used=1;
	tb.used_space=used_space;
	tb.next=0;
	
	for(int i=0; i<BLOCK_DATA; i++)
		tb.data[i]=data[i];
	
	fseek(fs, pos*BLOCK_SIZE, SEEK_SET);
	fwrite(&tb, BLOCK_SIZE, 1, fs);
	
	//update previous block
	
	if(prev_block!=0)
	{
		fseek(fs, prev_block*BLOCK_SIZE, SEEK_SET);
		fread(&tb, BLOCK_SIZE, 1, fs);
		
		tb.next=pos;
		
		fseek(fs, prev_block*BLOCK_SIZE, SEEK_SET);
		fwrite(&tb, BLOCK_SIZE, 1, fs);
	}
	
// 	printf("Wrote %d bytes to block %d\n", used_space, pos);
	
	freeblocks--;
	freespace-=BLOCK_DATA;
	
	update_head_stats();
	
	return pos;
}

int upload_file(char* name)
{
	if(name_exist(name))
	{
		printf("Name of uploaded file already exist\n");
		return -1;
	}
	
	if(freeinodes==0)
	{
		printf("No more free inodes\n");
		return -1;
	}
	
	uint16_t fatnr, inodenr;
	uint8_t found=0;
	
	for(int i=0; i<HEAD_LENGTH && !found; i++)
	{
		for(int j=0; j<BLOCK_SIZE/INODE_SIZE && !found; j++)
		{
			if(!j && !i) continue;
			
			if(fat[i].inodes[j].used==0)
			{
				found=1;
				fatnr=i;
				inodenr=j;
			}
		}
	}
	
	if(!found)
	{
		printf("(Internal error) No more free inodes\n");
		return -1;
	}
	
	FILE* ext;
	ext=fopen(name, "rb");
	
	if(ext==NULL)
	{
		printf("Error while opening file to upload\n");
		return -1;
	}
	
	uint8_t data[BLOCK_DATA], onebyte;
	
	for(int i=0; i<BLOCK_DATA; i++)
		data[i]=0;
	
	fseek(ext, 0, SEEK_END);
	uint32_t ext_size=ftell(ext);
	
	if(ext_size>freespace)
	{
		printf("Not enough space, additional %d B needed\n", ext_size-freespace);
		return -1;
	}
	
	fseek(ext, 0, SEEK_SET);
	printf("Uploading file (size: %dB), position in fat: %d,%d\n", ext_size, fatnr, inodenr);
	
	fat[fatnr].inodes[inodenr].used=1;
	fat[fatnr].inodes[inodenr].file_size=ext_size;
	
	freeinodes--;
	
	for(int i=0; i<NAME_LENGTH; i++)
		fat[fatnr].inodes[inodenr].name[i]=0;
	
	for(int i=0; i<NAME_LENGTH-1 && name[i]!=0; i++)
		fat[fatnr].inodes[inodenr].name[i]=name[i];
	
	uint32_t epos=0, data_pos=0, prev_block=0;
	
	while(epos<ext_size)
	{
		if(epos+BLOCK_DATA<ext_size)
		{
			fread(data, BLOCK_DATA, 1, ext);
			epos+=BLOCK_DATA-1;
			data_pos=BLOCK_DATA;
		}
		else
		{
			fread(&onebyte, sizeof(uint8_t), 1, ext);
			data[data_pos++]=onebyte;
		}
		
		if(data_pos==BLOCK_DATA)
		{
			if(prev_block==0) //first block
				fat[fatnr].inodes[inodenr].begin=prev_block=write_block(prev_block, data_pos, data);
			else
				prev_block=write_block(prev_block, data_pos, data);
			
			if(prev_block==0)
			{
				printf("Error while uploading file\n");
				return -1;
			}
			
			for(int i=0; i<BLOCK_DATA; i++)
				data[i]=0;
			
			data_pos=0;
		}
		epos++;
	}
	
	if(data_pos>0)
	{
		if(prev_block==0) //first block
			fat[fatnr].inodes[inodenr].begin=prev_block=write_block(prev_block, data_pos, data);
		else
			prev_block=write_block(prev_block, data_pos, data);
		
		if(prev_block==0)
		{
			printf("Error while uploading file\n");
			return -1;
		}
	}
	
	write_fat();
	
	fclose(ext);
}

int download_file(char* name, char* outname)
{
	uint32_t fatnr, inodenr;
	if(!find_file(name, &fatnr, &inodenr))
	{
		printf("File not found\n");
		return -1;
	}
	
	FILE *ext=fopen(outname, "wb");
	
	uint32_t pos=fat[fatnr].inodes[inodenr].begin;
	fseek(fs, pos*BLOCK_SIZE, SEEK_SET);
	
	block tb;
	
	uint64_t out=0;
	
	while(pos!=0)
	{
		fread(&tb, BLOCK_SIZE, 1, fs);
		
		out+=tb.used_space;
		
		fwrite(tb.data, sizeof(uint8_t), tb.used_space, ext);
		
		pos=tb.next;
	}
	
	fclose(ext);
	
	printf("Saved out %dB\n", out);
	
	return 0;
}

int main(int argc, char *argv[])
{
	if(sizeof(inode)!=INODE_SIZE)
	{
		printf("Internal error, check inode size\n");
		return 1;
	}
	if(sizeof(head_stats)!=INODE_SIZE)
	{
		printf("Internal error, check head_stats size\n");
		return 1;
	}
	if(sizeof(block)!=BLOCK_SIZE)
	{
		printf("Internal error, check block size\n");
		return 1;
	}
	if(sizeof(head)!=BLOCK_SIZE)
	{
		printf("Internal error, check head size\n");
		return 1;
	}
	
	if(argc==2 && argv[1][1]=='h')
	{
		printf("Simple linked-list filesystem in file\n\nAvailable paramaters:\n");
		printf("-l\t\t\tPrints file list\n");
		printf("-m\t\t\tPrints filesystem map, used space represented by first letter of filename, generated using fat table\n");
		printf("-n\t\t\tPrints filesystem map, generated by asking every block if it is used (shows also lost blocks (if any))\n");
		printf("-s\t\t\tPrints free space and free inodes stats\n");
		printf("-x\t\t\tClears filesystem image file\n");
		printf("-h\t\t\tPrints this help\n");
		printf("-f <size>\t\tFormat filesystem image file, size given in blocks\n");
		printf("-u <file>\t\tUploads file from disk to filesystem image file\n");
		printf("-d <file>\t\tDownloads file from filesystem image file to disk\n");
		printf("-d <file1> <file2>\tDownloads file <file1> from filesystem image file to disk as <file2>\n");
		printf("-r <file>\t\tRemoves file from filesystem image file\n");
		return 0;
	}
	
	fs=fopen(fs_mainfile, "r+b");
	if(fs==NULL)
	{
		printf("Creating new fs file: %s\n", fs_mainfile);
		fs=fopen(fs_mainfile, "wb");
		fclose(fs);
		fs=fopen(fs_mainfile, "r+b");
		format(HEAD_LENGTH+2);
	}
	fseek(fs, 0, SEEK_END);
	FS_END=ftell(fs)/BLOCK_SIZE;
	fseek(fs, 0, SEEK_SET);
	
	if(argc==3 && argv[1][1]=='f')
	{
		format(atoi(argv[2]));
		return 0;
	}
	
	if(!check_fs())
	{
		printf("Checking filesystem failed. Format, create new image or fix settings\n");
		return 1;
	}
	
	printf("Opened fs:\n");
	stats();
	
	fseek(fs, 0, SEEK_SET);
	fread(fat, BLOCK_SIZE, HEAD_LENGTH, fs);
	
	if(argc==2)
	{
		if(argv[1][1]=='l')
			list();
		else if(argv[1][1]=='m')
			print_map(1);
		else if(argv[1][1]=='n')
			print_map(0);
		else if(argv[1][1]=='s')
			stats();
		else if(argv[1][1]=='x')
			delete_fs();
		else
		{
			printf("Wrong argument\n");
			return 1;
		}
	}
	else if(argc==3)
	{
		if(argv[1][1]=='u')
			upload_file(argv[2]);
		else if(argv[1][1]=='d')
			download_file(argv[2], argv[2]);
		else if(argv[1][1]=='r')
			delete_file(argv[2]);
		else
		{
			printf("Wrong argument\n");
			return 1;
		}
	}
	else if(argc==4)
	{
		if(argv[1][1]=='d')
			download_file(argv[2], argv[3]);
		else
		{
			printf("Wrong argument\n");
			return 1;
		}
	}
	else
	{
		printf("Wrong argument count!\n");
		return 1;
	}
}
