/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright (c) 2009 jimmikaelkael
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#ifndef __MCMAN_INTERNAL_H__
#define __MCMAN_INTERNAL_H__


#include <time.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include "types.h"
#include "libmc-common.h"
#include "mcfat.h"



#ifdef DEBUG
    #define DPRINTF(format, args...) \
        printf("MCFat: " format, ##args)
#else
    #define DPRINTF(format, args...)
#endif

typedef struct _MCCacheDir {
    int  cluster;   // 0
    int  fsindex;   // 4
    int  maxent;    // 8
    uint32_t  unused;
} McCacheDir;

// Card Flags
#define CF_USE_ECC   				0x01
#define CF_BAD_BLOCK 				0x08
#define CF_ERASE_ZEROES 			0x10


#define MCMAN_CLUSTERSIZE 			1024
#define MCMAN_CLUSTERFATENTRIES		256

typedef struct _McFatCluster {
    int entry[MCMAN_CLUSTERFATENTRIES];
} McFatCluster;

#define MAX_CACHEENTRY 			0x24

typedef struct {
    int entry[1 + (MCMAN_CLUSTERFATENTRIES * 2)];
} McFatCache;

#define MAX_CACHEDIRENTRY 		0x3

typedef struct {  // size = 48
    uint8_t  status;   // 0
    uint8_t  wrflag;   // 1
    uint8_t  rdflag;   // 2
    uint8_t  unknown1; // 3
    uint8_t  drdflag;  // 4
    uint8_t  unknown2; // 5
    uint16_t unknown3; // 10
    uint32_t position; // 12
    uint32_t filesize; // 16
    uint32_t freeclink; // 20 link to next free cluster
    uint32_t clink;	  // 24  link to next cluster
    uint32_t clust_offset;// 28
    uint32_t field_20; // 32
    uint32_t field_24; // 36
    uint32_t field_28; // 40
    uint32_t field_2C; // 44
} MC_FHANDLE;

#define MAX_FDHANDLES 		3

#define MC_IO_S_RD SCE_STM_R
#define MC_IO_S_WR SCE_STM_W
#define MC_IO_S_EX SCE_STM_X
#define MC_IO_S_FL SCE_STM_F
#define MC_IO_S_DR SCE_STM_D

#define MC_IO_CST_ATTR SCE_CST_ATTR
#define MC_IO_CST_MODE SCE_CST_MODE
#define MC_IO_CST_CT SCE_CST_CT
#define MC_IO_CST_MT SCE_CST_MT

// internal functions prototypes

void long_multiply(uint32_t v1, uint32_t v2, uint32_t *HI, uint32_t *LO);
int  mcfat_chrpos(const char *str, int chr);
void mcfat_wmemset(void *buf, int size, int value);
int  mcfat_calcEDC(void *buf, int size);
int  mcfat_checkpath(const char *str);
int  mcfat_checkdirpath(const char *str1, const char *str2);
void mcfat_invhandles();
int  McCloseAll(void);
int  mcfat_dread(int fd, mcfat_dirent_t *dirent);
int  mcfat_getstat( const char *filename, mcfat_stat_t *stat);
int  mcfat_getmcrtime(sceMcStDateTime *tm);
void mcfat_initPS2com(void);
int  mcfat_eraseblock( int block, void **pagebuf, void *eccbuf);
int  mcfat_readpage( int page, void *buf, void *eccbuf);
int  mcfat_probePS2Card2();
int  mcfat_probePS2Card();
int  mcfat_getcnum ();
int  mcfat_correctdata(void *buf, void *ecc);
int  mcfat_sparesize();
int  mcfat_setdevspec();
int  mcfat_reportBadBlocks();
int  mcfat_setdevinfos();
int  mcfat_format2();
int  mcfat_fatRseek(int fd);
int  mcfat_fatWseek(int fd);
int  mcfat_findfree2( int reserve);
int  mcfat_dread2(int fd, mcfat_dirent_t *dirent);
int  mcfat_getstat2( const char *filename, mcfat_stat_t *stat);
int  mcfat_setinfo2( const char *filename, sceMcTblGetDir *info, int flags);
int  mcfat_read2(int fd, void *buffer, int nbyte);
int  mcfat_write2(int fd, void *buffer, int nbyte);
int  mcfat_close2(int fd);
int  mcfat_getentspace( const char *dirname);
int  mcfat_cachedirentry( const char *filename, McCacheDir *pcacheDir, McFsEntry **pfse, int unknown_flag);
int  mcfat_getdirinfo( McFsEntry *pfse, const char *filename, McCacheDir *pcd, int unknown_flag);
int  mcfat_open2( const char *filename, int flags);
int  mcfat_chdir( const char *newdir, char *currentdir);
int  mcfat_writecluster( int cluster, int flag);
int  mcfat_getdir2( const char *dirname, int flags, int maxent, sceMcTblGetDir *info);
int  mcfat_delete2( const char *filename, int flags);
int  mcfat_checkBackupBlocks();
int  mcfat_unformat2();
void mcfat_initcache(void);
int  mcfat_clearcache();
McCacheEntry *mcfat_getcacheentry( int cluster);
void mcfat_freecluster( int cluster);
int  mcfat_getFATindex( int num);
McCacheEntry *mcfat_get1stcacheEntp(void);
void mcfat_addcacheentry(McCacheEntry *mce);
int  mcfat_flushcacheentry(McCacheEntry *mce);
int  mcfat_readdirentryPS1( int cluster, McFsEntryPS1 **pfse);
int  mcfat_readclusterPS1( int cluster, McCacheEntry **pmce);
int  mcfat_replaceBackupBlock( int block);
int  mcfat_fillbackupblock1( int block, void **pagedata, void *eccdata);
int  mcfat_clearsuperblock();
int  mcfat_ioerrcode(int errcode);
void mcfat_unit2card(uint32_t unit);
int  mcfat_initdev(void);


typedef struct { 				// size = 384
    char  magic[28];				// Superblock magic, on PS2 MC : "Sony PS2 Memory Card Format "
    uint8_t  version[12];  			// Version number of the format used, 1.2 indicates full support for bad_block_list
    int16_t pagesize;				// size in bytes of a memory card page
    uint16_t pages_per_cluster;		// number of pages in a cluster
    uint16_t blocksize;				// number of pages in an erase block
    uint16_t unused;					// unused
    uint32_t clusters_per_card;		// total size in clusters of the memory card
    uint32_t alloc_offset;			// Cluster offset of the first allocatable cluster. Cluster values in the FAT and directory entries are relative to this. This is the cluster immediately after the FAT
    uint32_t alloc_end;				// The cluster after the highest allocatable cluster. Relative to alloc_offset. Not used
    uint32_t rootdir_cluster;		// First cluster of the root directory. Relative to alloc_offset. Must be zero
    uint32_t backup_block1;			// Erase block used as a backup area during programming. Normally the the last block on the card, it may have a different value if that block was found to be bad
    uint32_t backup_block2;			// This block should be erased to all ones. Normally the the second last block on the card
    uint8_t  unused2[8];
    uint32_t ifc_list[32];			// List of indirect FAT clusters. On a standard 8M card there's only one indirect FAT cluster
    int bad_block_list[32];		// List of erase blocks that have errors and shouldn't be used
    uint8_t  cardtype;				// Memory card type. Must be 2, indicating that this is a PS2 memory card
    uint8_t  cardflags;				// Physical characteristics of the memory card
    uint16_t unused3;
    uint32_t cluster_size;
    uint32_t FATentries_per_cluster;
    uint32_t clusters_per_block;
    int cardform;
    uint32_t rootdir_cluster2;
    uint32_t unknown1;
    uint32_t unknown2;
    uint32_t max_allocatable_clusters;
    uint32_t unknown3;
    uint32_t unknown4;
    int unknown5;
} MCDevInfo;

union mcfat_pagebuf {
    uint32_t word[1056/sizeof(uint32_t)];
    uint8_t byte[1056/sizeof(uint8_t)];
    char magic[1056/sizeof(char)];
};

union mcfat_PS1PDApagebuf {
    uint32_t word[128/sizeof(uint32_t)];
    uint16_t half[128/sizeof(uint16_t)];
    uint8_t byte[128/sizeof(uint8_t)];
};

// Defined in main.c

extern char SUPERBLOCK_MAGIC[];
extern char SUPERBLOCK_VERSION[];

extern int mcfat_wr_block;
extern int mcfat_wr_flag3;
extern int mcfat_curdircluster;

extern union mcfat_pagebuf mcfat_pagebuf;
extern union mcfat_PS1PDApagebuf mcfat_PS1PDApagebuf;

extern int PS1CardFlag;

extern McFsEntry mcfat_dircache[MAX_CACHEDIRENTRY];

extern MC_FHANDLE mcfat_fdhandles[MAX_FDHANDLES];
extern MCDevInfo mcfat_devinfos;

extern uint8_t mcfat_eccdata[512]; // size for 32 ecc

// Defined in mcsio2.c
extern uint8_t mcfat_sio2outbufs_PS1PDA[0x90];

extern mcfat_mcops_t mcops;
extern mcfat_datasource_info_t mcdsi;

#endif	// __MCMAN_INTERNAL_H__
