#ifndef TYPES_H
#define TYPES_H

#define DIR 0x040000 
#define FILE 0x100000 

typedef unsigned int uint;
typedef unsigned short ushort;
typedef unsigned char uchar;
typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int uint32;
typedef unsigned long uint64;

//chp6 Stat from slides
typedef struct {
	uint64 dev;	//drive number of the disk where the file is located, to be 0
	uint64 ino;	//inode the inode number where the inode file is lcoated
	uint32 mode;	//file type 
	uint32 nlink;	//the number of hard links, initially 1
	uint64 pad[7];	//for compatability only
} Stat;


#endif // TYPES_H