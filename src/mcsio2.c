/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# Copyright (c) 2009 jimmikaelkael
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
*/

#include <stdint.h>
#include <string.h>

#include "mcfat-internal.h"

int mcfat_eraseblock( int block, void **pagebuf, void *eccbuf)
{
    register int retries, size, ecc_offset;
    int page;

    void *p_ecc;
    register MCDevInfo *mcdi = &mcfat_devinfos;

    page = block * mcdi->blocksize;

    retries = 0;
    do {
        // Erase page
		if (!mcops.page_erase(&mcdsi, page))
			break;
    } while (++retries < 5);

    if (retries >= 5)
        return sceMcResChangedCard;

    if (pagebuf && eccbuf) { // This part leave the first ecc byte of each block page in eccbuf
        mcfat_wmemset(eccbuf, 32, 0);

        page = 0;
        while (page < mcdi->blocksize) {
            ecc_offset = page * mcdi->pagesize;
            if (ecc_offset < 0)
                ecc_offset += 0x1f;
            ecc_offset = ecc_offset >> 5;
            p_ecc = (void *)((uint8_t *)eccbuf + ecc_offset);
            size = 0;
            while (size < mcdi->pagesize)	{
                if (*pagebuf)
                    McDataChecksum((void *)((uint8_t *)(*pagebuf) + size), p_ecc);
                size += 128;
                p_ecc = (void *)((uint8_t *)p_ecc + 3);
            }
            pagebuf++;
            page++;
        }
    }

    return sceMcResSucceed;
}

int McWritePage( int page, void *pagebuf, void *eccbuf) // Export #19
{
    register int retries;

    char page_buf[528];
    memcpy(page_buf, pagebuf, 512);
    memcpy(page_buf + 512, eccbuf, 16);

    retries = 0;

    do {
        // Write Page
        if (mcops.page_write(&mcdsi, page, page_buf))
        	continue;
        return sceMcResSucceed;

    } while (++retries < 5);

    return sceMcResFailReplace;
}

int mcfat_readpage( int page, void *buf, void *eccbuf)
{
    // No retry logic here.
    char page_buf[528];
    // Read Page
    if (!mcops.page_read(&mcdsi, page, 1, page_buf))
    {
    	if (buf)
    	{
    		memcpy(buf, page_buf, 512);
    	}
    	if (eccbuf)
    	{
    		memcpy(eccbuf, page_buf + 512, 16);
    	}
    	return sceMcResSucceed;
    }
    
    DPRINTF("Error Reading Page\n");
    return sceMcResChangedCard;
}

int McGetCardSpec( int16_t *pagesize, uint16_t *blocksize, int *cardsize, uint8_t *flags)
{

    // Parse superblock
    mcops.info(&mcdsi);
    *pagesize = 512; /* Yes, this is hardcoded to 512 bytes */
    *blocksize = mcdsi.block_pages;
    *cardsize = mcdsi.blocks * mcdsi.block_pages;
    *flags = 0x22 | CF_BAD_BLOCK;
    if (mcdsi.page_bytes != 512)
    	*flags |= CF_USE_ECC;

    DPRINTF("McGetCardSpec sio2cmd pagesize=%d blocksize=%u cardsize=%d flags%x\n", *pagesize, *blocksize, *cardsize, *flags);

    return sceMcResSucceed;
}

int mcfat_probePS2Card2()
{
    if (!McGetFormat())
    {
        mcfat_probePS2Card();
    }
    if (McGetFormat() > 0)
        return sceMcResSucceed;
    if (McGetFormat() < 0)
        return sceMcResNoFormat;
        
    return sceMcResFailDetect2;
}

int mcfat_probePS2Card() //2
{
    register int r;

    DPRINTF("mcfat_probePS2Card sio2cmd \n");

    r = McGetFormat();
    if (r > 0)
    {
        DPRINTF("mcfat_probePS2Card sio2cmd succeeded\n");

        return sceMcResSucceed;
    }
    else if (r < 0)
    {
        DPRINTF("mcfat_probePS2Card sio2cmd failed (no format)\n");

        return sceMcResNoFormat;
    }
    mcfat_clearcache();

    r = mcfat_setdevinfos();
    if (r == 0) {
        DPRINTF("mcfat_probePS2Card sio2cmd card changed!\n");
        return sceMcResChangedCard;
    }
    if (r != sceMcResNoFormat) {
        DPRINTF("mcfat_probePS2Card sio2cmd failed (mc detection failed)\n");
        return sceMcResFailDetect2;
    }

    DPRINTF("mcfat_probePS2Card sio2cmd succeeded\n");

    return r;
}



