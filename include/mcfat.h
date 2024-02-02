#ifndef __MCFAT_H_
#define __MCFAT_H_

#include <stdint.h>
#include <stdbool.h>

// Defines

#ifdef MCFAT_DEBUG
    #define DPRINTF(format, args...) \
        printf("MCFat: " format, ##args)
#else
    #define DPRINTF(format, args...)
#endif


// MCMAN basic error codes
#define sceMcResSucceed         0
#define sceMcResChangedCard     -1
#define sceMcResNoFormat        -2
#define sceMcResFullDevice      -3
#define sceMcResNoEntry         -4
#define sceMcResDeniedPermit    -5
#define sceMcResNotEmpty        -6
#define sceMcResUpLimitHandle   -7
#define sceMcResFailReplace     -8
#define sceMcResFailResetAuth   -11
#define sceMcResFailDetect      -12
#define sceMcResFailDetect2     -13
#define sceMcResDeniedPS1Permit -51
#define sceMcResFailAuth        -90

/* High-Level File I/O */
// Used with the statmask field of chstat() to indicate which field(s) to change.
#define SCE_CST_MODE 0x01
#define SCE_CST_ATTR 0x02
#define SCE_CST_SIZE 0x04
#define SCE_CST_CT   0x08
#define SCE_CST_AT   0x10
#define SCE_CST_MT   0x20
#define SCE_CST_PRVT 0x40

// Used with the mode field of chstat() to indicate what to change.
#define SCE_STM_R 0x01
#define SCE_STM_W 0x02
#define SCE_STM_X 0x04
#define SCE_STM_C 0x08
#define SCE_STM_F 0x10
#define SCE_STM_D 0x20



// Used with the mode field of chstat() to indicate what to change.
#define SCE_STM_R 0x01
#define SCE_STM_W 0x02
#define SCE_STM_X 0x04
#define SCE_STM_C 0x08
#define SCE_STM_F 0x10
#define SCE_STM_D 0x20

/* file attributes */
#define sceMcFileAttrReadable    SCE_STM_R // Readable
#define sceMcFileAttrWriteable   SCE_STM_W // Writable
#define sceMcFileAttrExecutable  SCE_STM_X // Executable
#define sceMcFileAttrDupProhibit SCE_STM_C // Copy Protected
#define sceMcFileAttrFile        SCE_STM_F // Is a file.
#define sceMcFileAttrSubdir      SCE_STM_D // Is a sub-directory
#define sceMcFileCreateDir       0x0040    // Used internally to create directories.
#define sceMcFileAttrClosed      0x0080    // Indicates whether a file _may_ not have been written properly. Earlier browsers do not copy this flag.
#define sceMcFileCreateFile      0x0200    // Equivalent in value and functionality to O_CREAT.
#define sceMcFile0400            0x0400    // Set during creation.
#define sceMcFileAttrPDAExec     0x0800    // PDA Application (1st Generation PDA Download)
#define sceMcFileAttrPS1         0x1000    // PlayStation-format data
#define sceMcFileAttrHidden      0x2000    // Indicates whether the file is a hidden file (but not to the browser).
#define sceMcFileAttrExists      0x8000    // Indicates whether the file exists.

/* Valid information bit fields for sceMcSetFileInfo */
#define sceMcFileInfoCreate 0x01 // Creation Date/Time
#define sceMcFileInfoModify 0x02 // Modification Date/Time
#define sceMcFileInfoAttr   0x04 // File Attributes



// Memory Card device types
#define sceMcTypeNoCard 0
#define sceMcTypePS2    2


// structs
typedef struct _datasource_info
{
    uint32_t	id;
    uint32_t	mbits;
    uint32_t	page_bytes;	/* bytes/page */
    uint32_t	block_pages;	/* pages/block */
    uint32_t	blocks;
} mcfat_datasource_info_t;

typedef struct _mc_ops
{
    int (*page_erase)(mcfat_datasource_info_t*, uint32_t);
    int (*page_write)(mcfat_datasource_info_t*, uint32_t, void*);
    int (*page_read)(mcfat_datasource_info_t*, uint32_t, uint32_t, void*);
    int (*info)(mcfat_datasource_info_t*);
} mcfat_mcops_t;


typedef struct _sceMcStDateTime
{
    uint8_t Resv2;
    uint8_t Sec;
    uint8_t Min;
    uint8_t Hour;
    uint8_t Day;
    uint8_t Month;
    uint16_t Year;
} sceMcStDateTime;

typedef struct
{                             // size = 512
    uint16_t mode;                 // 0
    uint16_t unused;               // 2
    uint32_t length;               // 4
    sceMcStDateTime created;  // 8
    uint32_t cluster;              // 16
    uint32_t dir_entry;            // 20
    sceMcStDateTime modified; // 24
    uint32_t attr;                 // 32
    uint32_t unused2[7];           // 36
    char name[32];            // 64
    uint8_t unused3[416];          // 96
} McFsEntry;

/* MCMAN public structure */
typedef struct _sceMcTblGetDir {	// size = 64
	sceMcStDateTime _Create;	// 0
	sceMcStDateTime _Modify;	// 8
	uint32_t FileSizeByte;		// 16
	uint16_t AttrFile;			// 20
	uint16_t Reserve1;			// 22
	uint32_t Reserve2;			// 24
	uint32_t PdaAplNo;			// 28
	char EntryName[32];	// 32
} sceMcTblGetDir;

typedef struct _MCCacheEntry
{
    int32_t cluster;  // 0
    uint8_t *cl_data;  // 4
    uint8_t wr_flag;   // 10
    uint8_t rd_flag;   // 12
    uint8_t unused[3]; // 13
} McCacheEntry;

// Function Declarations

int  McDetectCard();
int  McOpen( const char *filename, int flags);
int  McClose(int fd);
int  McRead(int fd, void *buf, int length);
int  McWrite(int fd, void *buf, int length);
int  McSeek(int fd, int offset, int origin);
int  McFormat();
int  McGetDir( const char *dirname, int flags, int maxent, sceMcTblGetDir *info);
int  McDelete( const char *filename, int flags);
int  McFlush(int fd);
int  McChDir( const char *newdir, char *currentdir);
int  McSetFileInfo( const char *filename, sceMcTblGetDir *info, int flags);
int  McEraseBlock( int block, void **pagebuf, void *eccbuf);
int  McReadPage( int page, void *buf);
int  McWritePage( int page, void *pagebuf, void *eccbuf);
void McDataChecksum(void *buf, void *ecc);
int  McUnformat();
int  McGetFreeClusters();
int  McGetMcType();
int  McEraseBlock2( int block, void **pagebuf, void *eccbuf);
int  McGetFormat();
int  McGetEntSpace( const char *dirname);
int  McReplaceBadBlock(void);
int  McCloseAll(void);
int  McGetCardSpec( int16_t *pagesize, uint16_t *blocksize, int *cardsize, uint8_t *flags);
int  McGetFATentry( int fat_index, int *fat_entry);
int  McCheckBlock( int block);
int  McSetFATentry( int fat_index, int fat_entry);
int  McReadDirEntry( int cluster, int fsindex, McFsEntry **pfse);
void Mc1stCacheEntSetWrFlagOff(void);
int McCreateDirentry( int parent_cluster, int num_entries, int cluster, const sceMcStDateTime *ctime);
int McReadCluster( int cluster, McCacheEntry **pmce);
int McFlushCache();
int McSetDirEntryState( int cluster, int fsindex, int flags);
bool McDirExists( const char* dirname );
int McCreateDir( const char* dirname );
bool McFileExists( const char* filename );


void mcfat_set_config(mcfat_mcops_t* ops, mcfat_datasource_info_t* info);
int mcfat_start();


#endif