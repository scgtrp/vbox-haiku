#include "vboxsf.h"

#define MODULE_NAME "file_systems/vboxsf"
#define FS_NAME "vboxsf"

VBSFCLIENT g_clientHandle;
static fs_volume_ops vboxsf_volume_ops;
static fs_vnode_ops vboxsf_vnode_ops;

status_t init_module(void)
{
	if (get_module(VBOXGUEST_MODULE_NAME, (module_info **)&g_VBoxGuest) != B_OK) {
		dprintf("get_module(%s) failed\n", VBOXGUEST_MODULE_NAME);
		return B_ERROR;
	}
	
	if (RT_FAILURE(vboxInit())) {
		dprintf("vboxInit failed\n");
		return B_ERROR;
	}
	
	if (RT_FAILURE(vboxConnect(&g_clientHandle))) {
		dprintf("vboxConnect failed\n");
		return B_ERROR;
	}
	
	if (RT_FAILURE(vboxCallSetUtf8(&g_clientHandle))) {
		dprintf("vboxCallSetUtf8 failed\n");
		return B_ERROR;
	}
	
	dprintf(FS_NAME ": inited successfully\n");
	return B_OK;
}

void uninit_module(void)
{
	put_module(VBOXGUEST_MODULE_NAME);
}

PSHFLSTRING make_shflstring(const char* const s) {
	PSHFLSTRING rv = malloc(strlen(s) + 3);
	if (!rv) {
		return NULL;
	}
	int len = strlen(s);
	if (len > 0xFFFE) {
		dprintf(FS_NAME ": make_shflstring: string too long\n");
		free(rv);
		return NULL;
	}
	rv->u16Length = len;
    rv->u16Size = len + 1;
    strcpy(rv->String.utf8, s);
	return rv;
}

status_t mount(fs_volume *volume, const char *device, uint32 flags, const char *args, ino_t *_rootVnodeID) {
	if (device) {
		dprintf(FS_NAME ": trying to mount a real device as a vbox share is silly\n");
		return B_BAD_TYPE;
	}
	
	dprintf(FS_NAME ": mount(%s)\n", args);
	
	PSHFLSTRING sharename = make_shflstring(args);
	
	vboxsf_volume* vbsfvolume = malloc(sizeof(vboxsf_volume));
	volume->private_volume = vbsfvolume;
	int rv = vboxCallMapFolder(&g_clientHandle, sharename, &(vbsfvolume->map));
	free(sharename);
	
	if (rv == 0) {
		vboxsf_vnode* root_vnode;
		
		PSHFLSTRING name = make_shflstring(".");
		if (!name) {
			dprintf(FS_NAME ": make_shflstring() failed\n");
			return B_ERROR;
		}
		
		status_t rs = vboxsf_new_vnode(&vbsfvolume->map, name, &root_vnode);
		free(name);
		
		if (rs != B_OK) {
			dprintf(FS_NAME ": vboxsf_new_vnode() failed (%d)\n", rs);
			return rs;
		}
		
		dprintf(FS_NAME ": publish_vnode(): %d\n", publish_vnode(volume, root_vnode->vnode, vbsfvolume, &vboxsf_vnode_ops, S_IFDIR, 0));
		*_rootVnodeID = root_vnode->vnode;
		volume->ops = &vboxsf_volume_ops;
		return B_OK;
	}
	else {
		dprintf(FS_NAME ": vboxCallMapFolder failed (%d)\n", rv);
		kernel_debugger("8");
		free(volume->private_volume);
		return B_ERROR;
	}
}

status_t unmount(fs_volume *volume) {
	dprintf(FS_NAME ": unmount\n");
	vboxCallUnmapFolder(&g_clientHandle, volume->private_volume);
	return B_OK;
}
/*
status_t vboxsf_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode, int* _type, uint32* _flags, bool reenter) {
	dprintf("get_vnode %08x\n", id);
	vboxsf_volume* v = volume->private_volume;
	vboxsf_vnode* n = malloc(sizeof(vboxsf_vnode));
	vnode->private_node = n;
	n->map = &v->map;
	n->path = NULL;
	vnode->ops = &vboxsf_vnode_ops;
	return B_OK;
}

status_t vboxsf_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter) {
	dprintf("put_vnode %08x\n", vnode->private_node);
	free(vnode->private_node);
	return B_OK;
}*/

status_t read_stat(fs_volume* volume, fs_vnode* vnode, struct stat* st) {
	st->st_dev = 0;
	st->st_ino = (ino_t)(1);
	st->st_mode = S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO;
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = 0;
	st->st_blksize = 0;
	st->st_blocks = 0;
	st->st_atime = 0;
	st->st_mtime = 0;
	st->st_ctime = 0;
	return B_OK;
}

status_t open_dir(fs_volume* _volume, fs_vnode* _vnode, void** _cookie) {
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_vnode* vnode = _vnode->private_node;
	SHFLCREATEPARMS params;
	
	RT_ZERO(params);
	params.Handle = SHFL_HANDLE_NIL;
	params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS
		| SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;
	
	PSHFLSTRING path = make_shflstring("*"); // TODO put real path here
	int rc = vboxCallCreate(&g_clientHandle, &volume->map, path, &params);
	
	if (RT_SUCCESS(rc)) {
		if (params.Result == SHFL_FILE_EXISTS) {
			dprintf("wheeeee\n");
			vboxsf_dir_cookie* cookie = malloc(sizeof(vboxsf_dir_cookie));
			*_cookie = cookie;
			cookie->index = 0;
			cookie->path = path;
			cookie->handle = 42; // TODO
			return B_OK;
		}
	}
	else {
		dprintf(FS_NAME ": vboxCallCreate: %d\n", rc);
		return B_PERMISSION_DENIED;
	}
	
	return B_ERROR;
}

status_t read_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	struct dirent* buffer, size_t bufferSize, uint32* _num) {
	vboxsf_dir_cookie* cookie = _cookie;
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_vnode* vnode = _vnode->private_node;
	uint32 num_read = 0;
	status_t rv = B_OK;
	for (; *_num > 0; (*_num)--) {
		SHFLDIRINFO dir_buffer;
		uint32_t pcbBuffer = sizeof(dir_buffer);
		uint32_t count = 1;
		
		// TODO this can read more than one file at once, do that when needed
		int rc = vboxCallDirInfo(&g_clientHandle, &volume->map, cookie->handle, cookie->path,
			0, cookie->index++, &pcbBuffer, &dir_buffer, &count);
		
		if (!RT_SUCCESS(rc)) {
			if (rc == VERR_NO_MORE_FILES) {
				dprintf(FS_NAME ": no more files\n");
			}
			else {
				dprintf(FS_NAME ": vboxCallDirInfo failed: %d\n", rc);
			}
			break;
		}
		
		dprintf(FS_NAME ": read_dir: %s\n", dir_buffer.name.String.utf8);

		buffer[num_read].d_ino = 1; // TODO get a real vnode id
		strncpy(buffer[num_read].d_name, dir_buffer.name.String.utf8, NAME_MAX);
		
		num_read++;
	}
	
	*_num = num_read;
	return B_OK;
}

status_t free_dir_cookie(fs_volume* volume, fs_vnode* vnode, void* cookie) {
	free(cookie);
	
	return B_OK;
}

status_t read_fs_info(fs_volume* _volume, struct fs_info* info) {
	vboxsf_volume* volume = _volume->private_volume;
	
	SHFLVOLINFO volume_info;
	uint32_t bytes = sizeof(SHFLVOLINFO);

	int rc = vboxCallFSInfo(&g_clientHandle, &volume->map, 0,
		(SHFL_INFO_GET | SHFL_INFO_VOLUME), &bytes, (PSHFLDIRINFO)&volume_info);
	
	if (RT_FAILURE(rc)) {
		dprintf(FS_NAME ": vboxCallFSInfo failed (%d)\n", rc);
		return B_ERROR;
	}
	
	info->flags = (volume_info.fsProperties.fReadOnly? B_FS_IS_READONLY : 0);
	info->block_size = volume_info.ulBytesPerAllocationUnit;
	info->io_size = volume_info.ulBytesPerAllocationUnit;
	info->total_blocks = volume_info.ullTotalAllocationBytes / info->block_size;
	info->free_blocks = volume_info.ullAvailableAllocationBytes / info->block_size;
	info->total_nodes = LONGLONG_MAX;
	info->free_nodes = LONGLONG_MAX;
	strcpy(info->volume_name, "VBox share");
	return B_OK;
}

status_t lookup(fs_volume* volume, fs_vnode* dir, const char* name, ino_t* _id) {
	// TODO
	*_id = 1;
	return B_OK;
}

static status_t std_ops(int32 op, ...) {
	switch(op) {
	case B_MODULE_INIT:
		dprintf(MODULE_NAME ": B_MODULE_INIT\n");
		return init_module();
	case B_MODULE_UNINIT:
		dprintf(MODULE_NAME ": B_MODULE_UNINIT\n");
		uninit_module();
		return B_OK;
	default:
		return B_ERROR;
	}
}

static fs_volume_ops vboxsf_volume_ops = {
	unmount,
	
	read_fs_info, // read_fs_info
	NULL, // write_fs_info
	NULL, // sync

	vboxsf_get_vnode, // get_vnode

	NULL, // open_index_dir
	NULL, // close_index_dir
	NULL, // free_index_dir_cookie
	NULL, // read_index_dir
	NULL, // rewind_index_dir

	NULL, // create_index
	NULL, // remove_index
	NULL, // read_index_stat

	NULL, // open_query
	NULL, // close_query
	NULL, // free_query_cookie
	NULL, // read_query
	NULL, // rewind_query

	NULL, // all_layers_mounted
	NULL, // create_sub_vnode
	NULL, // delete_sub_vnode
};

static fs_vnode_ops vboxsf_vnode_ops = {
	lookup, // lookup
	NULL, // get_vnode_name
	vboxsf_put_vnode, // put_vnode
	NULL, // remove_vnode
	NULL, // can_page
	NULL, // read_pages
	NULL, // write_pages
	NULL, // io
	NULL, // cancel_io
	NULL, // get_file_map
	NULL, // ioctl
	NULL, // set_flags
	NULL, // select
	NULL, // deselect
	NULL, // fsync
	NULL, // read_symlink
	NULL, // create_symlink
	NULL, // link
	NULL, // unlink
	NULL, // rename
	NULL, // access
	read_stat, // read_stat
	NULL, // write_stat
	NULL, // preallocate
	NULL, // create
	NULL, // open
	NULL, // close
	NULL, // free_cookie
	NULL, // read
	NULL, // write
	NULL, // create_dir
	NULL, // remove_dir
	open_dir, // open_dir
	NULL, // close_dir
	free_dir_cookie, // free_dir_cookie
	read_dir, // read_dir
	NULL, // rewind_dir
	NULL, // open_attr_dir
	NULL, // close_attr_dir
	NULL, // free_attr_dir_cookie
	NULL, // read_attr_dir
	NULL, // rewind_attr_dir
	NULL, // create_attr
	NULL, // open_attr
	NULL, // close_attr
	NULL, // free_attr_cookie
	NULL, // read_attr
	NULL, // write_attr
	NULL, // read_attr_stat
	NULL, // write_attr_stat
	NULL, // rename_attr
	NULL, // remove_attr
	NULL, // create_special_node
	NULL, // get_super_vnode
};

static file_system_module_info sVBoxSharedFileSystem = {
	{
		MODULE_NAME B_CURRENT_FS_API_VERSION,
		0,
		std_ops,
	},

	FS_NAME,						// short_name
	"VirtualBox shared folders",	// pretty_name
	0, //B_DISK_SYSTEM_SUPPORTS_WRITING,	// DDM flags

	// scanning
	NULL, // identify_partition
	NULL, // scan_partition
	NULL, // free_identify_partition_cookie
	NULL,	// free_partition_content_cookie()

	mount,
};

module_info *modules[] = {
	(module_info *)&sVBoxSharedFileSystem,
	NULL,
};