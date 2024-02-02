#ifndef __TYPES_H
#define __TYPES_H

#include <stdint.h>


typedef struct
{
    unsigned int mode;
    unsigned int attr;
    unsigned int size;
    unsigned char ctime[8];
    unsigned char atime[8];
    unsigned char mtime[8];
    unsigned int hisize;
} mcfat_stat_t;

typedef struct
{
    mcfat_stat_t stat;
    char name[256];
    void *privdata;
} mcfat_dirent_t;


typedef struct _mcfat_file {
	/** File open mode.  */
	int32_t	mode;		
	/** HW device unit number.  */
	int32_t	unit;		
	/** Device driver.  */
	struct _mcfat_device *device; 
	/** The device driver can use this however it wants.  */
	void	*privdata;	
} mcfat_file_t;

typedef struct _mcfat_device {
	const char *name;
	unsigned int type;
	/** Not so sure about this one.  */
	unsigned int version;
	const char *desc;
	struct _mcfat_device_ops *ops;
} mcfat_device_t;



typedef struct _mcfat_device_ops {
	int	(*init)(mcfat_device_t *);
	int	(*deinit)(mcfat_device_t *);
	int	(*format)(mcfat_file_t *);
	int	(*open)(mcfat_file_t *, const char *, int);
	int	(*close)(mcfat_file_t *);
	int	(*read)(mcfat_file_t *, void *, int);
	int	(*write)(mcfat_file_t *, void *, int);
	int	(*lseek)(mcfat_file_t *, int, int);
	int	(*ioctl)(mcfat_file_t *, int, void *);
	int	(*remove)(mcfat_file_t *, const char *);
	int	(*mkdir)(mcfat_file_t *, const char *);
	int	(*rmdir)(mcfat_file_t *, const char *);
	int	(*dopen)(mcfat_file_t *, const char *);
	int	(*dclose)(mcfat_file_t *);
	int	(*dread)(mcfat_file_t *, mcfat_dirent_t *);
	int	(*getstat)(mcfat_file_t *, const char *, mcfat_stat_t *);
	int	(*chstat)(mcfat_file_t *, const char *, mcfat_stat_t *, unsigned int);
	/* Extended ops start here.  */
	int	(*rename)(mcfat_file_t *, const char *, const char *);
	int	(*chdir)(mcfat_file_t *, const char *);
	int	(*sync)(mcfat_file_t *, const char *, int);
	int	(*mount)(mcfat_file_t *, const char *, const char *, int, void *, int);
	int	(*umount)(mcfat_file_t *, const char *);
	int64_t	(*lseek64)(mcfat_file_t *, int64_t, int);
	int	(*devctl)(mcfat_file_t *, const char *, int, void *, unsigned int, void *, unsigned int);
	int	(*symlink)(mcfat_file_t *, const char *, const char *);
	int	(*readlink)(mcfat_file_t *, const char *, char *, unsigned int);
	int	(*ioctl2)(mcfat_file_t *, int, void *, unsigned int, void *, unsigned int);

} mcfat_device_ops_t;

#endif