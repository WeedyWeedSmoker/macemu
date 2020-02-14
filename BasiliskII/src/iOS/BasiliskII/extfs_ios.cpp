/*
 *  extfs_macosx.cpp - MacOS file system for access native file system access, MacOS X specific stuff
 *
 *  Basilisk II (C) 1997-2008 Christian Bauer
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/attr.h>
#include <sys/xattr.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <map>

#include "sysdeps.h"
#include "prefs.h"
#include "extfs.h"
#include "extfs_defs.h"
#include "extfs_ios_helper.h"

// XXX: don't clobber with native definitions
#define noErr	native_noErr
#define Point	native_Point
#define Rect	native_Rect
#define ProcPtr	native_ProcPtr
# include <CoreFoundation/CFString.h>
#undef ProcPtr
#undef Rect
#undef Point
#undef noErr

#define DEBUG 0
#include "debug.h"


// Default Finder flags
const uint16 DEFAULT_FINDER_FLAGS = kHasBeenInited;

#define XATTR_FINFO  "org.BasiliskII.FinderInfo"
#define XATTR_FXINFO "org.BasiliskII.ExtendedFinderInfo"

// Open resource fork handles
std::map<int, char*> rfork_handles;

/*
 *  Initialization
 */

void extfs_init(void)
{
}


/*
 *  Deinitialization
 */

void extfs_exit(void)
{
    // write out resource forks that are still open
    while (!rfork_handles.empty()) {
        std::pair<int, char*> handle = *(rfork_handles.begin());
        close_rfork(handle.second, handle.first);
    }
}


/*
 *  Add component to path name
 */

void add_path_component(char *path, const char *component)
{
	int l = strlen(path);
	if (l < MAX_PATH_LENGTH-1 && path[l-1] != '/') {
		path[l] = '/';
		path[l+1] = 0;
	}
	strncat(path, component, MAX_PATH_LENGTH-1);
}


/*
 *  Finder info manipulation helpers
 */

typedef uint8 FinderInfo[SIZEOF_FInfo];

struct FinderInfoAttrBuf {
    uint32 length;
    FinderInfo finderInfo;
    FinderInfo extendedFinderInfo;
};

static const FinderInfo kNativeFInfoMask  = {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00};
static const FinderInfo kNativeFXInfoMask = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00};
static const FinderInfo kNativeDInfoMask  = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00};
static const FinderInfo kNativeDXInfoMask = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00}; /* XXX: keep frScroll? */

static void finfo_merge(FinderInfo dst, const FinderInfo emu, const FinderInfo nat, const FinderInfo mask)
{
	for (int i = 0; i < SIZEOF_FInfo; i++)
		dst[i] = (emu[i] & ~mask[i]) | (nat[i] & mask[i]);
}

static void finfo_split(FinderInfo dst, const FinderInfo emu, const FinderInfo mask)
{
	for (int i = 0; i < SIZEOF_FInfo; i++)
		dst[i] = emu[i] & mask[i];
}

/*
 *  Get/set finder info for file/directory specified by full path
 */

// Get emulated Finder info from metadata
static bool get_finfo_from_xattr(const char *path, uint8 *finfo, uint8 *fxinfo)
{
    
	if (!getxattr(path, XATTR_FINFO, finfo, SIZEOF_FInfo, 0 ,0))
		return false;
	if (fxinfo && !getxattr(path, XATTR_FXINFO, fxinfo, SIZEOF_FXInfo, 0, 0))
		return false;
	return true;
}

// Get native Finder info
static bool get_finfo_from_native(const char *path, uint8 *finfo, uint8 *fxinfo)
{
    struct attrlist attrList;
    memset(&attrList, 0, sizeof(attrList));
    attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrList.commonattr  = ATTR_CMN_FNDRINFO;
    
    FinderInfoAttrBuf attrBuf;
    if (getattrlist(path, &attrList, &attrBuf, sizeof(attrBuf), 0) < 0)
        return false;
    
    memcpy(finfo, attrBuf.finderInfo, SIZEOF_FInfo);
    if (fxinfo)
        memcpy(fxinfo, attrBuf.extendedFinderInfo, SIZEOF_FXInfo);
    return true;
}

static bool do_get_finfo(const char *path, bool has_fxinfo,
						 FinderInfo emu_finfo, FinderInfo emu_fxinfo,
						 FinderInfo nat_finfo, FinderInfo nat_fxinfo)
{
	memset(emu_finfo, 0, SIZEOF_FInfo);
	if (has_fxinfo)
		memset(emu_fxinfo, 0, SIZEOF_FXInfo);
	*((uint16 *)(emu_finfo + fdFlags)) = htons(DEFAULT_FINDER_FLAGS);
	*((uint32 *)(emu_finfo + fdLocation)) = htonl((uint32)-1);

	get_finfo_from_xattr(path, emu_finfo, has_fxinfo ? emu_fxinfo : NULL);

	if (!get_finfo_from_native(path, nat_finfo, has_fxinfo ? nat_fxinfo : NULL))
		return false;
    
	return true;
}

void get_finfo(const char *path, uint32 finfo, uint32 fxinfo, bool is_dir)
{
	// Set default finder info
	Mac_memset(finfo, 0, SIZEOF_FInfo);
	if (fxinfo)
		Mac_memset(fxinfo, 0, SIZEOF_FXInfo);
	WriteMacInt16(finfo + fdFlags, DEFAULT_FINDER_FLAGS);
	WriteMacInt32(finfo + fdLocation, (uint32)-1);

	// Merge emulated and native Finder info
	FinderInfo emu_finfo, emu_fxinfo;
	FinderInfo nat_finfo, nat_fxinfo;
	if (do_get_finfo(path, fxinfo, emu_finfo, emu_fxinfo, nat_finfo, nat_fxinfo)) {
		if (!is_dir) {
			finfo_merge(Mac2HostAddr(finfo), emu_finfo, nat_finfo, kNativeFInfoMask);
            if (ShouldHideExtFSFile(path))
                *((uint16 *)(Mac2HostAddr(finfo) + fdFlags)) |= htons(kHasBeenInited|kIsInvisible|kNameLocked);
			if (fxinfo)
				finfo_merge(Mac2HostAddr(fxinfo), emu_fxinfo, nat_fxinfo, kNativeFXInfoMask);
			if (ReadMacInt32(finfo + fdType) != 0 && ReadMacInt32(finfo + fdCreator) != 0)
				return;
		} else {
			finfo_merge(Mac2HostAddr(finfo), emu_finfo, nat_finfo, kNativeDInfoMask);
            if (ShouldHideExtFSFile(path))
                *((uint16 *)(Mac2HostAddr(finfo) + fdFlags)) |= htons(kHasBeenInited|kIsInvisible|kNameLocked);
			if (fxinfo)
				finfo_merge(Mac2HostAddr(fxinfo), emu_fxinfo, nat_fxinfo, kNativeDXInfoMask);
			return;
		}
	}

	// No native Finder info, translate file name extension to MacOS type/creator
	if (!is_dir) {
        uint32 type, creator;
        if (GetTypeAndCreatorForFileName(path, &type, &creator)) {
            WriteMacInt32(finfo + fdType, type);
            WriteMacInt32(finfo + fdCreator, creator);
        }
    }
    if (ShouldHideExtFSFile(path)) {
        *((uint16 *)(Mac2HostAddr(finfo) + fdFlags)) |= htons(kHasBeenInited|kIsInvisible|kNameLocked);
    }
}

// Set emulated Finder info into metada
static bool set_finfo_to_xattr(const char *path, const uint8 *finfo, const uint8 *fxinfo)
{
    if (setxattr(path, XATTR_FINFO, finfo, SIZEOF_FInfo, 0, 0) < 0) {
		return false;
    }
    if (fxinfo && setxattr(path, XATTR_FXINFO, fxinfo, SIZEOF_FXInfo, 0, 0) < 0) {
		return false;
    }
	return true;
}

// Set native Finder info
static bool set_finfo_to_native(const char *path, const uint8 *finfo, const uint8 *fxinfo, bool is_dir)
{
    struct attrlist attrList;
    memset(&attrList, 0, sizeof(attrList));
    attrList.bitmapcount = ATTR_BIT_MAP_COUNT;
    attrList.commonattr  = ATTR_CMN_FNDRINFO;
    
    FinderInfoAttrBuf attrBuf;
    if (getattrlist(path, &attrList, &attrBuf, sizeof(attrBuf), 0) < 0)
        return false;
    
    finfo_merge(attrBuf.finderInfo, attrBuf.finderInfo, finfo, is_dir ? kNativeDInfoMask : kNativeFInfoMask);
    if (fxinfo)
        finfo_merge(attrBuf.extendedFinderInfo, attrBuf.extendedFinderInfo, fxinfo, is_dir ? kNativeDXInfoMask : kNativeFXInfoMask);
    
    attrList.commonattr = ATTR_CMN_FNDRINFO;
    if (setattrlist(path, &attrList, attrBuf.finderInfo, 2 * SIZEOF_FInfo, 0) < 0)
        return false;
    return true;
}

void set_finfo(const char *path, uint32 finfo, uint32 fxinfo, bool is_dir)
{
	// Extract native Finder info flags
	FinderInfo nat_finfo, nat_fxinfo;
	const uint8 *emu_finfo = Mac2HostAddr(finfo);
	const uint8 *emu_fxinfo = fxinfo ? Mac2HostAddr(fxinfo) : NULL;
	finfo_split(nat_finfo, emu_finfo, is_dir ? kNativeDInfoMask : kNativeFInfoMask);
	if (fxinfo)
		finfo_split(nat_fxinfo, emu_fxinfo, is_dir ? kNativeDXInfoMask : kNativeFXInfoMask);

	// Update Finder info file (all flags)
	set_finfo_to_xattr(path, emu_finfo, emu_fxinfo);

	// Update native Finder info flags
	set_finfo_to_native(path, nat_finfo, nat_fxinfo, is_dir);
}


/*
 *  Resource fork emulation functions
 */

uint32 get_rfork_size(const char *path)
{
    ssize_t size = getxattr(path, XATTR_RESOURCEFORK_NAME, NULL, 0, 0, 0);
    return size < 0 ? 0 : (uint32)size;
}

int open_rfork(const char *path, int flag)
{
    // Open original file
    int fd = open(path, flag);
    if (fd < 0) {
        return -1;
    }
    close(fd);
    
    // Open temporary file for resource fork
    char *tmpdir = getenv("TMPDIR");
    if (tmpdir == NULL) {
        tmpdir = strdup(getAltTempDirectory());
    }
    size_t rname_len = strlen(tmpdir) + 1 + 36 + 4 + 1;
    char rname[rname_len];
    strcpy(rname, tmpdir);
    if (rname[strlen(rname)-1] != '/') {
        strcat(rname, "/");
    }
    uuid_t uuid;
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, &rname[strlen(rname)]);
    strcat(rname, ".rsrc");
    int rfd = open(rname, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (rfd < 0) {
        return -1;
    }
    unlink(rname);	// File will be deleted when closed
    
    // Get size of resource fork attribute
    ssize_t resSize = get_rfork_size(path);
    
    // Copy resource data from attribute to temporary file
    if (resSize > 0) {
        // Allocate buffer
        void *attrBuf = malloc(resSize);
        if (attrBuf == NULL) {
            close(rfd);
            return -1;
        }
        
        // Copy data
        getxattr(path, XATTR_RESOURCEFORK_NAME, attrBuf, resSize, 0, 0);
        write(rfd, attrBuf, resSize);
        lseek(rfd, 0, SEEK_SET);
        
        // Free buffer
        free(attrBuf);
    }
    
    rfork_handles[rfd] = strdup(path);
    return rfd;
}

void close_rfork(const char *path, int fd)
{
    if (fd < 0) {
        return;
    }
    
    // Get size of temporary file
    ssize_t resSize = (ssize_t)lseek(fd, 0, SEEK_END);
    if (resSize < 0) {
        close(fd);
        free(rfork_handles[fd]);
        rfork_handles.erase(fd);
        return;
    }
    
    // Copy resource data to extended attribute
    if (resSize == 0) {
        setxattr(path, XATTR_RESOURCEFORK_NAME, NULL, 0, 0, 0);
    } else {
        // Allocate buffer
        void *attrBuf = malloc(resSize);
        if (attrBuf == NULL) {
            close(fd);
            free(rfork_handles[fd]);
            rfork_handles.erase(fd);
            return;
        }
        
        // Copy data
        lseek(fd, 0, SEEK_SET);
        read(fd, attrBuf, resSize);
        setxattr(path, XATTR_RESOURCEFORK_NAME, attrBuf, resSize, 0, 0);
        
        // Free buffer
        free(attrBuf);
    }
    
    // close file
    close(fd);
    // remove from open handles
    free(rfork_handles[fd]);
    rfork_handles.erase(fd);
}

/*
 *  Read "length" bytes from file to "buffer",
 *  returns number of bytes read (or -1 on error)
 */

ssize_t extfs_read(int fd, void *buffer, size_t length)
{
	return read(fd, buffer, length);
}


/*
 *  Write "length" bytes from "buffer" to file,
 *  returns number of bytes written (or -1 on error)
 */

ssize_t extfs_write(int fd, void *buffer, size_t length)
{
	return write(fd, buffer, length);
}


/*
 *  Remove file/directory,
 *  returns false on error (and sets errno)
 */

bool extfs_remove(const char *path)
{
	if (remove(path) < 0) {
		if (errno == EISDIR) {
			return rmdir(path) == 0;
		} else
			return false;
	}
	return true;
}


/*
 *  Rename/move file/directory
 *  returns false on error (and sets errno)
 */

bool extfs_rename(const char *old_path, const char *new_path)
{
	return rename(old_path, new_path) == 0;
}


/*
 *  Strings (filenames) conversion
 */

// Convert string in the specified source and target encodings
const char *convert_string(const char *str, CFStringEncoding from, CFStringEncoding to)
{
	const char *ostr = str;
	CFStringRef cfstr = CFStringCreateWithCString(NULL, str, from);
	if (cfstr) {
		static char buffer[MAX_PATH_LENGTH];
		memset(buffer, 0, sizeof(buffer));
		if (CFStringGetCString(cfstr, buffer, sizeof(buffer), to))
			ostr = buffer;
		CFRelease(cfstr);
	}
	return ostr;
}

// Convert from the host OS filename encoding to MacRoman
const char *host_encoding_to_macroman(const char *filename)
{
	return convert_string(filename, kCFStringEncodingUTF8, kCFStringEncodingMacRoman);
}

// Convert from MacRoman to host OS filename encoding
const char *macroman_to_host_encoding(const char *filename)
{
	return convert_string(filename, kCFStringEncodingMacRoman, kCFStringEncodingUTF8);
}
