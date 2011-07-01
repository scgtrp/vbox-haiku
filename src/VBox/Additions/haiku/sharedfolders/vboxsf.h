#ifndef ___vboxsf_h
#define ___vboxsf_h

#include <malloc.h>
#include <dirent.h>
#include <fs_info.h>
#include <sys/stat.h>
#include <fs_interface.h>
#include <KernelExport.h>
#include <VBoxGuest-haiku.h>
#include <VBoxGuestR0LibSharedFolders.h>
#include <lock.h>

typedef struct vboxsf_volume {
	VBSFMAP map;
	ino_t rootid;
} vboxsf_volume;

typedef struct vboxsf_vnode {
	PVBSFMAP map;
	PSHFLSTRING name;
	PSHFLSTRING path;
	
	ino_t vnode;
	struct vboxsf_vnode* next;
} vboxsf_vnode;

typedef struct vboxsf_dir_cookie {
	SHFLHANDLE handle;
	PSHFLSTRING path;
	uint32_t index;
	bool has_more_files;
	PSHFLDIRINFO buffer_start, buffer;
	uint32_t buffer_length, num_files;
} vboxsf_dir_cookie;

typedef struct vboxsf_file_cookie {
	SHFLHANDLE handle;
	PSHFLSTRING path;
} vboxsf_file_cookie;

#ifdef __cplusplus
extern "C" {
#endif
status_t vboxsf_new_vnode(PVBSFMAP map, PSHFLSTRING path, PSHFLSTRING name, vboxsf_vnode** p);
status_t vboxsf_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode, int* _type, uint32* _flags, bool reenter);
status_t vboxsf_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter);
PSHFLSTRING make_shflstring(const char* const s);
mode_t mode_from_fmode(RTFMODE fMode);
status_t vbox_err_to_haiku_err(int rc);
extern mutex g_vnodeCacheLock;
#ifdef __cplusplus
}
#endif

#endif