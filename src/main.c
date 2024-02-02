/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright (c) 2009 jimmikaelkael
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#include "mcfat.h"
#include "mcfat-internal.h"
#include "types.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

const char SUPERBLOCK_MAGIC[] = "Sony PS2 Memory Card Format ";
const char SUPERBLOCK_VERSION[] = "1.2.0.0";


mcfat_mcops_t mcops;
mcfat_datasource_info_t mcdsi;

int mcfat_wr_block = -1;
int mcfat_wr_flag3 = -10;
int mcfat_curdircluster = -1;

union mcfat_pagebuf mcfat_pagebuf;

static uint8_t mcfat_cachebuf[MAX_CACHEENTRY * MCMAN_CLUSTERSIZE];
static McCacheEntry mcfat_entrycache[MAX_CACHEENTRY];
static McCacheEntry *mcfat_mccache[MAX_CACHEENTRY];

static McCacheEntry *pmcfat_entrycache;
static McCacheEntry **pmcfat_mccache;

static void *mcfat_pagedata[32];
static uint8_t mcfat_backupbuf[16384];

static int mcfat_badblock;
static int mcfat_replacementcluster[16];

static McFatCache mcfat_fatcache;
McFsEntry mcfat_dircache[MAX_CACHEDIRENTRY];

MC_FHANDLE mcfat_fdhandles[MAX_FDHANDLES];
MCDevInfo mcfat_devinfos;

uint8_t mcfat_eccdata[512]; // size for 32 ecc

// mcfat xor table
// clang-format off
static const uint8_t mcfat_xortable[256] = {
    0x00, 0x87, 0x96, 0x11, 0xA5, 0x22, 0x33, 0xB4,
    0xB4, 0x33, 0x22, 0xA5, 0x11, 0x96, 0x87, 0x00,
    0xC3, 0x44, 0x55, 0xD2, 0x66, 0xE1, 0xF0, 0x77,
    0x77, 0xF0, 0xE1, 0x66, 0xD2, 0x55, 0x44, 0xC3,
    0xD2, 0x55, 0x44, 0xC3, 0x77, 0xF0, 0xE1, 0x66,
    0x66, 0xE1, 0xF0, 0x77, 0xC3, 0x44, 0x55, 0xD2,
    0x11, 0x96, 0x87, 0x00, 0xB4, 0x33, 0x22, 0xA5,
    0xA5, 0x22, 0x33, 0xB4, 0x00, 0x87, 0x96, 0x11,
    0xE1, 0x66, 0x77, 0xF0, 0x44, 0xC3, 0xD2, 0x55,
    0x55, 0xD2, 0xC3, 0x44, 0xF0, 0x77, 0x66, 0xE1,
    0x22, 0xA5, 0xB4, 0x33, 0x87, 0x00, 0x11, 0x96,
    0x96, 0x11, 0x00, 0x87, 0x33, 0xB4, 0xA5, 0x22,
    0x33, 0xB4, 0xA5, 0x22, 0x96, 0x11, 0x00, 0x87,
    0x87, 0x00, 0x11, 0x96, 0x22, 0xA5, 0xB4, 0x33,
    0xF0, 0x77, 0x66, 0xE1, 0x55, 0xD2, 0xC3, 0x44,
    0x44, 0xC3, 0xD2, 0x55, 0xE1, 0x66, 0x77, 0xF0,
    0xF0, 0x77, 0x66, 0xE1, 0x55, 0xD2, 0xC3, 0x44,
    0x44, 0xC3, 0xD2, 0x55, 0xE1, 0x66, 0x77, 0xF0,
    0x33, 0xB4, 0xA5, 0x22, 0x96, 0x11, 0x00, 0x87,
    0x87, 0x00, 0x11, 0x96, 0x22, 0xA5, 0xB4, 0x33,
    0x22, 0xA5, 0xB4, 0x33, 0x87, 0x00, 0x11, 0x96,
    0x96, 0x11, 0x00, 0x87, 0x33, 0xB4, 0xA5, 0x22,
    0xE1, 0x66, 0x77, 0xF0, 0x44, 0xC3, 0xD2, 0x55,
    0x55, 0xD2, 0xC3, 0x44, 0xF0, 0x77, 0x66, 0xE1,
    0x11, 0x96, 0x87, 0x00, 0xB4, 0x33, 0x22, 0xA5,
    0xA5, 0x22, 0x33, 0xB4, 0x00, 0x87, 0x96, 0x11,
    0xD2, 0x55, 0x44, 0xC3, 0x77, 0xF0, 0xE1, 0x66,
    0x66, 0xE1, 0xF0, 0x77, 0xC3, 0x44, 0x55, 0xD2,
    0xC3, 0x44, 0x55, 0xD2, 0x66, 0xE1, 0xF0, 0x77,
    0x77, 0xF0, 0xE1, 0x66, 0xD2, 0x55, 0x44, 0xC3,
    0x00, 0x87, 0x96, 0x11, 0xA5, 0x22, 0x33, 0xB4,
    0xB4, 0x33, 0x22, 0xA5, 0x11, 0x96, 0x87, 0x00
};
// clang-format on


void long_multiply(uint32_t v1, uint32_t v2, uint32_t *HI, uint32_t *LO)
{
    register long a, b, c, d;
    register long x, y;

    a = (v1 >> 16) & 0xffff;
    b = v1 & 0xffff;
    c = (v2 >> 16) & 0xffff;
    d = v2 & 0xffff;

    *LO = b * d;
    x = a * d + c * b;
    y = ((*LO >> 16) & 0xffff) + x;

    *LO = (*LO & 0xffff) | ((y & 0xffff) << 16);
    *HI = (y >> 16) & 0xffff;

    *HI += a * c;
}


int mcfat_chrpos(const char *str, int chr)
{
    const char *p;

    p = str;
    if (*str) {
        do {
            if (*p == (chr & 0xff))
                break;
            p++;
        } while (*p);
    }
    if (*p != (chr & 0xff))
        return -1;

    return p - str;
}

void McStart()
{
    mcfat_initcache();
    McDetectCard();
    mcfat_setdevspec();
}


int McGetFormat()
{
    DPRINTF("McGetFormat \n");
    return mcfat_devinfos.cardform;
}


int McGetMcType()
{
    DPRINTF("McGetMcType \n");
    return mcfat_devinfos.cardtype;
}

int McGetFreeClusters()
{
    register int r, mcfree;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    DPRINTF("McGetFreeClusters \n");

    mcfree = 0;
    if (mcdi->cardform > 0)	{
        mcfree = mcfat_findfree2(0);
        
        if (mcfree == sceMcResFullDevice)
            mcfree = 0;

        r = mcfree;
    }
    else
        return 0;

    return r;
}


void mcfat_wmemset(void *buf, int size, int value)
{
    int *p = buf;
    size = (size >> 2) - 1;

    if (size > -1) {
        do {
            *p++ = value;
        } while (--size > -1);
    }
}


int mcfat_calcEDC(void *buf, int size)
{
    register uint32_t checksum;
    uint8_t *p = (uint8_t *)buf;

    checksum = 0;

    if (size > 0) {
        register int i;

        size--;
        i = 0;
        while (size-- != -1) {
            checksum ^= p[i];
            i++;
        }
    }
    return checksum & 0xff;
}


int mcfat_checkpath(const char *str) // check that a string do not contain special chars ( chr<32, ?, *)
{
    register int i;
    uint8_t *p = (uint8_t *)str;

    i = 0;
    while (p[i]) {
        if (((p[i] & 0xff) == '?') || ((p[i] & 0xff) == '*'))
            return 0;
        if ((p[i] & 0xff) < 32)
            return 0;
        i++;
    }
    return 1;
}


int mcfat_checkdirpath(const char *str1, const char *str2)
{
    register int pos;
    const char *p1 = str1;
    const char *p2 = str2;

    do {
        register int pos1, pos2;
        pos1 = mcfat_chrpos(p2, '?');
        pos2 = mcfat_chrpos(p2, '*');

        if ((pos1 < 0) && (pos2 < 0)) {
            if (!strcmp(p2, p1))
                return 1;
            return 0;
        }
        pos = pos2;
        if (pos1 >= 0) {
            pos = pos1;
            if (pos2 >= 0) {
                pos = pos2;
                if (pos1 < pos2) {
                    pos = pos1;
                }
            }
        }
        if (strncmp(p2, p1, pos) != 0)
            return 0;

        p2 += pos;
        p1 += pos;

        while (p2[0] == '?') {
            if (p1[0] == 0)
                return 1;
            p2++;
            p1++;
        }
    } while (p2[0] != '*');

    while((p2[0] == '*') || (p2[0] == '?'))
        p2++;

    if (p2[0] != 0) {
        register int r;
        do {
            pos = mcfat_chrpos(p1, (uint8_t)p2[0]);
            p1 += pos;
            if (pos < 0)
                return 0;
            r = mcfat_checkdirpath(p1, p2);
            p1++;
        } while (r == 0);
    }
    return 1;
}


void mcfat_invhandles()
{
    register int i = 0;
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles;

    do {
        fh->status = 0;
        fh++;
    } while (++i < MAX_FDHANDLES);
}


int McCloseAll(void)
{
    register int fd = 0, rv = 0;

    do {
        if (mcfat_fdhandles[fd].status) {
            register int rc;

            rc = McClose(fd);
            if (rc < rv)
                rv = rc;
        }
        fd++;

    } while (fd < MAX_FDHANDLES);

    return rv;
}




int McDetectCard()
{
    register int r;
    register MCDevInfo *mcdi;

    DPRINTF("McDetectCard2 port \n");

    mcdi = (MCDevInfo *)&mcfat_devinfos;

    r = mcfat_probePS2Card();
    if (r >= -9) {
        mcdi->cardtype = sceMcTypePS2;
        return r;
    }

    mcdi->cardtype = 0;
    mcdi->cardform = 0;
    mcfat_invhandles();
    mcfat_clearcache();

    return r;
}


int McOpen( const char *filename, int flag)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    flag &= ~0x00002000; // disables FRCOM flag OR what is it

    r = mcfat_open2(filename, flag);

    if (r < -9)	{
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}

int McClose(int fd)
{
    register MC_FHANDLE *fh;
    register int r;

    if (!((uint32_t)fd < MAX_FDHANDLES))
        return sceMcResDeniedPermit;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    if (!fh->status)
        return sceMcResDeniedPermit;

    fh->status = 0;
    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = McFlushCache();
    if (r < -9)	{
        mcfat_invhandles();
        mcfat_clearcache();
    }

    if (r != sceMcResSucceed)
        return r;

    if (fh->unknown2 != 0) {
        register MCDevInfo *mcdi;

        fh->unknown2 = 0;
        mcdi = (MCDevInfo *)&mcfat_devinfos;
        r = mcfat_close2(fd);

        if (r < -9)	{
            mcfat_invhandles();
            mcfat_clearcache();
        }

        if (r != sceMcResSucceed)
            return r;
    }

    r = McFlushCache();
    if (r < -9)	{
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McFlush(int fd)
{
    register int r;
    register MC_FHANDLE *fh;

    if (!((uint32_t)fd < MAX_FDHANDLES))
        return sceMcResDeniedPermit;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    if (!fh->status)
        return sceMcResDeniedPermit;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = McFlushCache();
    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    if (r != sceMcResSucceed) {
        fh->status = 0;
        return r;
    }

    if (fh->unknown2 != 0) {
        register MCDevInfo *mcdi;

        fh->unknown2 = 0;
        mcdi = (MCDevInfo *)&mcfat_devinfos;
        r = mcfat_close2(fd);

        if (r < -9)	{
            mcfat_invhandles();
            mcfat_clearcache();
        }

        if (r != sceMcResSucceed) {
            fh->status = 0;
            return r;
        }
    }

    r = McFlushCache();
    if (r < 0)
        fh->status = 0;

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McSeek(int fd, int offset, int origin)
{
    register int r;
    register MC_FHANDLE *fh;

    DPRINTF("McSeek fd %d offset %d origin %d\n", fd, offset, origin);

    if (!((uint32_t)fd < MAX_FDHANDLES))
        return sceMcResDeniedPermit;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    if (!fh->status)
        return sceMcResDeniedPermit;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    switch (origin) {
        default:
        case SEEK_CUR:
            r = fh->position + offset;
            break;
        case SEEK_SET:
            r = offset;
            break;
        case SEEK_END:
            r = fh->filesize + offset;
            break;
    }

    return fh->position = (r < 0) ? 0 : r;
}


int McRead(int fd, void *buf, int length)
{
    register int r;
    register MC_FHANDLE *fh;

    if (!((uint32_t)fd < MAX_FDHANDLES))
        return sceMcResDeniedPermit;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    if (!fh->status)
        return sceMcResDeniedPermit;

    if (!fh->rdflag)
        return sceMcResDeniedPermit;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_read2(fd, buf, length);

    if (r < 0)
        fh->status = 0;

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McWrite(int fd, void *buf, int length)
{
    register int r;
    register MC_FHANDLE *fh;

    if (!((uint32_t)fd < MAX_FDHANDLES))
        return sceMcResDeniedPermit;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    if (!fh->status)
        return sceMcResDeniedPermit;

    if (!fh->wrflag)
        return sceMcResDeniedPermit;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_write2(fd, buf, length);

    if (r < 0)
        fh->status = 0;

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McGetEntSpace( const char *dirname)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_getentspace(dirname);

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McGetDir( const char *dirname, int flags, int maxent, sceMcTblGetDir *info)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_getdir2(dirname, flags & 0xFFFF, maxent, info);

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int mcfat_dread(int fd, mcfat_dirent_t *dirent)
{
    register int r;
    register MC_FHANDLE *fh;

    if (!((uint32_t)fd < MAX_FDHANDLES))
        return sceMcResDeniedPermit;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    if (!fh->status)
        return sceMcResDeniedPermit;

    if (!fh->drdflag)
        return sceMcResDeniedPermit;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_dread2(fd, dirent);

    if (r < 0)
        fh->status = 0;

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int mcfat_getstat( const char *filename, mcfat_stat_t *stat)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_getstat2(filename, stat);

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McSetFileInfo( const char *filename, sceMcTblGetDir *info, int flags)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_setinfo2(filename, info, flags);

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    if (r == sceMcResSucceed) {
        r = McFlushCache();
        if (r < -9) {
            mcfat_invhandles();
            mcfat_clearcache();
        }
    }

    return r;
}


int McChDir( const char *newdir, char *currentdir)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_chdir(newdir, currentdir);

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McDelete( const char *filename, int flags)
{
    register int r;

    r = McDetectCard();
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_delete2(filename, flags);

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McFormat()
{
    register int r;

    mcfat_invhandles();

    r = McDetectCard();
    if (r < -2)
        return r;

    mcfat_clearcache();

    r = mcfat_format2();

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int McUnformat()
{
    register int r;

    mcfat_invhandles();

    r = McDetectCard();
    if (r < -2)
        return r;

    mcfat_clearcache();

    r = mcfat_unformat2();

    mcfat_devinfos.cardform = 0;

    if (r < -9) {
        mcfat_invhandles();
        mcfat_clearcache();
    }

    return r;
}


int mcfat_getmcrtime(sceMcStDateTime *tm)
{

    time_t rawtime;
    struct tm timeinfo;
    time(&rawtime);
    // Convert to JST
    rawtime += (-9 * 60 * 60);
#ifdef _WIN32
    gmtime_s(&timeinfo, &rawtime);
#else
    gmtime_r(&rawtime, &timeinfo);
#endif

    tm->Sec = timeinfo.tm_sec;
    tm->Min = timeinfo.tm_min;
    tm->Hour = timeinfo.tm_hour;
    tm->Day = timeinfo.tm_mday;
    tm->Month = timeinfo.tm_mon + 1;
    tm->Year = timeinfo.tm_year + 1900;

    return 0;
}


int McEraseBlock( int block, void **pagebuf, void *eccbuf)
{
    return mcfat_eraseblock(block, (void **)pagebuf, eccbuf);
}


int McEraseBlock2( int block, void **pagebuf, void *eccbuf)
{
    return mcfat_eraseblock(block, (void **)pagebuf, eccbuf);
}


int McReadPage( int page, void *buf)
{
    register int r, index, ecres, retries, count, erase_byte;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    uint8_t eccbuf[32];
    uint8_t *pdata, *peccb;

    count = (mcdi->pagesize + 127) >> 7;
    erase_byte = (mcdi->cardflags & CF_ERASE_ZEROES) ? 0x0 : 0xFF;

    retries = 0;
    ecres = sceMcResSucceed;
    do {
        if (!mcfat_readpage(page, buf, eccbuf)) {
            if (mcdi->cardflags & CF_USE_ECC) { // checking ECC from spare data block
                   // check for erased page (last byte of spare data set to 0xFF or 0x0)/
                if (eccbuf[mcfat_sparesize() - 1] == erase_byte)
                    break;

                index = 0;

                if (count > 0) {
                     peccb = (uint8_t *)eccbuf;
                     pdata = (uint8_t *)buf;

                     do {
                          r = mcfat_correctdata(pdata, peccb);
                          if (r < ecres)
                              ecres = r;

                          peccb += 3;
                          pdata += 128;
                    } while (++index < count);
                }

                if (ecres == sceMcResSucceed)
                    break;

                if ((retries == 4) && (!(ecres < sceMcResNoFormat)))
                    break;
            }
            else
            {
                break;
            }
        }
    } while (++retries < 5);

    if (retries < 5)
        return sceMcResSucceed;

    return (ecres != sceMcResSucceed) ? sceMcResNoFormat : sceMcResChangedCard;
}


void McDataChecksum(void *buf, void *ecc)
{
    register uint8_t *p, *p_ecc;
    register int i, a2, a3, t0;

    p = buf;
    i = 0;
    a2 = 0;
    a3 = 0;
    t0 = 0;

    do {
        register int v;

        v = mcfat_xortable[*p++];
        a2 ^= v;
        if (v & 0x80) {
            a3 ^= ~i;
            t0 ^= i;
        }
    } while (++i < 0x80);

    p_ecc = ecc;
    p_ecc[0] = ~a2 & 0x77;
    p_ecc[1] = ~a3 & 0x7F;
    p_ecc[2] = ~t0 & 0x7F;
}



int mcfat_correctdata(void *buf, void *ecc)
{
    register int xor0, xor1, xor2, xor3, xor4;
    uint8_t eccbuf[12];
    uint8_t *p = (uint8_t *)ecc;

    McDataChecksum(buf, eccbuf);

    xor0 = p[0] ^ eccbuf[0];
    xor1 = p[1] ^ eccbuf[1];
    xor2 = p[2] ^ eccbuf[2];

    xor3 = xor1 ^ xor2;
    xor4 = (xor0 & 0xf) ^ (xor0 >> 4);

    if (!xor0 && !xor1 && !xor2)
        return 0;

    if ((xor3 == 0x7f) && (xor4 == 0x7)) {
        p[xor2] ^= 1 << (xor0 >> 4);
        return -1;
    }

    xor0 = 0;
    xor2 = 7;
    do {
        if ((xor3 & 1))
            xor0++;
        xor2--;
        xor3 = xor3 >> 1;
    } while (xor2 >= 0);

    xor2 = 3;
    do {
        if ((xor4 & 1))
            xor0++;
        xor2--;
        xor4 = xor4 >> 1;
    } while (xor2 >= 0);

    if (xor0 == 1)
        return -2;

    return -3;
}


int mcfat_sparesize()
{ // Get ps2 mc spare size by dividing pagesize / 32
    return (mcfat_devinfos.pagesize + 0x1F) >> 5;
}


int mcfat_setdevspec()
{
    int cardsize;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    DPRINTF("mcfat_setdevspec\n");

    if (McGetCardSpec(&mcdi->pagesize, &mcdi->blocksize, &cardsize, &mcdi->cardflags) != sceMcResSucceed)
        return sceMcResFullDevice;

    mcdi->pages_per_cluster = MCMAN_CLUSTERSIZE / mcdi->pagesize;
    mcdi->cluster_size = MCMAN_CLUSTERSIZE;
    mcdi->unknown1 = 0;
    mcdi->unknown2 = 0;
    mcdi->unused = 0xff00;
    mcdi->FATentries_per_cluster = MCMAN_CLUSTERFATENTRIES;
    mcdi->unknown5 = -1;
    mcdi->rootdir_cluster2 = mcdi->rootdir_cluster;
    mcdi->clusters_per_block = mcdi->blocksize / mcdi->pages_per_cluster;
    mcdi->clusters_per_card = (cardsize / mcdi->blocksize) * (mcdi->blocksize / mcdi->pages_per_cluster);

    return sceMcResSucceed;
}


int mcfat_setdevinfos()
{
    register int r, allocatable_clusters_per_card, iscluster_valid, current_allocatable_cluster, cluster_cnt;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McFsEntry *pfse;

    DPRINTF("mcfat_setdevinfos \n");

    mcfat_wmemset((void *)mcdi, sizeof(MCDevInfo), 0);

    mcdi->cardform = 0;

    r = mcfat_setdevspec();
    if (r != sceMcResSucceed)
        return -49;

    mcdi->cardform = -1;

    r = McReadPage(0, &mcfat_pagebuf);
    
    if (r == sceMcResNoFormat)
        return sceMcResNoFormat; // should rebuild a valid superblock here
    if (r != sceMcResSucceed)
        return -48;

    if (strncmp(SUPERBLOCK_MAGIC, mcfat_pagebuf.magic, 28) != 0) {
        DPRINTF("mcfat_setdevinfos No card format !!!\n");
        return sceMcResNoFormat;
    }

    if (((mcfat_pagebuf.byte[28] - 48) == 1) && ((mcfat_pagebuf.byte[30] - 48) == 0))  // check ver major & minor
        return sceMcResNoFormat;

    uint8_t *p = (uint8_t *)mcdi;
    for (r=0; r<0x150; r++)
        p[r] = mcfat_pagebuf.byte[r];

    mcdi->cardtype = sceMcTypePS2; // <--

    r = mcfat_checkBackupBlocks();
    if (r != sceMcResSucceed)
        return -47;

    r = McReadDirEntry(0, 0, &pfse);
    if (r != sceMcResSucceed)
        return sceMcResNoFormat; // -46

    if (strcmp(pfse->name, ".") != 0)
        return sceMcResNoFormat;

    if (McReadDirEntry(0, 1, &pfse) != sceMcResSucceed)
        return -45;

    if (strcmp(pfse->name, "..") != 0)
        return sceMcResNoFormat;

    mcdi->cardform = 1;
//	mcdi->cardtype = sceMcTypePS2;

    if (((mcfat_pagebuf.byte[28] - 48) == 1) && ((mcfat_pagebuf.byte[30] - 48) == 1)) {  // check ver major & minor
        if ((mcdi->clusters_per_block * mcdi->backup_block2) == mcdi->alloc_end)
            mcdi->alloc_end = (mcdi->clusters_per_block * mcdi->backup_block2) - mcdi->alloc_offset;
    }

    uint32_t hi, lo, temp;

    long_multiply(mcdi->clusters_per_card, 0x10624dd3, &hi, &lo);
    temp = (hi >> 6) - (mcdi->clusters_per_card >> 31);
    allocatable_clusters_per_card = (((((temp << 5) - temp) << 2) + temp) << 3) + 1;
    iscluster_valid = 0;
    cluster_cnt = 0;
    current_allocatable_cluster = mcdi->alloc_offset;

    while (cluster_cnt < allocatable_clusters_per_card) {
        if ((uint32_t)current_allocatable_cluster >= mcdi->clusters_per_card)
            break;

        if (((current_allocatable_cluster % mcdi->clusters_per_block) == 0) \
            || (mcdi->alloc_offset == (uint32_t)current_allocatable_cluster)) {
            iscluster_valid = 1;
            for (r=0; r<16; r++) {
                if (current_allocatable_cluster / mcdi->clusters_per_block == (uint32_t)(mcdi->bad_block_list[r]))
                    iscluster_valid = 0;
            }
        }
        if (iscluster_valid == 1)
            cluster_cnt++;
        current_allocatable_cluster++;
    }

    mcdi->max_allocatable_clusters = current_allocatable_cluster - mcdi->alloc_offset;

    return sceMcResSucceed;
}


int mcfat_reportBadBlocks()
{
    register int bad_blocks, erase_byte, err_limit;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    DPRINTF("mcfat_reportBadBlocks\n");

    mcfat_wmemset((void *)mcdi->bad_block_list, 128, -1);

    if ((mcdi->cardflags & CF_BAD_BLOCK) == 0)
        return sceMcResSucceed;

    err_limit = ((mcdi->pagesize & 0xffff) + (mcdi->pagesize & 0x1)) >> 1; //s7

    erase_byte = 0;
    if ((mcdi->cardflags & CF_ERASE_ZEROES) != 0)
        erase_byte = 0xff;

    bad_blocks = 0; // s2

    if ((mcdi->clusters_per_card / mcdi->clusters_per_block) > 0) {
        register int block;

        block = 0; // s1

        do {
            register int page, err_cnt;

            if (bad_blocks >= 16)
                break;

            err_cnt = 0; 	//s4
            page = 0; //s3
            do {
                register int r;

                r = McReadPage((block * mcdi->blocksize) + page, &mcfat_pagebuf);
                if (r == sceMcResNoFormat) {
                    mcdi->bad_block_list[bad_blocks] = block;
                    bad_blocks++;
                    break;
                }
                if (r != sceMcResSucceed)
                    return r;

                if ((mcdi->cardflags & CF_USE_ECC) == 0) {
                    register int i;
                    uint8_t *p;

                    p = mcfat_pagebuf.byte;
                    for (i = 0; i < mcdi->pagesize; i++) {
                        // check if the content of page is clean
                        if (*p++ != erase_byte)
                            err_cnt++;

                        if (err_cnt >= err_limit) {
                            mcdi->bad_block_list[bad_blocks] = block;
                            bad_blocks++;
                            break;
                        }
                    }
                }
            } while (++page < 2);

        } while ((uint32_t)(++block) < (mcdi->clusters_per_card / mcdi->clusters_per_block));
    }

    return sceMcResSucceed;
}


int McCreateDirentry( int parent_cluster, int num_entries, int cluster, const sceMcStDateTime *ctime)
{
    register int r;
    McCacheEntry *mce;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McFsEntry *mfe, *mfe_next, *pfse;

    DPRINTF("McCreateDirentry  parent_cluster %x num_entries %d cluster %x\n",parent_cluster, num_entries, cluster);

    r = McReadCluster(mcdi->alloc_offset + cluster, &mce);
    if (r != sceMcResSucceed)
        return r;

    mcfat_wmemset(mce->cl_data, MCMAN_CLUSTERSIZE, 0);

    mfe = (McFsEntry*)mce->cl_data;
    mfe_next = (McFsEntry*)(mce->cl_data + sizeof (McFsEntry));

    mfe->mode = sceMcFileAttrReadable | sceMcFileAttrWriteable | sceMcFileAttrExecutable \
              | sceMcFileAttrSubdir | sceMcFile0400 | sceMcFileAttrExists; // 0x8427

    if (ctime == NULL)
        mcfat_getmcrtime(&mfe->created);
    else
        mfe->created = *ctime;

    mfe->modified =	mfe->created;

    mfe->length = 0;
    mfe->dir_entry = num_entries;
    mfe->cluster = parent_cluster;
    mfe->name[0] = '.';
    mfe->name[1] = '\0';

    if ((parent_cluster == 0) && (num_entries == 0)) {
        // entry is root directory
        mfe_next->created = mfe->created;
        mfe->length = 2;
        mfe++;

        mfe->mode = sceMcFileAttrWriteable | sceMcFileAttrExecutable | sceMcFileAttrSubdir \
                  | sceMcFile0400 | sceMcFileAttrExists | sceMcFileAttrHidden; // 0xa426
        mfe->dir_entry = 0;
        mfe->cluster = 0;
    }
    else {
        // entry is normal "." / ".."
        McReadDirEntry(parent_cluster, 0, &pfse);

        mfe_next->created = pfse->created;
        mfe++;

        mfe->mode = sceMcFileAttrReadable | sceMcFileAttrWriteable | sceMcFileAttrExecutable \
                  | sceMcFileAttrSubdir | sceMcFile0400 | sceMcFileAttrExists; // 0x8427
        mfe->dir_entry = pfse->dir_entry;

        mfe->cluster = pfse->cluster;
    }

    mfe->modified = mfe->created;
    mfe->length = 0;

    mfe->name[0] = '.';
    mfe->name[1] = '.';
    mfe->name[2] = '\0';

    mce->wr_flag = 1;

    return sceMcResSucceed;
}


int mcfat_fatRseek(int fd)
{
    register int entries_to_read, fat_index;
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles[fd]; //s1
    register MCDevInfo *mcdi = &mcfat_devinfos;	//s2
    int fat_entry;

    entries_to_read = fh->position / mcdi->cluster_size; //s0

    //s5 = 0

    if ((uint32_t)entries_to_read < fh->clust_offset) //v1 = fh->fh->clust_offset
        fat_index = fh->freeclink;
    else {
        fat_index = fh->clink; // a2
        entries_to_read -= fh->clust_offset;
    }

    if (entries_to_read == 0) {
        if (fat_index >= 0)
            return fat_index + mcdi->alloc_offset;

        return sceMcResFullDevice;
    }

    do {
        register int r;

        r = McGetFATentry(fat_index, &fat_entry);
        if (r != sceMcResSucceed)
            return r;

        fat_index = fat_entry;

        if (fat_index >= -1)
            return sceMcResFullDevice;

        entries_to_read--;

        fat_index &= ~0x80000000;
        fh->clink = fat_index;
        fh->clust_offset = (fh->position / mcdi->cluster_size) - entries_to_read;

    } while (entries_to_read > 0);

    return fat_index + mcdi->alloc_offset;
}


int mcfat_fatWseek(int fd) // modify FAT to hold new content for a file
{
    register int r, entries_to_write, fat_index;
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    register MCDevInfo *mcdi = &mcfat_devinfos;
    register McCacheEntry *mce;
    int fat_entry;

    entries_to_write = fh->position / mcdi->cluster_size;

    if ((fh->clust_offset == 0) || ((uint32_t)entries_to_write < fh->clust_offset)) {
        fat_index = fh->freeclink;

        if (fat_index < 0) {
            fat_index = mcfat_findfree2(1);

            if (fat_index < 0)
                return sceMcResFullDevice;

            mce = (McCacheEntry *)mcfat_get1stcacheEntp();
            fh->freeclink = fat_index;

            r = mcfat_close2(fd);
            if (r != sceMcResSucceed)
                return r;

            mcfat_addcacheentry(mce);
            McFlushCache();
        }
    }
    else {
        fat_index = fh->clink;
        entries_to_write -= fh->clust_offset;
    }

    if (entries_to_write != 0) {
        do {
            r = McGetFATentry(fat_index, &fat_entry);
            if (r != sceMcResSucceed)
                return r;

            if ((unsigned int)fat_entry >= 0xffffffff) {
                r = mcfat_findfree2(1);
                if (r < 0)
                        return r;
                fat_entry = r;
                fat_entry |= 0x80000000;

                mce = (McCacheEntry *)mcfat_get1stcacheEntp();

                r = McSetFATentry(fat_index, fat_entry);
                if (r != sceMcResSucceed)
                    return r;

                mcfat_addcacheentry(mce);
            }

            entries_to_write--;
            fat_index = fat_entry & ~0x80000000;
        } while (entries_to_write > 0);
    }

    fh->clink = fat_index;
    fh->clust_offset = fh->position / mcdi->cluster_size;

    return sceMcResSucceed;
}


int mcfat_findfree2( int reserve)
{
    register int r, rfree, ifc_index, indirect_offset, fat_index, block;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce1, *mce2;

    DPRINTF("mcfat_findfree2  reserve%d\n",reserve);

    rfree = 0;

    for (fat_index = mcdi->unknown2; (uint32_t)fat_index < mcdi->max_allocatable_clusters; fat_index++) {
        register int indirect_index, fat_offset;

        indirect_index = fat_index / mcdi->FATentries_per_cluster;
        fat_offset = fat_index % mcdi->FATentries_per_cluster;

        if ((fat_offset == 0) || ((uint32_t)fat_index == mcdi->unknown2)) {

            ifc_index = indirect_index / mcdi->FATentries_per_cluster;
            r = McReadCluster(mcdi->ifc_list[ifc_index], &mce1);
            if (r != sceMcResSucceed)
                return r;
        //}
        //if ((fat_offset == 0) || (fat_index == mcdi->unknown2)) {
            indirect_offset = indirect_index % mcdi->FATentries_per_cluster;
            McFatCluster *fc = (McFatCluster *)mce1->cl_data;
            r = McReadCluster(fc->entry[indirect_offset], &mce2);
            if (r != sceMcResSucceed)
                return r;
        }

        McFatCluster *fc = (McFatCluster *)mce2->cl_data;

        if (fc->entry[fat_offset] >= 0) {
            block = (mcdi->alloc_offset + fat_offset) / mcdi->clusters_per_block;
            if (block != mcfat_badblock) {
                if (reserve) {
                    fc->entry[fat_offset] = 0xffffffff;
                    mce2->wr_flag = 1;
                    mcdi->unknown2 = fat_index;
                    return fat_index;
                }
                rfree++;
            }
        }
    }

    if (reserve)
        return sceMcResFullDevice;

    return (rfree) ? rfree : sceMcResFullDevice;
}


int mcfat_getentspace( const char *dirname)
{
    register int r, i, entspace;
    McCacheDir cacheDir;
    McFsEntry *fse;
    McFsEntry mfe;
    uint8_t *pfsentry, *pmfe, *pfseend;

    DPRINTF("mcfat_getentspace  dirname %s\n",dirname);

    r = mcfat_cachedirentry(dirname, &cacheDir, &fse, 1);
    if (r > 0)
        return sceMcResNoEntry;
    if (r < 0)
        return r;

    pfsentry = (uint8_t *)fse;
    pmfe = (uint8_t *)&mfe;
    pfseend = (uint8_t *)(pfsentry + sizeof (McFsEntry));

    do {
        *((uint32_t *)pmfe  ) = *((uint32_t *)pfsentry  );
        *((uint32_t *)pmfe+1) = *((uint32_t *)pfsentry+1);
        *((uint32_t *)pmfe+2) = *((uint32_t *)pfsentry+2);
        *((uint32_t *)pmfe+3) = *((uint32_t *)pfsentry+3);
        pfsentry += 16;
        pmfe += 16;
    } while (pfsentry < pfseend);

    entspace = mfe.length & 1;

    for (i = 0; (uint32_t)i < mfe.length; i++) {

        r = McReadDirEntry(mfe.cluster, i, &fse);
        if (r != sceMcResSucceed)
            return r;
        if ((fse->mode & sceMcFileAttrExists) == 0)
            entspace++;
    }

    return entspace;
}


int mcfat_cachedirentry( const char *filename, McCacheDir *pcacheDir, McFsEntry **pfse, int unknown_flag)
{
    register int r, fsindex, cluster, fmode;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McFsEntry *fse;
    McCacheDir cacheDir;
    uint8_t *pfsentry, *pcache, *pfseend;
    const char *p;

    DPRINTF("mcfat_cachedirentry  name %s\n",filename);

    if (pcacheDir == NULL) {
        pcacheDir = &cacheDir;
        pcacheDir->maxent = -1;
    }

    p = filename;
    if (*p == '/') {
        p++;
        cluster = 0;
        fsindex = 0;
    }
    else {
        cluster = mcdi->rootdir_cluster2;
        fsindex = mcdi->unknown1;
    }

    r = McReadDirEntry(cluster, fsindex, &fse);
    if (r != sceMcResSucceed)
        return r;

    if (*p == 0) {
        if (!(fse->mode & sceMcFileAttrExists))
            return 2;

        if (pcacheDir == NULL) {
            *pfse = (McFsEntry *)fse;
            return sceMcResSucceed;
        }

        pfsentry = (uint8_t *)fse;
        pcache = (uint8_t *)&mcfat_dircache[0];
        pfseend = (uint8_t *)(pfsentry + sizeof (McFsEntry));

        do {
            *((uint32_t *)pcache  ) = *((uint32_t *)pfsentry  );
            *((uint32_t *)pcache+1) = *((uint32_t *)pfsentry+1);
            *((uint32_t *)pcache+2) = *((uint32_t *)pfsentry+2);
            *((uint32_t *)pcache+3) = *((uint32_t *)pfsentry+3);
            pfsentry += 16;
            pcache += 16;
        } while (pfsentry < pfseend);

        r = mcfat_getdirinfo((McFsEntry *)&mcfat_dircache[0], ".", pcacheDir, unknown_flag);

        McReadDirEntry(pcacheDir->cluster, pcacheDir->fsindex, pfse);

        if (r > 0)
            return 2;

        return r;

    } else {

        do {
            fmode = sceMcFileAttrReadable | sceMcFileAttrExecutable;
            if ((fse->mode & fmode) != fmode)
                return sceMcResDeniedPermit;

            pfsentry = (uint8_t *)fse;
            pcache = (uint8_t *)&mcfat_dircache[0];
            pfseend = (uint8_t *)(pfsentry + sizeof(McFsEntry));

            do {
                *((uint32_t *)pcache  ) = *((uint32_t *)pfsentry  );
                *((uint32_t *)pcache+1) = *((uint32_t *)pfsentry+1);
                *((uint32_t *)pcache+2) = *((uint32_t *)pfsentry+2);
                *((uint32_t *)pcache+3) = *((uint32_t *)pfsentry+3);
                pfsentry += 16;
                pcache += 16;
            } while (pfsentry < pfseend);

            r = mcfat_getdirinfo((McFsEntry *)&mcfat_dircache[0], p, pcacheDir, unknown_flag);

            if (r > 0) {
                if (mcfat_chrpos(p, '/') >= 0)
                    return 2;

                pcacheDir->cluster = cluster;
                pcacheDir->fsindex = fsindex;

                return 1;
            }

            r = mcfat_chrpos(p, '/');
            if ((r >= 0) && (p[r + 1] != 0)) {
                p += mcfat_chrpos(p, '/') + 1;
                cluster = pcacheDir->cluster;
                fsindex = pcacheDir->fsindex;

                McReadDirEntry(cluster, fsindex, &fse);
            }
            else {
                McReadDirEntry(pcacheDir->cluster, pcacheDir->fsindex, pfse);

                return sceMcResSucceed;
            }
        } while (*p != 0);
    }
    return sceMcResSucceed;
}


int mcfat_getdirinfo( McFsEntry *pfse, const char *filename, McCacheDir *pcd, int unknown_flag)
{
    register int i, r, ret, len, pos;
    McFsEntry *fse;
    uint8_t *pfsentry, *pfsee, *pfseend;

    DPRINTF("mcfat_getdirinfo  name %s\n",filename);

    pos = mcfat_chrpos(filename, '/');
    if (pos < 0)
        pos = strlen(filename);

    ret = 0;
    if ((pos == 2) && (!strncmp(filename, "..", 2))) {

        r = McReadDirEntry(pfse->cluster, 0, &fse);
        if (r != sceMcResSucceed)
            return r;

        r = McReadDirEntry(fse->cluster, 0, &fse);
        if (r != sceMcResSucceed)
            return r;

        if (pcd) {
            pcd->cluster = fse->cluster;
            pcd->fsindex = fse->dir_entry;
        }

        r = McReadDirEntry(fse->cluster, fse->dir_entry, &fse);
        if (r != sceMcResSucceed)
            return r;

        pfsentry = (uint8_t *)fse;
        pfsee = (uint8_t *)pfse;
        pfseend = (uint8_t *)(pfsentry + sizeof(McFsEntry));

        do {
            *((uint32_t *)pfsee  ) = *((uint32_t *)pfsentry  );
            *((uint32_t *)pfsee+1) = *((uint32_t *)pfsentry+1);
            *((uint32_t *)pfsee+2) = *((uint32_t *)pfsentry+2);
            *((uint32_t *)pfsee+3) = *((uint32_t *)pfsentry+3);
            pfsentry += 16;
            pfsee += 16;
        } while (pfsentry < pfseend);

        if ((fse->mode & sceMcFileAttrHidden) != 0) {
            ret = 2;
            if ((pcd == NULL) || (pcd->maxent < 0))
                return 3;
        }

        if ((pcd == NULL) || (pcd->maxent < 0))
            return sceMcResSucceed;
    }
    else {
        if ((pos == 1) && (!strncmp(filename, ".", 1))) {

            r = McReadDirEntry(pfse->cluster, 0, &fse);
            if (r != sceMcResSucceed)
                return r;

            if (pcd) {
                pcd->cluster = fse->cluster;
                pcd->fsindex = fse->dir_entry;
            }

            if ((fse->mode & sceMcFileAttrHidden) != 0) {
                ret = 2;
                if ((pcd == NULL) || (pcd->maxent < 0))
                    return 3;
                else {
                    if ((pcd == NULL) || (pcd->maxent < 0))
                        return sceMcResSucceed;
                }
            }
            else {
                ret = 1;
                if ((pcd == NULL) || (pcd->maxent < 0))
                    return sceMcResSucceed;
            }
        }
    }

    if ((pcd) && (pcd->maxent >= 0))
        pcd->maxent = pfse->length;

    if (pfse->length > 0) {

        i = 0;
        do {
            r = McReadDirEntry(pfse->cluster, i, &fse);
            if (r != sceMcResSucceed)
                return r;

            if (((fse->mode & sceMcFileAttrExists) == 0) && (pcd) && (i < pcd->maxent))
                 pcd->maxent = i;

            if (unknown_flag) {
                if ((fse->mode & sceMcFileAttrExists) == 0)
                    continue;
            }
            else {
                if ((fse->mode & sceMcFileAttrExists) != 0)
                    continue;
            }

            if (ret != 0)
                continue;

            if ((pos >= 11) && (!strncmp(&filename[10], &fse->name[10], pos-10))) {
                len = pos;
                if (strlen(fse->name) >= (unsigned int)pos)
                    len = strlen(fse->name);

                if (!strncmp(filename, fse->name, len))
                    goto continue_check;
            }

            if (strlen(fse->name) >= (unsigned int)pos)
                len = strlen(fse->name);
            else
                len = pos;

            if (strncmp(filename, fse->name, len))
                continue;

continue_check:
            ret = 1;

            if ((fse->mode & sceMcFileAttrHidden) != 0) {
                ret = 2;
            }

            if (pcd == NULL)
                break;

            pcd->fsindex = i;
            pcd->cluster = pfse->cluster;

            if (pcd->maxent < 0)
                break;

        } while ((uint32_t)(++i) < pfse->length);
    }

    if (ret == 2)
        return 2;

    return ((ret < 1) ? 1 : 0);
}


int mcfat_writecluster( int cluster, int flag)
{
    register int i, block;
    register uint32_t erase_value;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    block = cluster / mcdi->clusters_per_block;

    if (mcfat_wr_block == block)
        return mcfat_wr_flag3;

    mcfat_wr_block = block;
    mcfat_wr_flag3 = -9;

    for (i = 0; i < 16; i++) { // check only 16 bad blocks ?
        if (mcdi->bad_block_list[i] < 0)
            break;
        if (mcdi->bad_block_list[i] == block) {
            mcfat_wr_flag3 = 0;
            return sceMcResSucceed;
        }
    }

    if (flag) {
        register int r, j, page, pageword_cnt;

        for (i = 1; i < mcdi->blocksize; i++)
            mcfat_pagedata[i] = 0;

        mcfat_pagedata[0] = mcfat_pagebuf.byte;

        pageword_cnt = mcdi->pagesize >> 2;
        page = block * mcdi->blocksize;

        if (mcdi->cardflags & CF_ERASE_ZEROES)
            erase_value = 0xffffffff;
        else
            erase_value = 0x00000000;

        for (i = 0; i < pageword_cnt; i++)
            mcfat_pagebuf.word[i] = erase_value;

        r = mcfat_eraseblock(block, (void **)mcfat_pagedata, (void *)mcfat_eccdata);
        if (r == sceMcResFailReplace)
            return sceMcResSucceed;
        if (r != sceMcResSucceed)
            return sceMcResChangedCard;

        for (i = 1; i < mcdi->blocksize; i++) {
            r = McWritePage(page + i, &mcfat_pagebuf, mcfat_eccdata);
            if (r == sceMcResFailReplace)
                return sceMcResSucceed;
            if (r != sceMcResSucceed)
                return sceMcResNoFormat;
        }

        for (i = 1; i < mcdi->blocksize; i++) {
            r = McReadPage(page + i, &mcfat_pagebuf);
            if (r == sceMcResNoFormat)
                return sceMcResSucceed;
            if (r != sceMcResSucceed)
                return sceMcResFullDevice;

            for (j = 0; j < pageword_cnt; j++) {
                if (mcfat_pagebuf.word[j] != erase_value) {
                    mcfat_wr_flag3 = 0;
                    return sceMcResSucceed;
                }
            }
        }

        r = mcfat_eraseblock(block, NULL, NULL);
        if (r != sceMcResSucceed)
            return sceMcResChangedCard;

        r = McWritePage(page, &mcfat_pagebuf, mcfat_eccdata);
        if (r == sceMcResFailReplace)
            return sceMcResSucceed;
        if (r != sceMcResSucceed)
            return sceMcResNoFormat;

        r = McReadPage(page, &mcfat_pagebuf);
        if (r == sceMcResNoFormat)
            return sceMcResSucceed;
        if (r != sceMcResSucceed)
            return sceMcResFullDevice;

        for (j = 0; j < pageword_cnt; j++) {
            if (mcfat_pagebuf.word[j] != erase_value) {
                mcfat_wr_flag3 = 0;
                return sceMcResSucceed;
            }
        }

        r = mcfat_eraseblock(block, NULL, NULL);
        if (r == sceMcResFailReplace)
            return sceMcResSucceed;
        if (r != sceMcResSucceed)
            return sceMcResFullDevice;

        erase_value = ~erase_value;

        for (i = 0; i < mcdi->blocksize; i++) {
            r = McReadPage(page + i, &mcfat_pagebuf);
            if (r != sceMcResSucceed)
                return sceMcResDeniedPermit;

            for (j = 0; j < pageword_cnt; j++) {
                if (mcfat_pagebuf.word[j] != erase_value) {
                    mcfat_wr_flag3 = 0;
                    return sceMcResSucceed;
                }
            }
        }
    }
    mcfat_wr_flag3 = 1;

    return mcfat_wr_flag3;
}


int McSetDirEntryState( int cluster, int fsindex, int flags)
{
    register int r, i, fat_index;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McFsEntry *fse;
    int fat_entry;

    r = McReadDirEntry(cluster, fsindex, &fse);
    if (r != sceMcResSucceed)
        return r;

    if (fse->name[0] == '.') {
        if ((fse->name[1] == 0) || (fse->name[1] == '.'))
            return sceMcResNoEntry;
    }

    i = 0;
    do {
        if (mcfat_fdhandles[i].status == 0)
            continue;

        if (mcfat_fdhandles[i].field_20 != (uint32_t)cluster)
            continue;

        if (mcfat_fdhandles[i].field_24 == (uint32_t)fsindex)
            return sceMcResDeniedPermit;

    } while (++i < MAX_FDHANDLES);

    if (flags == 0)
        fse->mode = fse->mode & (sceMcFileAttrExists - 1);
    else
        fse->mode = fse->mode | sceMcFileAttrExists;

    Mc1stCacheEntSetWrFlagOff();

    fat_index = fse->cluster;

    if (fat_index >= 0) {
        if ((uint32_t)fat_index < mcdi->unknown2)
            mcdi->unknown2 = fat_index;
        mcdi->unknown5 = -1;

        do {
            r = McGetFATentry(fat_index, &fat_entry);
            if (r != sceMcResSucceed)
                return r;

            if (flags == 0)	{
                fat_entry &= ~0x80000000;
                if ((uint32_t)fat_index < mcdi->unknown2)
                    mcdi->unknown2 = fat_entry;
            }
            else
                fat_entry |= 0x80000000;

            r = McSetFATentry(fat_index, fat_entry);
            if (r != sceMcResSucceed)
                return r;

            fat_index = fat_entry & ~0x80000000;

        } while (fat_index != ~0x80000000);
    }

    return sceMcResSucceed;
}


int mcfat_checkBackupBlocks()
{
    register int r1, r2, r, eccsize;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce;
    uint32_t *pagebuf = mcfat_pagebuf.word;
    uint32_t value1, value2;

    // First check backup block2 to see if it's in erased state
    r1 = McReadPage(mcdi->backup_block2 * mcdi->blocksize, &mcfat_pagebuf); //s1

    value1 = *pagebuf; //s3
    if (((mcdi->cardflags & CF_ERASE_ZEROES) != 0) && (value1 == 0))
        value1 = 0xffffffff;
    if (value1 != 0xffffffff)
        value1 = value1 & ~0x80000000;

    r2 = McReadPage((mcdi->backup_block2 * mcdi->blocksize) + 1, &mcfat_pagebuf); //a0

    value2 = *pagebuf; //s0
    if (((mcdi->cardflags & CF_ERASE_ZEROES) != 0) && (value2 == 0))
        value2 = 0xffffffff;
    if (value2 != 0xffffffff)
        value2 = value2 & ~0x80000000;

    if ((value1 != 0xffffffff) && (value2 == 0xffffffff))
        goto check_done;
    if ((r1 < 0) || (r2 < 0))
        goto check_done;

    if ((value1 == 0xffffffff) && (value1 == value2))
        return sceMcResSucceed;

    // bachup block2 is not erased, so programming is assumed to have not been completed
    // reads content of backup block1
    for (r1 = 0; (uint32_t)r1 < mcdi->clusters_per_block; r1++) {

        McReadCluster((mcdi->backup_block1 * mcdi->clusters_per_block) + r1, &mce);
        mce->rd_flag = 1;

        for (r2 = 0; r2 < mcdi->pages_per_cluster; r2++) {
            mcfat_pagedata[(r1 * ((mcdi->pages_per_cluster << 16) >> 16)) + r2] = \
                (void *)(mce->cl_data + (r2 * mcdi->pagesize));
        }
    }

    // Erase the block where data must be written
    r = mcfat_eraseblock(value1, (void **)mcfat_pagedata, (void *)mcfat_eccdata);
    if (r != sceMcResSucceed)
        return r;

    // Write the block
    for (r1 = 0; r1 < mcdi->blocksize; r1++) {

        eccsize = mcdi->pagesize;
        if (eccsize < 0)
            eccsize += 0x1f;
        eccsize = eccsize >> 5;

        r = McWritePage((value1 * ((mcdi->blocksize << 16) >> 16)) + r1, \
            mcfat_pagedata[r1], (void *)(mcfat_eccdata + (eccsize * r1)));

        if (r != sceMcResSucceed)
            return r;
    }

    for (r1 = 0; (uint32_t)r1 < mcdi->clusters_per_block; r1++)
        mcfat_freecluster((mcdi->backup_block1 * mcdi->clusters_per_block) + r1);

check_done:
    // Finally erase backup block2
    return mcfat_eraseblock(mcdi->backup_block2, NULL, NULL);
}


int McCheckBlock( int block)
{
    register int r, i, j, page, ecc_count, pageword_cnt, flag, erase_value;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    uint8_t *p_page, *p_ecc;

    DPRINTF("McCheckBlock  block 0x%x\n",block);

    // sp50 = block;
    page = block * mcdi->blocksize; //sp18
    pageword_cnt = mcdi->pagesize >> 2; //s6

    ecc_count = mcdi->pagesize;
    if (mcdi->pagesize < 0)
        ecc_count += 127;
    ecc_count = ecc_count >> 7; // s7

    flag = 0; // s4

    if (mcdi->cardform > 0) {
        for (i = 0; i < 16; i++) {
            if (mcdi->bad_block_list[i] <= 0)
                break;
            if (mcdi->bad_block_list[i] == block)
                goto lbl_8764;
        }
    }

    flag = 16; // s4

    for (i = 0; i < mcdi->blocksize; i++) {
        r = mcfat_readpage(page + i, &mcfat_pagebuf, mcfat_eccdata);
        if (r != sceMcResSucceed)
            return -45;

        if ((mcdi->cardflags & CF_USE_ECC) != 0) {
            if (mcfat_eccdata[mcfat_sparesize() - 1] != 0xff) {
                p_page = mcfat_pagebuf.byte; //s1
                p_ecc = (void *)mcfat_eccdata; //s2

                for (j = 0; j < ecc_count; j++) {
                    r = mcfat_correctdata(p_page, p_ecc);
                    if (r != sceMcResSucceed) {
                        flag = -1;
                        goto lbl_8764;
                    }
                    p_ecc = (void *)((uint8_t *)p_ecc + 3);
                    p_page = (void *)((uint8_t *)p_page + 128);
                }
            }
        }
    }

    for (j = 0; j < pageword_cnt; j++)
        mcfat_pagebuf.word[j] = 0x5a5aa5a5;

    for (j = 0; j < 128; j++)
        *((uint32_t *)&mcfat_eccdata + j) = 0x5a5aa5a5;

    r = mcfat_eraseblock(block, NULL, NULL);
    if (r != sceMcResSucceed) {
        r = mcfat_probePS2Card2();
        if (r != sceMcResSucceed)
            return -45;
        flag = -1;
        goto lbl_8764;
    }

    for (i = 0; i < mcdi->blocksize; i++) {
        r = McWritePage(page + i, &mcfat_pagebuf, mcfat_eccdata);
        if (r != sceMcResSucceed) {
            r = mcfat_probePS2Card2();
            if (r != sceMcResSucceed)
                return -44;
            flag = -1;
            goto lbl_8764;
        }
    }

    for (i = 0; i < mcdi->blocksize; i++) {
        r = mcfat_readpage(page + i, &mcfat_pagebuf, mcfat_eccdata);
        if (r != sceMcResSucceed)
            return -45;

        for (j = 0; j < pageword_cnt; j++) {
            if (mcfat_pagebuf.word[j] != 0x5a5aa5a5) {
                flag = -1;
                goto lbl_8764;
            }
        }

        for (j = 0; j < 128; j++) {
            if (*((uint32_t *)&mcfat_eccdata + j) != 0x5a5aa5a5) {
                flag = -1;
                goto lbl_8764;
            }
        }
    }

    for (j = 0; j < pageword_cnt; j++)
        mcfat_pagebuf.word[j] = 0x05a55a5a;

    for (j = 0; j < 128; j++)
        *((uint32_t *)&mcfat_eccdata + j) = 0x05a55a5a;

    r = mcfat_eraseblock(block, NULL, NULL);
    if (r != sceMcResSucceed) {
        r = mcfat_probePS2Card2();
        if (r != sceMcResSucceed)
            return -42;
        flag = -1;
        goto lbl_8764;
    }

    for (i = 0; i < mcdi->blocksize; i++) {
        r = McWritePage(page + i, &mcfat_pagebuf, mcfat_eccdata);
        if (r != sceMcResSucceed) {
            r = mcfat_probePS2Card2();
            if (r != sceMcResSucceed)
                return -46;
            flag = -1;
            goto lbl_8764;
        }
    }

    for (i = 0; i < mcdi->blocksize; i++) {
        r = mcfat_readpage(page + i, &mcfat_pagebuf, mcfat_eccdata);
        if (r != sceMcResSucceed)
            return -45;

        for (j = 0; j < pageword_cnt; j++) {
            if (mcfat_pagebuf.word[j] != 0x05a55a5a) {
                flag = -1;
                goto lbl_8764;
            }
        }

        for (j = 0; j < 128; j++) {
            if (*((uint32_t *)&mcfat_eccdata + j) != 0x05a55a5a) {
                flag = -1;
                goto lbl_8764;
            }
        }
    }

lbl_8764:
    if (flag == 16) {
        mcfat_eraseblock(block, NULL, NULL);
        return sceMcResSucceed;
    }

    erase_value = 0x00000000;
    if ((mcdi->cardflags & CF_ERASE_ZEROES) != 0)
        erase_value = 0xffffffff;

    for (j = 0; j < pageword_cnt; j++)
        mcfat_pagebuf.word[j] = erase_value;

    for (j = 0; j < 128; j++)
        *((uint32_t *)&mcfat_eccdata + j) = erase_value;

    for (i = 0; i < mcdi->blocksize; i++) {
        r = McWritePage(page + i, &mcfat_pagebuf, mcfat_eccdata);
        if (r != sceMcResSucceed) {
            r = mcfat_probePS2Card2();
            if (r != sceMcResSucceed)
                return -48;
        }
    }

    return 1;
}



void mcfat_initcache(void)
{
    register int i, j;
    uint8_t *p;

    DPRINTF("mcfat_initcache\n");

    j = MAX_CACHEENTRY - 1;
    p = (uint8_t *)mcfat_cachebuf;

    for (i = 0; i < MAX_CACHEENTRY; i++) {
        mcfat_entrycache[i].cl_data = (uint8_t *)p;
        mcfat_mccache[i] = (McCacheEntry *)&mcfat_entrycache[j - i];
        mcfat_entrycache[i].cluster = -1;
        p += MCMAN_CLUSTERSIZE;
    }

    pmcfat_entrycache = (McCacheEntry *)mcfat_entrycache;
    pmcfat_mccache = (McCacheEntry **)mcfat_mccache;

    mcfat_devinfos.unknown3 = -1;
    mcfat_devinfos.unknown4 = -1;
    mcfat_devinfos.unknown5 = -1;

    memset((void *)&mcfat_fatcache, -1, sizeof (mcfat_fatcache));

    mcfat_fatcache.entry[0] = 0;
}



int mcfat_clearcache()
{
    register int i, j;
    McCacheEntry **pmce = (McCacheEntry **)pmcfat_mccache;
    McCacheEntry *mce;

    DPRINTF("mcfat_clearcache\n");

    for (i = MAX_CACHEENTRY - 1; i >= 0; i--) {
        mce = (McCacheEntry *)pmce[i];
        if ((mce->cluster >= 0)) {
            mce->wr_flag = 0;
            mce->cluster = -1;
        }
    }
        DPRINTF("mcfat_clearcache mid\n");


    for (i = 0; i < (MAX_CACHEENTRY - 1); i++) {
        McCacheEntry *mce_save;

        mce = (McCacheEntry *)pmce[i];
        mce_save = (McCacheEntry *)pmce[i];
        if (mce->cluster < 0) {
            for (j = i+1; j < MAX_CACHEENTRY; j++) {
                mce = (McCacheEntry *)pmce[j];
                if (mce->cluster >= 0)
                    break;
            }
            if (j == MAX_CACHEENTRY)
                break;

            pmce[i] = (McCacheEntry *)pmce[j];
            pmce[j] = (McCacheEntry *)mce_save;
        }
    }

    memset((void *)&mcfat_fatcache, -1, sizeof (McFatCache));

    mcfat_fatcache.entry[0] = 0;



    return sceMcResSucceed;
}


McCacheEntry *mcfat_getcacheentry( int cluster)
{
    register int i;
    McCacheEntry *mce = (McCacheEntry *)pmcfat_entrycache;

    //DPRINTF("mcfat_getcacheentry  cluster %x\n",cluster);

    for (i = 0; i < MAX_CACHEENTRY; i++) {
        if (mce->cluster == cluster)
            return mce;
        mce++;
    }

    return NULL;
}


void mcfat_freecluster( int cluster) // release cluster from entrycache
{
    register int i;
    McCacheEntry *mce = (McCacheEntry *)pmcfat_entrycache;

    for (i = 0; i < MAX_CACHEENTRY; i++) {
        if (mce->cluster == cluster) {
            mce->cluster = -1;
            mce->wr_flag = 0;
        }
        mce++;
    }
}


int mcfat_getFATindex( int num)
{
    return mcfat_fatcache.entry[num];
}


void Mc1stCacheEntSetWrFlagOff(void)
{
    McCacheEntry *mce = (McCacheEntry *)*pmcfat_mccache;

    mce->wr_flag = -1;
}


McCacheEntry *mcfat_get1stcacheEntp(void)
{
    return *pmcfat_mccache;
}


void mcfat_addcacheentry(McCacheEntry *mce)
{
    register int i;
    McCacheEntry **pmce = (McCacheEntry **)pmcfat_mccache;

    i = MAX_CACHEENTRY - 1;

#if 0
    // This condition is always false because MAX_CACHEENTRY is always bigger than 0
    if (i < 0)
        goto lbl1;
#endif

    do {
        if (pmce[i] == mce)
            break;
    } while (--i >= 0);

    if (i != 0) {
#if 0
    // This label is not used.
lbl1:
#endif
        do {
            pmce[i] = pmce[i-1];
        } while (--i != 0);
    }

    pmce[0] = (McCacheEntry *)mce;
}


int McFlushCache()
{
    register int i;
    McCacheEntry **pmce = (McCacheEntry **)pmcfat_mccache;

    DPRINTF("McFlushCache \n");

    i = MAX_CACHEENTRY - 1;

    {
        while (i >= 0) {
            McCacheEntry *mce;

            mce = (McCacheEntry *)pmce[i];
            if (mce->wr_flag != 0) {
                register int r;

                r = mcfat_flushcacheentry((McCacheEntry *)mce);
                if (r != sceMcResSucceed)
                    return r;
            }
            i--;
        }
    }

    return sceMcResSucceed;
}


int mcfat_flushcacheentry(McCacheEntry *mce)
{
   register int r, i, j, ecc_count;
	register int temp1, temp2, offset, pageindex;
	static int clusters_per_block, blocksize, cardtype, pagesize, sparesize, flag, cluster, block, pages_per_fatclust;
	McCacheEntry *pmce[16]; // sp18
	register MCDevInfo *mcdi;
	McCacheEntry *mcee;
	static uint8_t eccbuf[32];
	void *p_page, *p_ecc;

	DPRINTF("mcfat_flushcacheentry mce %x cluster %x\n", (int)mce, (int)mce->cluster);

	if (mce->wr_flag == 0)
		return sceMcResSucceed;

	mcdi = (MCDevInfo *)&mcfat_devinfos;

	//mcdi->pagesize = sp84
	pagesize = mcdi->pagesize; //sp84
	cardtype = mcdi->cardtype;

	if (cardtype == 0) {
		mce->wr_flag = 0;
		return sceMcResSucceed;
	}


	clusters_per_block = mcdi->clusters_per_block; //sp7c
	block = mce->cluster / mcdi->clusters_per_block; //sp78
	blocksize = mcdi->blocksize;  //sp80
	sparesize = mcfat_sparesize(); //sp84
	flag = 0; //sp88

	memset((void *)pmce, 0, 64);

	i = 0; //s1
	if (MAX_CACHEENTRY > 0) {
		mcee = (McCacheEntry *)pmcfat_entrycache;
		do {
            temp1 = mcee->cluster / clusters_per_block;
            temp2 = mcee->cluster % clusters_per_block;

            if (temp1 == block) {
                pmce[temp2] = (McCacheEntry *)mcee;
                if (mcee->rd_flag == 0)
                    flag = 1;
            }
        
			mcee++;
		} while (++i < MAX_CACHEENTRY);
	}

	if (clusters_per_block > 0) {
		i = 0; //s1
		pageindex = 0; //s5
		cluster = block * clusters_per_block; // sp8c

		do {
			if (pmce[i] != 0) {
				j = 0; // s0
				offset = 0; //a0
				for (j = 0; j < mcdi->pages_per_cluster; j++) {
					mcfat_pagedata[pageindex + j] = (void *)(pmce[i]->cl_data + offset);
					offset += pagesize;
				}
			}
			else {
				//s3 = s5
				// s2 = (cluster + i) * mcdi->pages_per_cluster
				j = 0; //s0
				do {
					offset = (pageindex + j) * pagesize; // t0
					mcfat_pagedata[pageindex + j] = (void *)(mcfat_backupbuf + offset);

					r = McReadPage(((cluster + i) * mcdi->pages_per_cluster) + j, \
							mcfat_backupbuf + offset);
					if (r != sceMcResSucceed)
						return -51;

				} while (++j < mcdi->pages_per_cluster);
			}

			pageindex += mcdi->pages_per_cluster;
		} while (++i < clusters_per_block);
	}

lbl1:
	if ((flag != 0) && (mcfat_badblock <= 0)) {
		r = mcfat_eraseblock( mcdi->backup_block1, (void**)mcfat_pagedata, mcfat_eccdata);
		if (r == sceMcResFailReplace) {
lbl2:
			r = mcfat_replaceBackupBlock(mcdi->backup_block1);
			mcdi->backup_block1 = r;
			goto lbl1;
		}
		if (r != sceMcResSucceed)
			return -52;

		mcfat_pagebuf.word[0] = block | 0x80000000;
		p_page = (void *)&mcfat_pagebuf; //s0
		p_ecc = (void *)eccbuf; //s2 = sp58

		i = 0;	//s1
		do {
			if (pagesize < 0)
				ecc_count = (pagesize + 0x7f) >> 7;
			else
				ecc_count = pagesize >> 7;

			if (i >= ecc_count)
				break;

			McDataChecksum(p_page, p_ecc);

			p_ecc = (void *)((uint8_t *)p_ecc + 3);
			p_page = (void *)((uint8_t *)p_page + 128);
			i++;
		} while (1);


		r = McWritePage(mcdi->backup_block2 * blocksize, &mcfat_pagebuf, eccbuf);
		if (r == sceMcResFailReplace)
			goto lbl3;
		if (r != sceMcResSucceed)
			return -53;

		if (r < mcdi->blocksize) {
			i = 0; //s0
			p_ecc = (void *)mcfat_eccdata;

			do {
				r = McWritePage((mcdi->backup_block1 * blocksize) + i, mcfat_pagedata[i], p_ecc);
				if (r == sceMcResFailReplace)
					goto lbl2;
				if (r != sceMcResSucceed)
					return -54;
				p_ecc = (void *)((uint8_t *)p_ecc + sparesize);
			} while (++i < mcdi->blocksize);
		}

		r = McWritePage((mcdi->backup_block2 * blocksize) + 1, &mcfat_pagebuf, eccbuf);
		if (r == sceMcResFailReplace)
			goto lbl3;
		if (r != sceMcResSucceed)
			return -55;
	}

	r = mcfat_eraseblock(block, (void**)mcfat_pagedata, mcfat_eccdata);
	//if (block == 1) /////
	//	r = sceMcResFailReplace; /////
	if (r == sceMcResFailReplace) {
		r = mcfat_fillbackupblock1(block, (void**)mcfat_pagedata, mcfat_eccdata);
		for (i = 0; i < clusters_per_block; i++) {
			if (pmce[i] != 0)
				pmce[i]->wr_flag = 0;
		}
		if (r == sceMcResFailReplace)
			return r;
		return -58;
	}
	if (r != sceMcResSucceed)
		return -57;

	if (mcdi->blocksize > 0) {
		i = 0; //s0
		p_ecc = (void *)mcfat_eccdata;

		do {
			if (pmce[i / mcdi->pages_per_cluster] == 0) {
				r = McWritePage((block * blocksize) + i, mcfat_pagedata[i], p_ecc);
				if (r == sceMcResFailReplace) {
					r = mcfat_fillbackupblock1(block, (void**)mcfat_pagedata, mcfat_eccdata);
					for (i = 0; i < clusters_per_block; i++) {
						if (pmce[i] != 0)
							pmce[i]->wr_flag = 0;
					}
					if (r == sceMcResFailReplace)
						return r;
					return -58;
				}
				if (r != sceMcResSucceed)
					return -57;
			}
			p_ecc = (void *)((uint8_t *)p_ecc + sparesize);
		} while (++i < mcdi->blocksize);
	}

	if (mcdi->blocksize > 0) {
		i = 0; //s0
		p_ecc = (void *)mcfat_eccdata;

		do {
			if (pmce[i / mcdi->pages_per_cluster] != 0) {
				r = McWritePage((block * blocksize) + i, mcfat_pagedata[i], p_ecc);
				if (r == sceMcResFailReplace) {
					r = mcfat_fillbackupblock1(block, (void**)mcfat_pagedata, mcfat_eccdata);
					for (i = 0; i < clusters_per_block; i++) {
						if (pmce[i] != 0)
							pmce[i]->wr_flag = 0;
					}
					if (r == sceMcResFailReplace)
						return r;
					return -58;
				}
				if (r != sceMcResSucceed)
					return -57;
			}
			p_ecc = (void *)((uint8_t *)p_ecc + sparesize);
		} while (++i < mcdi->blocksize);
	}

	if (clusters_per_block > 0) {
		i = 0;
		do {
			if (pmce[i] != 0)
				pmce[i]->wr_flag = 0;
		} while (++i < clusters_per_block);
	}

	if ((flag != 0) && (mcfat_badblock <= 0)) {
		r = mcfat_eraseblock(mcdi->backup_block2, NULL, NULL);
		if (r == sceMcResFailReplace) {
			goto lbl3;
		}
		if (r != sceMcResSucceed)
			return -58;
	}
	goto lbl_exit;

lbl3:
	r = mcfat_replaceBackupBlock(mcdi->backup_block2);
	mcdi->backup_block2 = r;
	goto lbl1;

lbl_exit:
	return sceMcResSucceed;
}


int McReadCluster( int cluster, McCacheEntry **pmce)
{
    register int i;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce;

    if (mcfat_badblock > 0) {
        register int block, block_offset;

        block = cluster / mcdi->clusters_per_block;
        block_offset = cluster % mcdi->clusters_per_block;

        if (block == mcfat_badblock) {
            cluster = (mcdi->backup_block1 * mcdi->clusters_per_block) + block_offset;
        }
        else {
            {
                for (i = 0; (uint32_t)i < mcdi->clusters_per_block; i++) {
                    if ((mcfat_replacementcluster[i] != 0) && (mcfat_replacementcluster[i] == cluster)) {
                        block_offset = i % mcdi->clusters_per_block;
                        cluster = (mcdi->backup_block1 * mcdi->clusters_per_block) + block_offset;
                    }
                }
            }
        }
    }

    mce = mcfat_getcacheentry(cluster);
    if (mce == NULL) {
        register int r;

        mce = pmcfat_mccache[MAX_CACHEENTRY - 1];

        if (mce->wr_flag != 0) {
            r = mcfat_flushcacheentry((McCacheEntry *)mce);
            if (r != sceMcResSucceed)
                return r;
        }

        mce->cluster = cluster;
        mce->rd_flag = 0;
        //s3 = (cluster * mcdi->pages_per_cluster);

        for (i = 0; i < mcdi->pages_per_cluster; i++) {
            r = McReadPage((cluster * mcdi->pages_per_cluster) + i, \
                    (void *)(mce->cl_data + (i * mcdi->pagesize)));
            if (r != sceMcResSucceed)
                return -21;

        }
    }
    mcfat_addcacheentry(mce);
    *pmce = (McCacheEntry *)mce;

    return sceMcResSucceed;
}


int McReadDirEntry( int cluster, int fsindex, McFsEntry **pfse)
{
    register int r, i;
    static int maxent, index, clust;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    register McFatCache *fci = &mcfat_fatcache;
    McCacheEntry *mce;

    DPRINTF("McReadDirEntry cluster %d fsindex %d\n",cluster, fsindex);

    maxent = 0x402 / (mcdi->cluster_size >> 9); //a1
    index = fsindex / (mcdi->cluster_size >> 9);//s2

    clust = cluster;
    i = 0; // s0
    if ((cluster == 0) && (index != 0)) {
        if (index < maxent) {
            if ((fci->entry[index]) >= 0 )
                clust = fci->entry[index];
        }
        if (index > 0) {
            do {
                if (i >= maxent)
                    break;
                if (fci->entry[i] < 0)
                    break;
                clust = fci->entry[i];
            } while (++i < index);
        }
        i--;
    }

    if (i < index) {
        do {
            r = McGetFATentry(clust, &clust);
            if (r != sceMcResSucceed)
                return -70;

            if ((unsigned int)clust == 0xffffffff)
                return sceMcResNoEntry;
            clust &= ~0x80000000;

            i++;
            if (cluster == 0) {
                if (i < maxent)
                    fci->entry[i] = clust;
            }
        } while (i < index);
    }

    r = McReadCluster(mcdi->alloc_offset + clust, &mce);
    if (r != sceMcResSucceed)
        return -71;

    *pfse = (McFsEntry *)(mce->cl_data + ((fsindex % (mcdi->cluster_size >> 9)) << 9));

    return sceMcResSucceed;
}


int McSetFATentry( int fat_index, int fat_entry)
{
    register int r, ifc_index, indirect_index, indirect_offset, fat_offset;
    McCacheEntry *mce;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    //DPRINTF("McSetFATentry  fat_index %x fat_entry %x\n",fat_index, fat_entry);

    indirect_index = fat_index / mcdi->FATentries_per_cluster;
    fat_offset = fat_index % mcdi->FATentries_per_cluster;

    ifc_index = indirect_index / mcdi->FATentries_per_cluster;
    indirect_offset = indirect_index % mcdi->FATentries_per_cluster;

    r = McReadCluster(mcdi->ifc_list[ifc_index], &mce);
    if (r != sceMcResSucceed)
        return -75;

    McFatCluster *fc = (McFatCluster *)mce->cl_data;

    r = McReadCluster(fc->entry[indirect_offset], &mce);
    if (r != sceMcResSucceed)
        return -76;

    fc = (McFatCluster *)mce->cl_data;

     fc->entry[fat_offset] = fat_entry;
     mce->wr_flag = 1;

    return sceMcResSucceed;
}


int McGetFATentry( int fat_index, int *fat_entry)
{
    register int r, ifc_index, indirect_index, indirect_offset, fat_offset;
    McCacheEntry *mce;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    indirect_index = fat_index / mcdi->FATentries_per_cluster;
    fat_offset = fat_index % mcdi->FATentries_per_cluster;

    ifc_index = indirect_index / mcdi->FATentries_per_cluster;
    indirect_offset = indirect_index % mcdi->FATentries_per_cluster;

    r = McReadCluster(mcdi->ifc_list[ifc_index], &mce);
    if (r != sceMcResSucceed)
        return -78;

    McFatCluster *fc = (McFatCluster *)mce->cl_data;

    r = McReadCluster(fc->entry[indirect_offset], &mce);
    if (r != sceMcResSucceed)
        return -79;

    fc = (McFatCluster *)mce->cl_data;

    *fat_entry = fc->entry[fat_offset];

    return sceMcResSucceed;
}



int mcfat_replaceBackupBlock( int block)
{
    register int i;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    if (mcfat_badblock > 0)
        return sceMcResFailReplace;

    for (i = 0; i < 16; i++) {
        if (mcdi->bad_block_list[i] == -1)
            break;
    }

    if (i < 16) {
        if ((mcdi->alloc_end - mcdi->max_allocatable_clusters) < 8)
            return sceMcResFullDevice;

        mcdi->alloc_end -= 8;
        mcdi->bad_block_list[i] = block;
        mcfat_badblock = -1;

        return (mcdi->alloc_offset + mcdi->alloc_end) / mcdi->clusters_per_block;
    }

    return sceMcResFullDevice;
}


int mcfat_fillbackupblock1( int block, void **pagedata, void *eccdata)
{
    register int r, i, sparesize, page_offset;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    register uint8_t *p_ecc;

    DPRINTF("mcfat_fillbackupblock1 block %x mcfat_badblock %x\n",block, mcfat_badblock);

    sparesize = mcfat_sparesize();

    if (mcfat_badblock != 0) {
        if (mcfat_badblock != block)
            return sceMcResFailReplace;
    }

    if ((mcdi->alloc_offset / mcdi->clusters_per_block) == (uint32_t)block) // Appparently this refuse to take care of a bad rootdir cluster
        return sceMcResFailReplace;

    r = mcfat_eraseblock(mcdi->backup_block2, NULL, NULL);
    if (r != sceMcResSucceed)
        return r;

    r = mcfat_eraseblock(mcdi->backup_block1, NULL, NULL);
    if (r != sceMcResSucceed)
        return r;

    for (i = 0; i < 16; i++) {
        if (mcdi->bad_block_list[i] == -1)
            break;
    }

    if (i >= 16)
        return sceMcResFailReplace;

    page_offset = mcdi->backup_block1 * mcdi->blocksize;
    p_ecc = (uint8_t *)eccdata;

    for (i = 0; i < mcdi->blocksize; i++) {
        r = McWritePage(page_offset + i, pagedata[i], p_ecc);
        if (r != sceMcResSucceed)
            return r;
        p_ecc += sparesize;
    }

    mcfat_badblock = block;

    i = 15;
    do {
        mcfat_replacementcluster[i] = 0;
    } while (--i >= 0);

    return sceMcResSucceed;
}


int McReplaceBadBlock(void)
{
    register int r, i, curentry, clust, index, offset, numifc, fat_length, temp, length;
    register int value, value2, cluster, index2, offset2, s3;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    int fat_entry[16];
    int fat_entry2;
    McCacheEntry *mce;
    McFsEntry *fse;

    DPRINTF("McReplaceBadBlock mcfat_badblock_mcfat_badblock %x\n", mcfat_badblock);

    if (mcfat_badblock == 0)
        return sceMcResSucceed;

    if (mcfat_badblock >= 0) {
        McFlushCache();
        mcfat_clearcache();

        for (i = 0; i <16; i++) {
            if (mcdi->bad_block_list[i] == -1)
                break;
        }
        if (i >= 16)
            goto lbl_e168;

        if (mcdi->alloc_end >= (mcdi->max_allocatable_clusters + 8)) {
            mcdi->max_allocatable_clusters += 8;
            mcdi->bad_block_list[i] = mcfat_badblock;
        }

        fat_length = (((mcdi->clusters_per_card << 2) - 1) / mcdi->cluster_size) + 1;
        numifc = (((fat_length << 2) - 1) / mcdi->cluster_size) + 1;

        if (numifc > 32) {
            numifc = 32;
            fat_length = mcdi->FATentries_per_cluster << 5;
        }

        i = 0; //sp5c
        value = ~(((uint32_t)(-1)) << mcdi->clusters_per_block);
        do {
            clust = (mcfat_badblock * mcdi->clusters_per_block) + i;
            if ((uint32_t)clust < mcdi->alloc_offset) {
                fat_entry[i] = 0;
            }
            else {
                r = McGetFATentry(clust - mcdi->alloc_offset, &fat_entry[i]);
                if (r != sceMcResSucceed)
                    goto lbl_e168;

                if (((fat_entry[i] & 0x80000000) == 0) || ((fat_entry[i] < -1) && (fat_entry[i] >= -9)))
                    value &= ~(1 << i);
            }
        } while ((uint32_t)(++i) < mcdi->clusters_per_block);

        if (mcdi->clusters_per_block > 0) {
            i = 0;
            do {
                if ((value & (1 << i)) != 0) {
                    r = mcfat_findfree2(1);
                    if (r < 0) {
                        mcfat_replacementcluster[i] = r;
                        goto lbl_e168;
                    }
                    r += mcdi->alloc_offset;
                    mcfat_replacementcluster[i] = r;
                }
            } while ((uint32_t)(++i) < mcdi->clusters_per_block);
        }

        if (mcdi->clusters_per_block > 0) {
            i = 0;
            do {
                if ((value & (1 << i)) != 0) {
                    if (fat_entry[i] != 0) {
                        index = ((fat_entry[i] & ~0x80000000) + mcdi->alloc_offset) / mcdi->clusters_per_block;
                        offset = ((fat_entry[i] & ~0x80000000) + mcdi->alloc_offset) % mcdi->clusters_per_block;
                        if (index == mcfat_badblock) {
                            fat_entry[i] = (mcfat_replacementcluster[offset] - mcdi->alloc_offset) | 0x80000000;
                        }
                    }
                }
            } while ((uint32_t)(++i) < mcdi->clusters_per_block);
        }

        if (mcdi->clusters_per_block > 0) {
            i = 0;
            do {
                if ((mcfat_replacementcluster[i] != 0) && (fat_entry[i] != 0)) {
                    r = McSetFATentry(mcfat_replacementcluster[i] + mcdi->alloc_offset, fat_entry[i]);
                    if (r != sceMcResSucceed)
                        goto lbl_e168;
                }
            } while ((uint32_t)(++i) < mcdi->clusters_per_block);
        }

        for (i = 0; i < numifc; i++) {
            index = mcdi->ifc_list[i] / mcdi->clusters_per_block;
            offset = mcdi->ifc_list[i] % mcdi->clusters_per_block;

            if (index == mcfat_badblock) {
                value &= ~(1 << offset);
                mcdi->ifc_list[i] = mcfat_replacementcluster[i];
            }
        }

        if (value == 0)
            goto lbl_e030;

        for (i = 0; i < fat_length; i++) {
            index = i / mcdi->FATentries_per_cluster;
            offset = i % mcdi->FATentries_per_cluster;

            if (offset == 0) {
                r = McReadCluster(mcdi->ifc_list[index], &mce);
                if (r != sceMcResSucceed)
                    goto lbl_e168;
            }
            offset = i % mcdi->FATentries_per_cluster;
            index2 = *((uint32_t *)&mce->cl_data[offset]) / mcdi->clusters_per_block;
            offset2 = *((uint32_t *)&mce->cl_data[offset]) % mcdi->clusters_per_block;

            if (index2 == mcfat_badblock) {
                value &= ~(1 << offset2);
                *((uint32_t *)&mce->cl_data[offset]) = mcfat_replacementcluster[offset2];
                mce->wr_flag = 1;
            }
        }

        McFlushCache();
        mcfat_clearcache();

        if (value == 0)
            goto lbl_e030;

        value2 = value;

        for (i = 0; (uint32_t)i < mcdi->alloc_end; i++) {
            r = McGetFATentry(i, &fat_entry2);
            if (r != sceMcResSucceed)
                goto lbl_e168;

            index = (uint32_t)(((fat_entry2 & ~0x80000000) + mcdi->alloc_offset) / mcdi->clusters_per_block);
            offset = (uint32_t)(((fat_entry2 & ~0x80000000) + mcdi->alloc_offset) % mcdi->clusters_per_block);

            if (index == mcfat_badblock) {
                value &= ~(1 << offset);
                r = McSetFATentry(i, (mcfat_replacementcluster[offset] - mcdi->alloc_offset) | 0x80000000);
                if (r != sceMcResSucceed)
                    goto lbl_e168;
            }
        }

        if (value2 != value)
            McFlushCache();

        r = McReadDirEntry(0, 0, &fse);
        if (r != sceMcResSucceed)
            goto lbl_e168;

        curentry = 2;
        s3 = 0;
        length = fse->length;

lbl_dd8c:
        if (curentry >= length)
            goto lbl_ded0;

        r = McReadDirEntry(s3, curentry, &fse);
        if (r != sceMcResSucceed)
            goto lbl_e168;

        cluster = fse->cluster;
        index = (fse->cluster + mcdi->alloc_offset) / mcdi->clusters_per_block;
        offset = (fse->cluster + mcdi->alloc_offset) % mcdi->clusters_per_block;

        temp = fse->length;

        if (index == mcfat_badblock) {
            fse->cluster = mcfat_replacementcluster[offset] - mcdi->alloc_offset;
            Mc1stCacheEntSetWrFlagOff();
        }

        if ((fse->mode & sceMcFileAttrSubdir) == 0) {
            curentry++;
            goto lbl_dd8c;
        }

        r = McReadDirEntry(cluster, 0, &fse);
        if (r != sceMcResSucceed)
            goto lbl_e168;

        if ((fse->mode & sceMcFileAttrSubdir) == 0) {
            curentry++;
            goto lbl_dd8c;
        }

        if (fse->cluster != 0) {
            curentry++;
            goto lbl_dd8c;
        }

        if ((int)(fse->dir_entry) != curentry) {
            curentry++;
            goto lbl_dd8c;
        }

        curentry++;

        if (strcmp(fse->name, "."))
            goto lbl_dd8c;

        s3 = cluster;
        curentry = 2;
        length = temp;
        goto lbl_dd8c;

lbl_ded0:
        r = McReadDirEntry(s3, 1, &fse);
        if (r != sceMcResSucceed)
            goto lbl_e168;

        index = (fse->cluster + mcdi->alloc_offset) / mcdi->clusters_per_block;
        offset = (fse->cluster + mcdi->alloc_offset) % mcdi->clusters_per_block;

        if (index == mcfat_badblock) {
            fse->cluster = mcfat_replacementcluster[offset] - mcdi->alloc_offset;
            Mc1stCacheEntSetWrFlagOff();
        }

        r = McReadDirEntry(fse->cluster, fse->dir_entry, &fse);
        if (r != sceMcResSucceed)
            goto lbl_e168;

        length = fse->length;

        r = McReadDirEntry(s3, 0, &fse);
        if (r != sceMcResSucceed)
            goto lbl_e168;

        s3 = fse->cluster;
        index = (fse->cluster + mcdi->alloc_offset) / mcdi->clusters_per_block;
        offset = (fse->cluster + mcdi->alloc_offset) % mcdi->clusters_per_block;
        curentry = fse->dir_entry + 1;

        if (index == mcfat_badblock) {
            fse->cluster = mcfat_replacementcluster[offset] - mcdi->alloc_offset;
            Mc1stCacheEntSetWrFlagOff();
        }

        if (s3 != 0)
            goto lbl_dd8c;

        if (curentry != 1)
            goto lbl_dd8c;

lbl_e030:
        if (mcdi->clusters_per_block > 0) {
            i = 0;
            do {
                clust = (mcfat_badblock * mcdi->clusters_per_block) + i;
                if ((uint32_t)clust >= mcdi->alloc_offset) {
                    r = McSetFATentry(clust - mcdi->alloc_offset, 0xfffffffd);
                    if (r != sceMcResSucceed)
                        goto lbl_e168;
                }
            } while ((uint32_t)(++i) < mcdi->clusters_per_block);
        }

        McFlushCache();
        mcfat_clearcache();

        mcfat_badblock = 0;

        if (mcdi->clusters_per_block > 0) {
            i = 0;
            do {
                if (mcfat_replacementcluster[i] != 0) {
                    r = McReadCluster((mcdi->backup_block1 * mcdi->clusters_per_block) + i, &mce);
                    if (r != sceMcResSucceed)
                        goto lbl_e168;

                    mce->cluster = mcfat_replacementcluster[i];
                    mce->wr_flag = 1;
                }
            } while ((uint32_t)(++i) < mcdi->clusters_per_block);
        }

        McFlushCache();
    }
    else {
        mcfat_badblock = 0;
    }

    r = mcfat_clearsuperblock();
    return r;

lbl_e168:
    mcfat_badblock = 0;
    return sceMcResFailReplace;
}


int mcfat_clearsuperblock()
{
    register int r, i;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce;

    // set superblock magic & version
    memset((void *)mcdi->magic, 0, sizeof (mcdi->magic) + sizeof (mcdi->version));
    strcpy(mcdi->magic, SUPERBLOCK_MAGIC);
    strcat(mcdi->magic, SUPERBLOCK_VERSION);

    for (i = 0; (uint32_t)((unsigned int)i < sizeof(MCDevInfo)); i += 1024) {
        register int size, temp;

        temp = i;
        if (i < 0)
            temp = i + 1023;
        r = McReadCluster(temp >> 10, &mce);
        if (r != sceMcResSucceed)
            return -48;
        mce->wr_flag = 1;
        size = 1024;
        temp = sizeof(MCDevInfo) - i;

        if (temp <= 1024)
            size = temp;

        memcpy((void *)mce->cl_data, (void *)mcdi, size);
    }

    r = McFlushCache();

    return r;
}

int McCreateDir( const char* dirname )
{
    int f = 0x40;
    int r = McOpen(dirname, f);
    if (!r)
    {
        McClose(r);
    }
    
    return r;
}

bool McDirExists( const char* dirname )
{
    mcfat_stat_t stat = { 0x0 };
    int f = 0x1;
    int r = mcfat_getstat2(dirname, &stat);

    return r >= 0 && (stat.mode & MC_IO_S_DR);
}

bool McFileExists( const char* filename )
{    mcfat_stat_t stat = { 0x0 };
    int f = 0x1;
    int r = mcfat_getstat2(filename, &stat);
    
    return r >= 0 && (stat.mode & MC_IO_S_FL);
}

void McSetConfig(mcfat_mcops_t* ops, mcfat_datasource_info_t* info)
{
    mcops = *ops;
    mcdsi = *info;
}