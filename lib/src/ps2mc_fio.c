/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright (c) 2009 jimmikaelkael
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#include <string.h>
#include "mcfat-internal.h"

static int mcfat_curdirmaxent;
static int mcfat_curdirlength;
static char mcfat_curdirpath[1024];
static const char *mcfat_curdirname;
static sceMcStDateTime mcfat_fsmodtime;

//--------------------------------------------------------------
int mcfat_format2()
{
    register int r, i, size, ifc_index, indirect_offset, allocatable_clusters_per_card;
    register int ifc_length, fat_length, fat_entry, alloc_offset;
    register int j = 0, z = 0;
    MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce;

    DPRINTF("mcfat_format2 cardform %d\n", mcdi->cardform);

    if (mcdi->cardform < 0) {
        for (i = 0; i < 32; i++)
            mcdi->bad_block_list[i] = -1;
        mcdi->rootdir_cluster = 0;
        mcdi->rootdir_cluster2 = 0;
        goto lbl1;
    }

    if (mcdi->cardform > 0) {
        if (((mcdi->version[0] - 48) >= 2) || ((mcdi->version[2] - 48) >= 2))
            goto lbl1;
    }

    r = mcfat_reportBadBlocks();
    if ((r != sceMcResSucceed) && (r != sceMcResNoFormat))
        return sceMcResChangedCard;

lbl1:
    // set superblock magic & version
    memset((void *)&mcdi->magic, 0, sizeof (mcdi->magic) + sizeof (mcdi->version));
    strcpy(mcdi->magic, SUPERBLOCK_MAGIC);
    strcat(mcdi->magic, SUPERBLOCK_VERSION);

    //size = 8192 / mcdi->cluster_size; // get blocksize in cluster a better way must be found
    size = mcdi->blocksize;
    mcdi->cardform = -1;

    mcfat_wr_block = -1;
    mcfat_wr_flag3 = -10;

    //if (size <= 0)
    //	size = 1;
    if (mcdi->blocksize <= 0)
        size = 8;

    // clear first 8 clusters
    for (i = 0; i < size; i++) {
        r = mcfat_writecluster(i, 1);
        if (r == 0)
            return sceMcResNoFormat;

        if (r != 1)
            return -40;
    }

    fat_length = (((mcdi->clusters_per_card << 2) - 1) / mcdi->cluster_size) + 1; // get length of fat in clusters
    ifc_length = (((fat_length << 2) - 1) / mcdi->cluster_size) + 1; // get number of needed ifc clusters

    if (!(ifc_length <= 32)) {
        ifc_length = 32;
        fat_length = mcdi->FATentries_per_cluster << 5;
    }

    // clear ifc clusters
    if (ifc_length > 0) {
        j = 0;
        do {
            if ((uint32_t)i >= mcdi->clusters_per_card)
                return sceMcResNoFormat;

            do {
                if (mcfat_writecluster(i, 1) != 0)
                    break;

                i++;
            } while ((uint32_t)i < mcdi->clusters_per_card);

            if ((uint32_t)i >= mcdi->clusters_per_card)
                return sceMcResNoFormat;

            mcdi->ifc_list[j] = i;
            j++;
            i++;
        } while (j < ifc_length);
    }

    // read ifc clusters to mc cache and clear fat clusters
    if (fat_length > 0) {
        j = 0;

        do {
            ifc_index = j / mcdi->FATentries_per_cluster;
            indirect_offset = j % mcdi->FATentries_per_cluster;

            if (indirect_offset == 0) {
                if (McReadCluster(mcdi->ifc_list[ifc_index], &mce) != sceMcResSucceed)
                    return -42;
                mce->wr_flag = 1;
            }

            if ((uint32_t)i >= mcdi->clusters_per_card)
                return sceMcResNoFormat;

            do {
                r = mcfat_writecluster(i, 1);
                if (r == 1)
                    break;

                if (r < 0)
                    return -43;

                i++;
            } while ((uint32_t)i < mcdi->clusters_per_card);

            if ((uint32_t)i >= mcdi->clusters_per_card)
                return sceMcResNoFormat;

            j++;
            McFatCluster *fc = (McFatCluster *)mce->cl_data;
            fc->entry[indirect_offset] = i;
            i++;

        } while (j < fat_length);
    }
    alloc_offset = i;

    mcdi->backup_block1 = 0;
    mcdi->backup_block2 = 0;

    // clear backup blocks
    for (i = (mcdi->clusters_per_card / mcdi->clusters_per_block) - 1; i > 0; i--) {

        r = mcfat_writecluster(mcdi->clusters_per_block * i, 1);
        if (r < 0)
            return -44;

        if ((r != 0) && (mcdi->backup_block1 == 0))
            mcdi->backup_block1 = i;
        else if ((r != 0) && (mcdi->backup_block2 == 0)) {
            mcdi->backup_block2 = i;
            break;
        }
    }

    // set backup block2 to erased state
    if (mcfat_eraseblock(mcdi->backup_block2, NULL, NULL) != sceMcResSucceed)
        return -45;

    uint32_t hi, lo, temp;

    long_multiply(mcdi->clusters_per_card, 0x10624dd3, &hi, &lo);
    temp = (hi >> 6) - (mcdi->clusters_per_card >> 31);
    allocatable_clusters_per_card = (((((temp << 5) - temp) << 2) + temp) << 3) + 1;
    j = alloc_offset;

    // checking for bad allocated clusters and building FAT
    if ((uint32_t)j < i * mcdi->clusters_per_block) {
        z = 0;
        do { // quick check for bad clusters
            r = mcfat_writecluster(j, 0);
            if (r == 1) {
                if (z == 0) {
                    mcdi->alloc_offset = j;
                    mcdi->rootdir_cluster = 0;
                    fat_entry = 0xffffffff; // marking rootdir end
                }
                else
                    fat_entry = ~0x80000000;	// marking free cluster
                z++;
                if (z == allocatable_clusters_per_card)
                    mcdi->max_allocatable_clusters = (j - mcdi->alloc_offset) + 1;
            }
            else {
                if (r != 0)
                    return -45;
                fat_entry = 0xfffffffd; // marking bad cluster
            }

            if (McSetFATentry(j - mcdi->alloc_offset, fat_entry) != sceMcResSucceed)
                return -46;

            j++;
        } while ((uint32_t)j < (i * mcdi->clusters_per_block));
    }

    mcdi->alloc_end = (i * mcdi->clusters_per_block) - mcdi->alloc_offset;

    if (mcdi->max_allocatable_clusters == 0)
        mcdi->max_allocatable_clusters = i * mcdi->clusters_per_block;

    if ((uint32_t)z < mcdi->clusters_per_block)
        return sceMcResNoFormat;

    // read superblock to mc cache
    for (i = 0; (unsigned int)i < sizeof (MCDevInfo); i += MCMAN_CLUSTERSIZE) {
        if (i < 0)
            size = i + (MCMAN_CLUSTERSIZE - 1);
        else
            size = i;

        if (McReadCluster(size >> 10, &mce) != sceMcResSucceed)
            return -48;

        size = MCMAN_CLUSTERSIZE;
        mce->wr_flag = 1;

        if ((sizeof (MCDevInfo) - i) <= 1024)
            size = sizeof (MCDevInfo) - i;

        memcpy((void *)mce->cl_data, (void *)(mcdi + i), size);
    }

    mcdi->unknown1 = 0;
    mcdi->unknown2 = 0;
    mcdi->unknown5 = -1;
    mcdi->rootdir_cluster2 = mcdi->rootdir_cluster;

    // Create root dir
    if (McCreateDirentry(0, 0, 0, NULL) != sceMcResSucceed)
        return -49;

    // finally flush cache to memcard
    r = McFlushCache();
    if (r != sceMcResSucceed)
        return r;

    mcdi->cardform = 1;

    return sceMcResSucceed;
}

//--------------------------------------------------------------
int mcfat_dread2(int fd, mcfat_dirent_t *dirent)
{
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    McFsEntry *fse;

    DPRINTF("mcfat_dread2 fd %d\n", fd);

    if (fh->position >= fh->filesize)
        return sceMcResSucceed;

    do {
        register int r;

        r = McReadDirEntry(fh->freeclink, fh->position, &fse);
        if (r != sceMcResSucceed)
            return r;

        if (fse->mode & sceMcFileAttrExists)
            break;

        fh->position++;
    } while (fh->position < fh->filesize);

    if (fh->position >= fh->filesize)
        return sceMcResSucceed;

    fh->position++;
    mcfat_wmemset((void *)dirent, sizeof (mcfat_dirent_t), 0);
    strcpy(dirent->name, fse->name);
    *(uint8_t *)&dirent->name[32] = 0;

    if (fse->mode & sceMcFileAttrReadable)
        dirent->stat.mode |= MC_IO_S_RD;
    if (fse->mode & sceMcFileAttrWriteable)
        dirent->stat.mode |= MC_IO_S_WR;
    if (fse->mode & sceMcFileAttrExecutable)
        dirent->stat.mode |= MC_IO_S_EX;
#if !MCMAN_ENABLE_EXTENDED_DEV_OPS
    if (fse->mode & sceMcFileAttrPS1)
        dirent->stat.mode |= sceMcFileAttrPS1;
    if (fse->mode & sceMcFileAttrPDAExec)
        dirent->stat.mode |= sceMcFileAttrPDAExec;
    if (fse->mode & sceMcFileAttrDupProhibit)
        dirent->stat.mode |= sceMcFileAttrDupProhibit;
#endif
    if (fse->mode & sceMcFileAttrSubdir)
        dirent->stat.mode |= MC_IO_S_DR;
    else
        dirent->stat.mode |= MC_IO_S_FL;

    dirent->stat.attr = fse->attr;
    dirent->stat.size = fse->length;
    memcpy(dirent->stat.ctime, &fse->created, sizeof(sceMcStDateTime));
    memcpy(dirent->stat.mtime, &fse->modified, sizeof(sceMcStDateTime));

    return 1;
}

//--------------------------------------------------------------
int mcfat_getstat2( const char *filename, mcfat_stat_t *stat)
{
    register int r;
    McFsEntry *fse;

    DPRINTF("mcfat_getstat2 filename %s\n", filename);

    r = mcfat_cachedirentry(filename, NULL, &fse, 1);
    if (r != sceMcResSucceed)
        return r;

    mcfat_wmemset((void *)stat, sizeof (mcfat_stat_t), 0);

    if (fse->mode & sceMcFileAttrReadable)
        stat->mode |= MC_IO_S_RD;
    if (fse->mode & sceMcFileAttrWriteable)
        stat->mode |= MC_IO_S_WR;
    if (fse->mode & sceMcFileAttrExecutable)
        stat->mode |= MC_IO_S_EX;
    if (fse->mode & sceMcFileAttrPS1)
        stat->mode |= sceMcFileAttrPS1;
    if (fse->mode & sceMcFileAttrPDAExec)
        stat->mode |= sceMcFileAttrPDAExec;
    if (fse->mode & sceMcFileAttrDupProhibit)
        stat->mode |= sceMcFileAttrDupProhibit;
    if (fse->mode & sceMcFileAttrSubdir)
        stat->mode |= MC_IO_S_DR;
    else
        stat->mode |= MC_IO_S_FL;

    stat->attr = fse->attr;

    if (!(fse->mode & sceMcFileAttrSubdir))
        stat->size = fse->length;

    memcpy(stat->ctime, &fse->created, sizeof(sceMcStDateTime));
    memcpy(stat->mtime, &fse->modified, sizeof(sceMcStDateTime));

    return sceMcResSucceed;
}

//--------------------------------------------------------------
int mcfat_setinfo2( const char *filename, sceMcTblGetDir *info, int flags)
{
    register int r, fmode;
    McFsEntry *fse;
    McCacheDir dirInfo;
    McFsEntry mfe;

    DPRINTF("mcfat_setinfo2 filename %s flags %x\n", filename, flags);

    r = mcfat_cachedirentry(filename, &dirInfo, &fse, 1); //dirInfo=sp218 fse=sp228
    if (r != sceMcResSucceed)
        return r;

    if ((flags & sceMcFileAttrFile) != 0)	{
        uint8_t *pfsentry, *pfseend, *mfee;

        if ((!strcmp(".", info->EntryName)) || (!strcmp("..", info->EntryName)))
            return sceMcResNoEntry;

        if (info->EntryName[0] == 0)
            return sceMcResNoEntry;

        r = mcfat_chrpos(info->EntryName, '/');
        if (r >= 0)
            return sceMcResNoEntry;

        if (dirInfo.fsindex < 2)
            return sceMcResNoEntry;

        r = McReadDirEntry(dirInfo.cluster, 0, &fse);
        if (r != sceMcResSucceed)
            return r;

        r = McReadDirEntry(fse->cluster, fse->dir_entry, &fse);
        if (r != sceMcResSucceed)
            return r;

        pfsentry = (uint8_t *)fse;
        mfee = (uint8_t *)&mfe;
        pfseend = (uint8_t *)(pfsentry + sizeof (McFsEntry));

        do {
            *((uint32_t *)mfee  ) = *((uint32_t *)pfsentry  );
            *((uint32_t *)mfee+1) = *((uint32_t *)pfsentry+1);
            *((uint32_t *)mfee+2) = *((uint32_t *)pfsentry+2);
            *((uint32_t *)mfee+3) = *((uint32_t *)pfsentry+3);
            pfsentry += 16;
            mfee += 16;
        } while (pfsentry < pfseend);

        r = mcfat_getdirinfo(&mfe, info->EntryName, NULL, 1);
        if (r != 1) {
            if (r < 2) {
                if (r == 0)
                    return sceMcResNoEntry;
                return r;
            }
            else {
                if (r != 2)
                    return r;
                return sceMcResDeniedPermit;
            }
        }
    }

    r = McReadDirEntry(dirInfo.cluster, dirInfo.fsindex, &fse);
    if (r != sceMcResSucceed)
        return r;

    Mc1stCacheEntSetWrFlagOff();

    //Special fix clause for file managers (like uLaunchELF)
    //This allows writing most entries that can be read by mcGetDir
    //and without usual restrictions. This flags value should cause no conflict
    //as Sony code never uses it, and the value is changed after its use here.
    if(flags == 0xFEED){
        fse->mode = info->AttrFile;
        //fse->unused = info->Reserve1;
        //fse->length = info->FileSizeByte;
        flags = sceMcFileAttrReadable|sceMcFileAttrWriteable;
        //The changed flags value allows more entries to be copied below
    }

    if ((flags & sceMcFileAttrDupProhibit) != 0)
        fse->attr = info->Reserve2;

    if ((flags & sceMcFileAttrExecutable) != 0) {
        fmode = 0x180f;
        fse->mode = (fse->mode & ~fmode) | (info->AttrFile & fmode);
    }

    if ((flags & sceMcFileCreateFile) != 0)	{
        fmode = 0x180f;
        fse->mode = (fse->mode & ~fmode) | (info->AttrFile & fmode);
    }

    if ((flags & sceMcFileAttrReadable) != 0)
        fse->created = info->_Create;

    if ((flags & sceMcFileAttrWriteable) != 0)
        fse->modified = info->_Modify;

    if ((flags & sceMcFileAttrFile) != 0) {
        strncpy(fse->name, info->EntryName, 32);
        fse->name[31] = 0;
    }

    return sceMcResSucceed;
}

//--------------------------------------------------------------
int mcfat_read2(int fd, void *buffer, int nbyte)
{
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce;

    DPRINTF("mcfat_read2 fd %d buf %x size %d\n", fd, (int)buffer, nbyte);

    if (fh->position < fh->filesize) {
        register int temp, rpos;

        temp = fh->filesize - fh->position;
        if (nbyte > temp)
            nbyte = temp;

        rpos = 0;
        if (nbyte > 0) {

            do {
                register int r, size, offset;

                offset = fh->position % mcdi->cluster_size;  // file pointer offset % cluster size
                temp = mcdi->cluster_size - offset;
                if (temp < nbyte)
                    size = temp;
                else
                    size = nbyte;

                r = mcfat_fatRseek(fd);

                if (r <= 0)
                    return r;

                r = McReadCluster(r, &mce);
                if (r != sceMcResSucceed)
                    return r;

                memcpy((void *)((uint8_t *)buffer + rpos), (void *)((uint8_t *)(mce->cl_data) + offset), size);

                rpos += size;
                mce->rd_flag = 1;
                nbyte -= size;
                fh->position += size;

            } while (nbyte);
        }
        return rpos;
    }

    return 0;
}

//--------------------------------------------------------------
int mcfat_write2(int fd, void *buffer, int nbyte)
{
    register int r, wpos;
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheEntry *mce;

    if (nbyte) {
        if (fh->unknown2 == 0) {
            fh->unknown2 = 1;
            r = mcfat_close2(fd);
            if (r != sceMcResSucceed)
                return r;
            r = McFlushCache();
            if (r != sceMcResSucceed)
                return r;
        }
    }

    wpos = 0;
    if (nbyte) {
        do {
            register int r2, size, offset;

            r = mcfat_fatRseek(fd);
            if (r == sceMcResFullDevice) {

                r2 = mcfat_fatWseek(fd);
                if (r2 == r)
                    return sceMcResFullDevice;

                if (r2 != sceMcResSucceed)
                    return r2;

                r = mcfat_fatRseek(fd);
            }
            else {
                if (r < 0)
                    return r;
            }

            r = McReadCluster(r, &mce);
            if (r != sceMcResSucceed)
                return r;

            mce->rd_flag = 1;

            offset = fh->position % mcdi->cluster_size;  // file pointer offset % cluster size
            r2 = mcdi->cluster_size - offset;
            if (r2 < nbyte)
                size = r2;
            else
                size = nbyte;

            memcpy((void *)((uint8_t *)(mce->cl_data) + offset), (void *)((uint8_t *)buffer + wpos), size);

            mce->wr_flag = 1;

            r = fh->position + size;
            fh->position += size;

            if ((uint32_t)r < fh->filesize)
                r = fh->filesize ;

            fh->filesize = r;

            nbyte -= size;
            wpos += size;

        } while (nbyte);
    }

    r = mcfat_close2(fd);
    if (r != sceMcResSucceed)
        return r;

    return wpos;
}

//--------------------------------------------------------------
int mcfat_close2(int fd)
{
    register int r, fmode;
    register MC_FHANDLE *fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
    McFsEntry *fse1, *fse2;

    DPRINTF("mcfat_close2 fd %d\n", fd);

    r = McReadDirEntry(fh->field_20, fh->field_24, &fse1);
    if (r != sceMcResSucceed)
        return -31;

    if (fh->unknown2 == 0) {
        fmode = fse1->mode | sceMcFileAttrClosed;
    }
    else {
        fmode = fse1->mode & 0xff7f;
    }
    fse1->mode = fmode;

    mcfat_getmcrtime(&fse1->modified);

    fse1->cluster = fh->freeclink;
    fse1->length = fh->filesize;

    Mc1stCacheEntSetWrFlagOff();

    mcfat_fsmodtime = fse1->modified;

    r = McReadDirEntry(fh->field_28, fh->field_2C, &fse2);
    if (r != sceMcResSucceed)
        return r;

    fse2->modified = mcfat_fsmodtime;

    Mc1stCacheEntSetWrFlagOff();

    return sceMcResSucceed;
}

//--------------------------------------------------------------
int mcfat_open2( const char *filename, int flags)
{
    register int fd, i, r, rdflag, wrflag, pos, mcfree;
    register MC_FHANDLE *fh;
    register MCDevInfo *mcdi;
    McCacheDir cacheDir;
    McFsEntry *fse1, *fse2;
    McCacheEntry *mce;
    uint8_t *pfsentry, *pcache, *pfseend;
    const char *p;
    int fat_entry;

    DPRINTF("mcfat_open2 name %s flags %x\n", filename, flags);

    if ((flags & sceMcFileCreateFile) != 0)
        flags |= sceMcFileAttrWriteable; // s5

    //if (!mcfat_checkpath(filename))
    //	return sceMcResNoEntry;
    if (filename[0] == 0)
        return sceMcResNoEntry;

    fd = 0;
    do {
        fh = (MC_FHANDLE *)&mcfat_fdhandles[fd];
        if (fh->status == 0)
            break;
    } while (++fd < MAX_FDHANDLES);

    if (fd == MAX_FDHANDLES)
        return sceMcResUpLimitHandle;

    fh = (MC_FHANDLE *)&mcfat_fdhandles[fd]; // s2

    mcfat_wmemset((void *)fh, sizeof (MC_FHANDLE), 0);

    mcdi = (MCDevInfo *)&mcfat_devinfos; // s3

    if ((flags & (sceMcFileCreateFile | sceMcFileCreateDir)) == 0)
        cacheDir.maxent = -1; //sp20
    else
        cacheDir.maxent = 0;  //sp20

    //fse1 = sp28
    //sp18 = cacheDir

    fse1 = NULL;
    r = mcfat_cachedirentry(filename, &cacheDir, &fse1, 1);
    if (r < 0)
        return r;

    if (fse1) {
        pfsentry = (uint8_t *)fse1;
        pcache = (uint8_t *)&mcfat_dircache[1];
        pfseend = (uint8_t *)(pfsentry + sizeof(McFsEntry));

        do {
            *((uint32_t *)pcache  ) = *((uint32_t *)pfsentry  );
            *((uint32_t *)pcache+1) = *((uint32_t *)pfsentry+1);
            *((uint32_t *)pcache+2) = *((uint32_t *)pfsentry+2);
            *((uint32_t *)pcache+3) = *((uint32_t *)pfsentry+3);
            pfsentry += 16;
            pcache += 16;
        } while (pfsentry < pfseend);
    }

    if ((flags == 0) && ((fse1->mode & sceMcFileAttrExists) == 0))
        r = 1;

    if (r == 2)
        return sceMcResNoEntry;

    if (r == 3)
        return sceMcResDeniedPermit;

    if ((r == 0) && ((flags & sceMcFileCreateDir) != 0))
        return sceMcResNoEntry;

    if ((r == 1) && ((flags & (sceMcFileCreateFile | sceMcFileCreateDir)) == 0))
        return sceMcResNoEntry;

    rdflag = flags & sceMcFileAttrReadable;
    wrflag = flags & sceMcFileAttrWriteable;
    fh->freeclink = -1;
    fh->clink = -1;
    fh->clust_offset = 0;
    fh->filesize = 0;
    fh->position = 0;
    fh->unknown2 = 0;
    fh->rdflag = rdflag;
    fh->wrflag = wrflag;
    fh->unknown1 = 0;
    fh->field_20 = cacheDir.cluster;
    fh->field_24 = cacheDir.fsindex;

    // fse2 = sp2c

    if (r == 0) {

        if ((wrflag != 0) && ((mcfat_dircache[1].mode & sceMcFileAttrWriteable) == 0))
            return sceMcResDeniedPermit;

        if ((flags & sceMcFileAttrReadable) != 0) {
            if ((mcfat_dircache[1].mode & sceMcFileAttrReadable) == 0)
                return sceMcResDeniedPermit;
        }
        r = McReadDirEntry(cacheDir.cluster, 0, &fse2);
        if (r != sceMcResSucceed)
            return r;

        fh->field_28 = fse2->cluster;
        fh->field_2C = fse2->dir_entry;

        if ((mcfat_dircache[1].mode & sceMcFileAttrSubdir) != 0) {
            if ((mcfat_dircache[1].mode & sceMcFileAttrReadable) == 0)
                return sceMcResDeniedPermit;

            fh->freeclink = mcfat_dircache[1].cluster;
            fh->rdflag = 0;
            fh->wrflag = 0;
            fh->unknown1 = 0;
            fh->drdflag = 1;
            fh->status = 1;
            fh->filesize = mcfat_dircache[1].length;
            fh->clink = fh->freeclink;

            return fd;
        }

        if ((flags & sceMcFileAttrWriteable) != 0) {
            i = 0;
            do {
                register MC_FHANDLE *fh2;

                fh2 = (MC_FHANDLE *)&mcfat_fdhandles[i];

                if ((fh2->status == 0) \
                    || (fh2->field_20 != (uint32_t)(cacheDir.cluster)) || (fh2->field_24 != (uint32_t)(cacheDir.fsindex)))
                    continue;

                if (fh2->wrflag != 0)
                    return sceMcResDeniedPermit;

            } while (++i < MAX_FDHANDLES);
        }

        if ((flags & sceMcFileCreateFile) != 0) {
            r = McSetDirEntryState(cacheDir.cluster, cacheDir.fsindex, 0);
            McFlushCache();

            if (r != sceMcResSucceed)
                return -43;

            if (cacheDir.fsindex < cacheDir.maxent)
                cacheDir.maxent = cacheDir.fsindex;
        }
        else {
            fh->freeclink = mcfat_dircache[1].cluster;
            fh->filesize = mcfat_dircache[1].length;
            fh->clink = fh->freeclink;

            if (fh->rdflag != 0)
                fh->rdflag = (*((uint8_t *)&mcfat_dircache[1].mode)) & sceMcFileAttrReadable;
            else
                fh->rdflag = 0;

            if (fh->wrflag != 0)
                fh->wrflag = (mcfat_dircache[1].mode >> 1) & sceMcFileAttrReadable;
            else
                fh->wrflag = 0;

            fh->status = 1;

            return fd;
        }
    }
    else {
        fh->field_28 = cacheDir.cluster;
        fh->field_2C = cacheDir.fsindex;
    }

    r = McReadDirEntry(fh->field_28, fh->field_2C, &fse1);
    if (r != sceMcResSucceed)
        return r;

    pfsentry = (uint8_t *)fse1;
    pcache = (uint8_t *)&mcfat_dircache[2];
    pfseend = (uint8_t *)(pfsentry + sizeof(McFsEntry));

    do {
        *((uint32_t *)pcache  ) = *((uint32_t *)pfsentry  );
        *((uint32_t *)pcache+1) = *((uint32_t *)pfsentry+1);
        *((uint32_t *)pcache+2) = *((uint32_t *)pfsentry+2);
        *((uint32_t *)pcache+3) = *((uint32_t *)pfsentry+3);
        pfsentry += 16;
        pcache += 16;
    } while (pfsentry < pfseend);

    i = -1;
    if (mcfat_dircache[2].length == (uint32_t)(cacheDir.maxent)) {
        register int fsindex, fsoffset;

        fsindex = mcfat_dircache[2].length / (mcdi->cluster_size >> 9); //v1
        fsoffset = mcfat_dircache[2].length % (mcdi->cluster_size >> 9); //v0

        if (fsoffset == 0) {
            register int fat_index;

            fat_index = mcfat_dircache[2].cluster;
            i = fsindex;

            if ((mcfat_dircache[2].cluster == 0) && (i >= 2)) {
                if (mcfat_getFATindex(i - 1) >= 0) {
                    fat_index = mcfat_getFATindex(i - 1); // s3
                    i = 1;
                }
            }
            i--;

            if (i != -1) {

                do {
                    r = McGetFATentry(fat_index, &fat_entry);
                    if (r != sceMcResSucceed)
                        return r;

                    if (fat_entry >= -1) {
                        r = mcfat_findfree2(1);
                        if (r < 0)
                            return r;

                        fat_entry = r;
                        mce = mcfat_get1stcacheEntp(); // s4

                        fat_entry |= 0x80000000;

                        r = McSetFATentry(fat_index, fat_entry);
                        if (r != sceMcResSucceed)
                            return r;

                        mcfat_addcacheentry(mce);
                    }
                    i--;
                    fat_index = fat_entry & ~0x80000000;

                } while (i != -1);
            }
        }

        r = McFlushCache();
        if (r != sceMcResSucceed)
            return r;

        i = -1;

        mcfat_dircache[2].length++;
    }

    do {
        p = filename + i + 1;
        pos = i + 1;
        r = mcfat_chrpos(p, '/');
        if (r < 0)
            break;
        i = pos + r;
    } while (1);

    p = filename + pos;

    mcfree = 0;

    if ((flags & sceMcFileCreateDir) != 0) {
        r = mcfat_findfree2(1); // r = s3
        if (r < 0)
            return r;
        mcfree = r;
    }

    mce = mcfat_get1stcacheEntp(); // mce = s4

    mcfat_getmcrtime(&mcfat_dircache[2].modified);

    r = McReadDirEntry(mcfat_dircache[2].cluster, cacheDir.maxent, &fse2);
    if (r != sceMcResSucceed)
        return r;

    mcfat_wmemset((void *)fse2, sizeof (McFsEntry), 0);

    strncpy(fse2->name, p, 32);

    fse2->created = mcfat_dircache[2].modified;
    fse2->modified = mcfat_dircache[2].modified;

    Mc1stCacheEntSetWrFlagOff();

    mcfat_addcacheentry(mce);

    flags &= 0xffffdfff;

    if ((flags & sceMcFileCreateDir) != 0) {

        fse2->mode = ((flags & sceMcFileAttrHidden) | sceMcFileAttrReadable | sceMcFileAttrWriteable \
                | sceMcFileAttrExecutable | sceMcFileAttrSubdir | sceMcFile0400 | sceMcFileAttrExists) // 0x8427
                    | (flags & (sceMcFileAttrPS1 | sceMcFileAttrPDAExec));
        fse2->length = 2;
        fse2->cluster = mcfree;

        r = McCreateDirentry(mcfat_dircache[2].cluster, cacheDir.maxent, mcfree, (sceMcStDateTime *)&fse2->created);
        if (r != sceMcResSucceed)
            return -46;

        r = McReadDirEntry(fh->field_28, fh->field_2C, &fse1);
        if (r != sceMcResSucceed)
            return r;

        pfsentry = (uint8_t *)fse1;
        pcache = (uint8_t *)&mcfat_dircache[2];
        pfseend = (uint8_t *)(pfsentry + sizeof(McFsEntry));

        do {
            *((uint32_t *)pfsentry  ) = *((uint32_t *)pcache  );
            *((uint32_t *)pfsentry+1) = *((uint32_t *)pcache+1);
            *((uint32_t *)pfsentry+2) = *((uint32_t *)pcache+2);
            *((uint32_t *)pfsentry+3) = *((uint32_t *)pcache+3);
            pfsentry += 16;
            pcache += 16;
        } while (pfsentry < pfseend);

        Mc1stCacheEntSetWrFlagOff();

        r = McFlushCache();
        if (r != sceMcResSucceed)
            return r;

        return sceMcResSucceed;
    }
    else {
        fse2->mode = ((flags & sceMcFileAttrHidden) | sceMcFileAttrReadable | sceMcFileAttrWriteable \
                | sceMcFileAttrExecutable | sceMcFileAttrFile | sceMcFile0400 | sceMcFileAttrExists) // 0x8417
                    | (flags & (sceMcFileAttrPS1 | sceMcFileAttrPDAExec));

        fse2->cluster = -1;
        fh->field_20 = mcfat_dircache[2].cluster;
        fh->status = 1;
        fh->field_24 = cacheDir.maxent;

        r = McReadDirEntry(fh->field_28, fh->field_2C, &fse1);
        if (r != sceMcResSucceed)
            return r;

        pfsentry = (uint8_t *)fse1;
        pcache = (uint8_t *)&mcfat_dircache[2];
        pfseend = (uint8_t *)(pfsentry + sizeof(McFsEntry));

        do {
            *((uint32_t *)pfsentry  ) = *((uint32_t *)pcache  );
            *((uint32_t *)pfsentry+1) = *((uint32_t *)pcache+1);
            *((uint32_t *)pfsentry+2) = *((uint32_t *)pcache+2);
            *((uint32_t *)pfsentry+3) = *((uint32_t *)pcache+3);
            pfsentry += 16;
            pcache += 16;
        } while (pfsentry < pfseend);

        Mc1stCacheEntSetWrFlagOff();

        r = McFlushCache();
        if (r != sceMcResSucceed)
            return r;
    }

    return fd;
}

//--------------------------------------------------------------
int mcfat_chdir( const char *newdir, char *currentdir)
{
    register int r, len, len2, cluster;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McCacheDir cacheDir;
    McFsEntry *fse;

    DPRINTF("mcfat_chdir newdir %s\n", newdir);

    //if (!mcfat_checkpath(newdir))
    //	return sceMcResNoEntry;

    cacheDir.maxent = -1;

    r = mcfat_cachedirentry(newdir, &cacheDir, &fse, 1);
    if (r < 0)
        return r;

    if (((uint32_t)(r - 1)) < 2)
        return sceMcResNoEntry;

    mcdi->rootdir_cluster2 = cacheDir.cluster;
    mcdi->unknown1 = cacheDir.fsindex;

    cluster = cacheDir.cluster;
    if (!strcmp(fse->name, "..")) {
        r = McReadDirEntry(cluster, 0, &fse);
        if (r != sceMcResSucceed)
            return r;
    }

    if (!strcmp(fse->name, ".")) {
        mcdi->rootdir_cluster2 = fse->cluster;
        mcdi->unknown1 = fse->dir_entry;

        cluster = fse->cluster;
        r = McReadDirEntry(cluster, fse->dir_entry, &fse);
        if (r != sceMcResSucceed)
            return r;
    }

    currentdir[0] = 0;

lbl1:
    if (strcmp(fse->name, ".")) {

        if (strlen(fse->name) < 32)
            len = strlen(fse->name);
        else
            len = 32;

        if (strlen(currentdir)) {
            len2 = strlen(currentdir);
            if (len2 >= 0) {
                do {
                    currentdir[1 + len2 + len] = currentdir[len2];
                } while (--len2 >= 0);
            }
            currentdir[len] = '/';
            strncpy(currentdir, fse->name, len);
        }
        else {
            strncpy(currentdir, fse->name, 32);
            currentdir[32] = 0;
        }

        r = McReadDirEntry(cluster, 0, &fse);
        if (r != sceMcResSucceed)
            return r;

        r = McReadDirEntry(fse->cluster, fse->dir_entry, &fse);

        if (r == sceMcResSucceed)
            goto lbl1;
    }
    else {
        len = strlen(currentdir);

        if (len >= 0) {
            do {
                currentdir[1 + len] = currentdir[len];
            } while (--len >= 0);
        }
        currentdir[0] = '/';

        r = sceMcResSucceed;
    }

    return r;
}

//--------------------------------------------------------------
int mcfat_getdir2( const char *dirname, int flags, int maxent, sceMcTblGetDir *info)
{
    register int r, nument;
    register MCDevInfo *mcdi = &mcfat_devinfos;
    McFsEntry *fse;
    char *p;

    DPRINTF("mcfat_getdir2 dir=%s flags=%d maxent=%d\n", dirname, flags, maxent);

    nument = 0;
    flags &= 0xffff;

    if (!flags) {
        register int pos;

        p = mcfat_curdirpath;
        strncpy(p, dirname, 1023);
        mcfat_curdirpath[1023] = 0;

        pos = -1; 	// s1
        p++; 		//s0
        do {
            r = mcfat_chrpos((void *)&p[pos], '/');
            if (r < 0)
                break;
            pos += 1 + r;
        } while (1);

        if (pos <= 0) {
            if (pos == 0)
                *p = 0;
            else
                p[-1] = 0;
        }
        else {
            mcfat_curdirpath[pos] = 0;
        }

        mcfat_curdirname = &dirname[pos] + 1;

        r = mcfat_cachedirentry(mcfat_curdirpath, NULL, &fse, 1);
        if (r > 0)
            return sceMcResNoEntry;
        if (r < 0)
            return r;

        if (!(fse->mode & sceMcFileAttrSubdir)) {
            mcfat_curdircluster = -1;
            return sceMcResNoEntry;
        }

        mcfat_curdircluster = fse->cluster;
        mcfat_curdirlength = fse->length;

        if ((fse->cluster == mcdi->rootdir_cluster) && (fse->dir_entry == 0))
            mcfat_curdirmaxent = 2;
        else
            mcfat_curdirmaxent = 0;
    }
    else {
        if (mcfat_curdircluster < 0)
            return sceMcResNoEntry;
    }

    if (maxent != 0) {
        do {
            if (mcfat_curdirmaxent >= mcfat_curdirlength)
                break;

            r = McReadDirEntry(mcfat_curdircluster, mcfat_curdirmaxent, &fse);
            if (r != sceMcResSucceed)
                return r;

            mcfat_curdirmaxent++;

            if (!(fse->mode & sceMcFileAttrExists))
                continue;
            if (fse->mode & sceMcFileAttrHidden)
                continue;
            if (!mcfat_checkdirpath(fse->name, mcfat_curdirname))
                continue;

            mcfat_wmemset((void *)info, sizeof (sceMcTblGetDir), 0);

            if (mcfat_curdirmaxent == 2) {

                r = McReadDirEntry(mcfat_curdircluster, 0, &fse);
                if (r != sceMcResSucceed)
                    return r;

                r = McReadDirEntry(fse->cluster, 0, &fse);
                if (r != sceMcResSucceed)
                    return r;

                r = McReadDirEntry(fse->cluster, fse->dir_entry, &fse);
                if (r != sceMcResSucceed)
                    return r;

                info->EntryName[0] = '.';
                info->EntryName[1] = '.';
                info->EntryName[2] = '\0';
            }
            else if (mcfat_curdirmaxent == 1) {

                r = McReadDirEntry(mcfat_curdircluster, 0, &fse);
                if (r != sceMcResSucceed)
                    return r;

                r = McReadDirEntry(fse->cluster, fse->dir_entry, &fse);
                if (r != sceMcResSucceed)
                    return r;

                info->EntryName[0] = '.';
                info->EntryName[1] = '\0';
            }
            else {
                strncpy(info->EntryName, fse->name, 32);
            }

            info->AttrFile = fse->mode;
            info->Reserve1 = fse->unused;

            info->_Create = fse->created;
            info->_Modify = fse->modified;

            if (!(fse->mode & sceMcFileAttrSubdir))
                info->FileSizeByte = fse->length;

            nument++;
            maxent--;
            info++;

        } while (maxent);
    }

    return nument;
}

//--------------------------------------------------------------
int mcfat_delete2( const char *filename, int flags)
{
    register int r, i;
    McCacheDir cacheDir;
    McFsEntry *fse1, *fse2;

    DPRINTF("mcfat_delete2 filename %s flags %x\n", filename, flags);

    //if (!mcfat_checkpath(filename))
    //	return sceMcResNoEntry;

    r = mcfat_cachedirentry(filename, &cacheDir, &fse1, ((uint32_t)(flags < 1)) ? 1 : 0);
    if (r > 0)
        return sceMcResNoEntry;
    if (r < 0)
        return r;

    if (!flags) {
        if (!(fse1->mode & sceMcFileAttrExists))
            return sceMcResNoEntry;
    }
    else {
        if (fse1->mode & sceMcFileAttrExists)
            return sceMcResNoEntry;
    }

    if (!flags) {
        if (!(fse1->mode & sceMcFileAttrWriteable))
            return sceMcResDeniedPermit;
    }

    if ((!fse1->cluster) && (!fse1->dir_entry))
        return sceMcResNoEntry;

    i = 2;
    if ((!flags) && (fse1->mode & sceMcFileAttrSubdir) && ((uint32_t)i < fse1->length)) {

        do {
            r = McReadDirEntry(fse1->cluster, i, &fse2);
            if (r != sceMcResSucceed)
                return r;

            if (fse2->mode & sceMcFileAttrExists)
                return sceMcResNotEmpty;

        } while ((uint32_t)(++i) < fse1->length);
    }

    r = McSetDirEntryState(cacheDir.cluster, cacheDir.fsindex, flags);
    if (r != sceMcResSucceed)
        return r;

    r = McFlushCache();
    if (r != sceMcResSucceed)
        return r;

    return sceMcResSucceed;
}

//--------------------------------------------------------------
int mcfat_unformat2()
{
    register int r, i, j, z, l, pageword_cnt, page, blocks_on_card, erase_byte, err_cnt;
    register uint32_t erase_value;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    DPRINTF("mcfat_unformat2 slot%d\n");

    pageword_cnt = mcdi->pagesize >> 2;
    blocks_on_card = mcdi->clusters_per_card / mcdi->clusters_per_block; //sp18

    erase_value = 0xffffffff; //s6
    if (!(mcdi->cardflags & CF_ERASE_ZEROES))
        erase_value = 0x00000000;

    for (i = 0; i < pageword_cnt; i++)
        mcfat_pagebuf.word[i] = erase_value;

    for (i = 0; i < 128; i++)
        *((uint32_t *)&mcfat_eccdata + i) = erase_value;

    i = 1;
    if (i < blocks_on_card) {
        erase_byte = erase_value & 0xff;	// sp20
        do {
            page = i * mcdi->blocksize;
            if (mcdi->cardform > 0) {
                j = 0;
                for (j = 0; j < 16; j++) {
                    if (mcdi->bad_block_list[j] <= 0) {
                        j = 16;
                        goto lbl1;
                    }
                    if (mcdi->bad_block_list[j] == i)
                        goto lbl1;
                }
            }
            else {
                err_cnt = 0;
                j = -1;
                z = 0;
                if (mcdi->blocksize > 0) {
                    do {
                        r = McReadPage(page + z, &mcfat_pagebuf);
                        if (r == sceMcResNoFormat) {
                            j = -2;
                            break;
                        }
                        if (r != sceMcResSucceed)
                            return -42;

                        if ((mcdi->cardflags & CF_USE_ECC) == 0)	{
                            for (l = 0; l < mcdi->pagesize; l++) {
                                if (mcfat_pagebuf.byte[l] != erase_byte)
                                    err_cnt++;
                                if ((uint32_t)err_cnt >= (mcdi->clusters_per_block << 6)) {
                                    j = 16;
                                    break;
                                }
                            }
                            if (j != -1)
                                break;
                        }
                    } while (++z < mcdi->blocksize);
                }
            }

            if (((mcdi->cardflags & CF_USE_ECC) != 0) && (j == -1))
                j = 16;
lbl1:
            if (j == 16) {
                r = mcfat_eraseblock(i, NULL, NULL);
                if (r != sceMcResSucceed)
                    return -43;
            }
            else {
                for (l = 0; l < pageword_cnt; l++)
                    mcfat_pagebuf.word[l] = erase_value;

                if (mcdi->blocksize > 0) {
                    z = 0;
                    do {
                        r = McWritePage(page + z, &mcfat_pagebuf, mcfat_eccdata);
                        if (r != sceMcResSucceed)
                            return -44;
                    } while (++z < mcdi->blocksize);
                }
            }
        } while (++i < blocks_on_card);
    }

    r = mcfat_eraseblock(0, NULL, NULL);
    if (r != sceMcResSucceed)
        return -45;

    return sceMcResSucceed;
}

//--------------------------------------------------------------
