# MCFat Standalone

This library is hevaily based on MCMan from PS2SDK.
It provides access to PS2 Memory Card File Systems and should be portable to be integrated into different platforms.

## How To Use

Configure mcfat_datasource_info_t and mcfat_mcops_t with the correct values and access functions for your platform.

All public functions are available in *include/mcfat.h*

If configuration of sizes is required to be changed, compile definitions should be used:

- **MAX_FDHANDLES**: Maximum number of active file handles
- **MAX_CACHEDIRENTRY**: Maximum Cache DirEntries
- **MAX_CACHEENTRY**: Maximum number of entries in cache


***Please be aware that this is only a pretty early WiP***
