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
    memset(&mcardfile[page*PAGE_SIZE], 0xFF, PAGE_SIZE);
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

    McSetConfig(&mcops, &mcdsi);
    char a[0];
    McStart();

    sceMcTblGetDir dir = {};

    McDetectCard();
    McGetDir(path, 0, 1, &dir);
    printf("DIR: %s\n", dir.EntryName);
    while(McGetDir(path, 1, 1, &dir) > 0)
        printf("DIR: %s\n", dir.EntryName);


    if (!McFileExists("/BADATA-SYSTEM/history"))
    {
        printf("File didn't exist.\n");
        if (!McDirExists("/BADATA-SYSTEM"))
        {
            printf("Dir didn't exist... creating...\n");
            McCreateDir("/BADATA-SYSTEM");
        }
    }
    int f = sceMcFileCreateFile  | sceMcFileAttrWriteable | sceMcFileAttrReadable;
    int fd = McOpen("/BADATA-SYSTEM/history", f);

    printf("FD: %d\n", fd);
    char buff[462];
    int length = McWrite(fd,buff, 462);
    printf("History wrote %d Bytes\n", length);

    return 0;
}