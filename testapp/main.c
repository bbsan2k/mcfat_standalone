#include "mcfat.h"
#include <stdio.h>
#include <string.h>

#define PAGE_SIZE 512

mcfat_mcops_t mcops;
mcfat_datasource_info_t mcdsi;

uint8_t mcardfile[8*1024*1024]; 
FILE* f;

int page_erase(mcfat_datasource_info_t* dsi, uint32_t page)
{
    memset(&mcardfile[page*PAGE_SIZE], 0x00, PAGE_SIZE);
    return 0;
}

int page_write(mcfat_datasource_info_t* dsi, uint32_t page, void* buffer)
{
    memcpy(&mcardfile[page*PAGE_SIZE], buffer, PAGE_SIZE);
    return 0;
}

int page_read(mcfat_datasource_info_t* dsi, uint32_t page, uint32_t count, void* buffer)
{
    memcpy(buffer, &mcardfile[page*PAGE_SIZE], count*PAGE_SIZE);
    return 0;
}

int info(mcfat_datasource_info_t* dsi)
{
    *dsi = mcdsi;
    return 0;
}
extern int McDetectCard2();
extern int mcfat_start(int argc, char *argv[]);
extern int McOpen( const char *filename, int flag);

int main(int argc, char* argv[])
{
    char* path = argv[1];
    f = fopen("./card.mcd", "r");
    fread(mcardfile, 1024*1024*8,1, f);
    fclose(f);
    printf("File read!\n");
    mcops.info = &info;
    mcops.page_erase = &page_erase;
    mcops.page_write = &page_write;
    mcops.page_read = &page_read;
    mcdsi.page_bytes = 512;
    mcdsi.block_pages = 16;
    mcdsi.blocks = (1024*1024*8)/(8*16);
    mcdsi.id = 0;
    mcdsi.mbits = 1000;

    mcfat_set_config(&mcops, &mcdsi);
    char a[0];
    mcfat_start(0, &a);

    //printf("Detect Card2: %d", McDetectCard2());
    sceMcTblGetDir dir = {};

    McDetectCard();
    McGetDir(path, 0, 1, &dir);
    printf("DIR: %s\n", dir.EntryName);
    while(McGetDir(path, 1, 1, &dir) > 0)
        printf("DIR: %s\n", dir.EntryName);

    int fd = McOpen("/BEDATA-SYSTEM/history", 1);
    printf("FD: %d\n", fd);
    char buff[1024*1024];
    McRead(fd,buff, 1024*1024);
    printf(buff);


    return 0;
}