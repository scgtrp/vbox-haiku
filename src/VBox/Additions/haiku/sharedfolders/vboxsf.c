#include "vboxsf.h"

#define MODULE_NAME "file_systems/vboxsf"
#define FS_NAME "vboxsf"

VBSFCLIENT g_clientHandle;
static fs_volume_ops vboxsf_volume_ops;
static fs_vnode_ops vboxsf_vnode_ops;

#define TRACE dprintf(FS_NAME ": trace: \e[1m%s\e[0m\n", __FUNCTION__);

status_t init_module(void)
{
	TRACE
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
	TRACE
	put_module(VBOXGUEST_MODULE_NAME);
}

PSHFLSTRING make_shflstring(const char* const s) {
	TRACE
	int len = strlen(s);
	if (len > 0xFFFE) {
		dprintf(FS_NAME ": make_shflstring: string too long\n");
		return NULL;
	}
	
	PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + len);
	if (!rv) {
		return NULL;
	}
	
	rv->u16Length = len;
    rv->u16Size = len + 1;
    strcpy(rv->String.utf8, s);
	return rv;
}

PSHFLSTRING clone_shflstring(PSHFLSTRING s) {
	TRACE
	PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + s->u16Length);
	if (rv)
		memcpy(rv, s, sizeof(SHFLSTRING) + s->u16Length);
	return rv;
}

PSHFLSTRING concat_shflstring_cstr(PSHFLSTRING s1, const char* const s2) {
	TRACE
	size_t s2len = strlen(s2);
	PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + s1->u16Length + s2len);
	if (rv) {
		memcpy(rv, s1, sizeof(SHFLSTRING) + s1->u16Length);
		strcat(rv->String.utf8, s2);
		rv->u16Length += s2len;
		rv->u16Size += s2len;
	}
	return rv;
}

PSHFLSTRING concat_cstr_shflstring(const char* const s1, PSHFLSTRING s2) {
	TRACE
	size_t s1len = strlen(s1);
	PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + s1len + s2->u16Length);
	if (rv) {
		strcpy(rv->String.utf8, s1);
		strcat(rv->String.utf8, s2->String.utf8);
		rv->u16Length = s1len + s2->u16Length;
		rv->u16Size = rv->u16Length + 1;
	}
	return rv;
}

PSHFLSTRING build_path(vboxsf_vnode* dir, const char* const name) {
	TRACE
	
	dprintf("*** build_path(%p, %p)\n", dir, name);
	if (!dir || !name)
		return NULL;
	
	size_t len = dir->path->u16Length + strlen(name) + 1;
	
	PSHFLSTRING rv = malloc(sizeof(SHFLSTRING) + len);
	if (rv) {
		strcpy(rv->String.utf8, dir->path->String.utf8);
		strcat(rv->String.utf8, "/");
		strcat(rv->String.utf8, name);
		rv->u16Length = len;
		rv->u16Size = rv->u16Length + 1;
	}
	return rv;
}

status_t mount(fs_volume *volume, const char *device, uint32 flags, const char *args, ino_t *_rootVnodeID) {
	TRACE
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
		
		PSHFLSTRING name = make_shflstring("");
		if (!name) {
			dprintf(FS_NAME ": make_shflstring() failed\n");
			return B_ERROR;
		}
		
		status_t rs = vboxsf_new_vnode(&vbsfvolume->map, name, name, &root_vnode);
		dprintf(FS_NAME ": allocated %p (path=%p name=%p)\n", root_vnode, root_vnode->path, root_vnode->name);
		
		if (rs != B_OK) {
			dprintf(FS_NAME ": vboxsf_new_vnode() failed (%d)\n", rs);
			return rs;
		}
		
		rs = publish_vnode(volume, root_vnode->vnode, root_vnode, &vboxsf_vnode_ops, S_IFDIR, 0);
		dprintf(FS_NAME ": publish_vnode(): %d\n", rs);
		*_rootVnodeID = root_vnode->vnode;
		volume->ops = &vboxsf_volume_ops;
		return B_OK;
	}
	else {
		dprintf(FS_NAME ": vboxCallMapFolder failed (%d)\n", rv);
		free(volume->private_volume);
		return B_ERROR;
	}
}

status_t unmount(fs_volume *volume) {
	TRACE
	dprintf(FS_NAME ": unmount\n");
	vboxCallUnmapFolder(&g_clientHandle, volume->private_volume);
	return B_OK;
}

status_t vboxsf_read_stat(fs_volume* _volume, fs_vnode* _vnode, struct stat* st) {
	TRACE
	vboxsf_vnode* vnode = _vnode->private_node;
	vboxsf_volume* volume = _volume->private_volume;
	SHFLCREATEPARMS params;
	int rc;
	
	dprintf("vboxsf_read_stat (_vnode=%p, vnode=%p, path=%p (%s))\n", _vnode, vnode, vnode->path->String.utf8, vnode->path->String.utf8);
	
	params.Handle = SHFL_HANDLE_NIL;
	params.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;
	dprintf("sf_stat: calling vboxCallCreate, file %s, flags %x\n", vnode->path->String.utf8, params.CreateFlags);
	rc = vboxCallCreate(&g_clientHandle, &volume->map, vnode->path, &params);
	if (rc == VERR_INVALID_NAME)
	{
		/* this can happen for names like 'foo*' on a Windows host */
		return B_ENTRY_NOT_FOUND;
	}
	if (RT_FAILURE(rc))
	{
		dprintf("vboxCallCreate: %d\n", params.Result);
		return B_ERROR;
	}
	if (params.Result != SHFL_FILE_EXISTS)
	{
		dprintf("vboxCallCreate: %d\n", params.Result);
		return B_ENTRY_NOT_FOUND;
	}
	
	st->st_dev = 0;
	st->st_ino = vnode->vnode;
	st->st_mode = mode_from_fmode(params.Info.Attr.fMode);
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = 0;
	st->st_size = params.Info.cbObject;
	st->st_blksize = 1;
	st->st_blocks = params.Info.cbAllocated;
	st->st_atime = RTTimeSpecGetSeconds(&params.Info.AccessTime);
	st->st_mtime = RTTimeSpecGetSeconds(&params.Info.ModificationTime);
	st->st_ctime = RTTimeSpecGetSeconds(&params.Info.BirthTime);
	return B_OK;
}

status_t vboxsf_open_dir(fs_volume* _volume, fs_vnode* _vnode, void** _cookie) {
	TRACE
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_vnode* vnode = _vnode->private_node;
	SHFLCREATEPARMS params;
	
	RT_ZERO(params);
	params.Handle = SHFL_HANDLE_NIL;
	params.CreateFlags = SHFL_CF_DIRECTORY | SHFL_CF_ACT_OPEN_IF_EXISTS
		| SHFL_CF_ACT_FAIL_IF_NEW | SHFL_CF_ACCESS_READ;
	
	int rc = vboxCallCreate(&g_clientHandle, &volume->map, vnode->path, &params);
	
	if (RT_SUCCESS(rc)) {
		if (params.Result == SHFL_FILE_EXISTS && params.Handle != SHFL_HANDLE_NIL) {
			vboxsf_dir_cookie* cookie = malloc(sizeof(vboxsf_dir_cookie));
			*_cookie = cookie;
			cookie->index = 0;
			cookie->path = build_path(vnode, "*");
			cookie->handle = params.Handle;
			cookie->has_more_files = true;
			return B_OK;
		}
		else {
			return B_FILE_NOT_FOUND;
		}
	}
	else {
		dprintf(FS_NAME ": vboxCallCreate: %d\n", rc);
		return B_ERROR;
	}
}

status_t vboxsf_read_dir(fs_volume* _volume, fs_vnode* _vnode, void* _cookie,
	struct dirent* buffer, size_t bufferSize, uint32* _num) {
	TRACE
	vboxsf_dir_cookie* cookie = _cookie;
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_vnode* vnode = _vnode->private_node;
	uint32 num_read = 0;
	status_t rv = B_OK;
	
	if (cookie->has_more_files) {
		for (;;) {
			uint32_t cbBuffer = 16384;
			PSHFLDIRINFO dir_buffer = malloc(cbBuffer);
			uint32_t count;
			
			int rc = vboxCallDirInfo(&g_clientHandle, &volume->map, cookie->handle, cookie->path,
				0, cookie->index, &cbBuffer, dir_buffer, &count);
			if (rc != 0 && rc != VERR_NO_MORE_FILES) {
				dprintf(FS_NAME ": vboxCallDirInfo failed: %d\n", rc);
				rv = B_ERROR;
				break;
			}
			
			dprintf(FS_NAME ": vboxsf_read_dir: got %d files\n", count);
			
			PSHFLDIRINFO this_entry = dir_buffer;
			uint32_t i;
			for (i = 0; i < count; i++) {
				dprintf(FS_NAME ": vboxsf_read_dir: file %d: %s\n", i, this_entry->name.String.utf8);
				PSHFLSTRING name1 = clone_shflstring(&this_entry->name);
				if (!name1) {
					dprintf(FS_NAME ": make_shflstring() failed\n");
					return B_ERROR;
				}
				
				vboxsf_vnode* new_vnode;
				rv = vboxsf_new_vnode(&volume->map, build_path(vnode, name1->String.utf8), name1, &new_vnode);
				if (rv != B_OK) {
					dprintf(FS_NAME ": vboxsf_new_vnode() failed\n");
					return rv;
				}
				
				if (bufferSize <= sizeof(struct dirent) + this_entry->name.u16Length) {
					dprintf("hit end of buffer\n");
					goto end;
				}
				
				buffer->d_dev = 0;
				buffer->d_pdev = 0;
				buffer->d_ino = new_vnode->vnode;
				buffer->d_pino = vnode->vnode;
				buffer->d_reclen = sizeof(struct dirent) + this_entry->name.u16Length;
				strncpy(buffer->d_name, this_entry->name.String.utf8, NAME_MAX);
				
				num_read++;
				if (num_read >= *_num) {
					dprintf("read enough files\n");
					goto end;
				}
				
				bufferSize -= buffer->d_reclen;
				buffer = ((void*)(buffer)) + buffer->d_reclen;
				
				size_t size = offsetof(SHFLDIRINFO, name.String) + this_entry->name.u16Size;
				this_entry = ((void*)this_entry + size);
			}
			
			if (rc == VERR_NO_MORE_FILES) {
				cookie->has_more_files = false;
				break;
			}
		}
	}
	
end:
	*_num = num_read;
	dprintf("vboxsf_read_dir: read %d\n", *_num);
	return rv;
}

status_t vboxsf_free_dir_cookie(fs_volume* _volume, fs_vnode* vnode, void* _cookie) {
	TRACE
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_dir_cookie* cookie = _cookie;
	
	vboxCallClose(&g_clientHandle, &volume->map, cookie->handle);
	free(cookie->path);
	free(cookie);
	
	return B_OK;
}

status_t vboxsf_read_fs_info(fs_volume* _volume, struct fs_info* info) {
	TRACE
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

status_t vboxsf_lookup(fs_volume* _volume, fs_vnode* dir, const char* name, ino_t* _id) {
	TRACE
	dprintf(FS_NAME ": lookup %s\n", name);
	vboxsf_volume* volume = _volume->private_volume;
	SHFLCREATEPARMS params;
	
	RT_ZERO(params);
	params.Handle = SHFL_HANDLE_NIL;
	params.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;
	
	dprintf("dir=%p name=%p\n", dir, name);
	PSHFLSTRING path = build_path(dir->private_node, name);
	if (!path) {
		dprintf(FS_NAME ": make_shflstring() failed\n");
		return B_ERROR;
	}
	int rc = vboxCallCreate(&g_clientHandle, &volume->map, path, &params);
	
	if (RT_SUCCESS(rc)) {
		if (params.Result == SHFL_FILE_EXISTS) {
			vboxsf_vnode* vn;
			status_t rv = vboxsf_new_vnode(&volume->map, path, path, &vn);
			if (rv == B_OK) {
				*_id = vn->vnode;
				rv = publish_vnode(_volume, vn->vnode, vn, &vboxsf_vnode_ops, mode_from_fmode(params.Info.Attr.fMode), 0);
			}
			dprintf("vboxsf_lookup: returning %d\n", rv);
			return rv;
		}
		else {
			free(path);
			dprintf("vboxsf_lookup: file not found\n");
			return B_FILE_NOT_FOUND;
		}
	}
	else {
		free(path);
		dprintf(FS_NAME ": vboxCallCreate: %d\n", rc);
		return B_ERROR;
	}

	*_id = 1;
	return B_OK;
}

mode_t mode_from_fmode(RTFMODE fMode) {
	mode_t m = 0;

	if (RTFS_IS_DIRECTORY(fMode))
		m |= S_IFDIR;
	else if (RTFS_IS_FILE(fMode))
		m |= S_IFREG;
	else if (RTFS_IS_FIFO(fMode))
		m |= S_IFIFO;
	else if (RTFS_IS_DEV_CHAR(fMode))
		m |= S_IFCHR;
	else if (RTFS_IS_DEV_BLOCK(fMode))
		m |= S_IFBLK;
	else if (RTFS_IS_SYMLINK(fMode))
		m |= S_IFLNK;
	else if (RTFS_IS_SOCKET(fMode))
		m |= S_IFSOCK;

	if (fMode & RTFS_UNIX_IRUSR)
		m |= S_IRUSR;
	if (fMode & RTFS_UNIX_IWUSR)
		m |= S_IWUSR;
	if (fMode & RTFS_UNIX_IXUSR)
		m |= S_IXUSR;
	if (fMode & RTFS_UNIX_IRGRP)
		m |= S_IRGRP;
	if (fMode & RTFS_UNIX_IWGRP)
		m |= S_IWGRP;
	if (fMode & RTFS_UNIX_IXGRP)
		m |= S_IXGRP;
	if (fMode & RTFS_UNIX_IROTH)
		m |= S_IROTH;
	if (fMode & RTFS_UNIX_IWOTH)
		m |= S_IWOTH;
	if (fMode & RTFS_UNIX_IXOTH)
		m |= S_IXOTH;
	if (fMode & RTFS_UNIX_ISUID)
		m |= S_ISUID;
	if (fMode & RTFS_UNIX_ISGID)
		m |= S_ISGID;
	if (fMode & RTFS_UNIX_ISTXT)
		m |= S_ISVTX;
	
	return m;
}

status_t vboxsf_open(fs_volume* _volume, fs_vnode* _vnode, int openMode, void** _cookie) {
	TRACE
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_vnode* vnode = _vnode->private_node;
	
	dprintf(FS_NAME ": open (mode=%x)\n", openMode);
	
	SHFLCREATEPARMS params;
	
	RT_ZERO(params);
	params.Handle = SHFL_HANDLE_NIL;
	
	if (openMode & O_RDWR)
		params.CreateFlags |= SHFL_CF_ACCESS_READWRITE;
	else if (openMode & O_RDONLY)
		params.CreateFlags |= SHFL_CF_ACCESS_READ;
	else if (openMode & O_WRONLY)
		params.CreateFlags |= SHFL_CF_ACCESS_WRITE;
	
	if (openMode & O_APPEND)
		params.CreateFlags |= SHFL_CF_ACCESS_APPEND;
	
	if (openMode & O_CREAT) {
		params.CreateFlags |= SHFL_CF_ACT_CREATE_IF_NEW;
		if (openMode & O_EXCL)
			params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_EXISTS;
		else if (openMode & O_TRUNC)
			params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
		else
			params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
	}
	else {
		params.CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
		if (openMode & O_TRUNC)
			params.CreateFlags |= SHFL_CF_ACT_OVERWRITE_IF_EXISTS;
		else
			params.CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
	}
	
	int rc = vboxCallCreate(&g_clientHandle, &volume->map, vnode->path, &params);
	if (!RT_SUCCESS(rc)) {
		dprintf("vboxCallCreate returned %d\n", rc);
		return B_ERROR;
	}
	
	vboxsf_file_cookie* cookie = malloc(sizeof(vboxsf_file_cookie));
	if (!cookie) {
		dprintf("couldn't allocate file cookie\n");
		return B_NO_MEMORY;
	}
	
	cookie->handle = params.Handle;
	cookie->path = vnode->path;
	
	*_cookie = cookie;
	
	return B_OK;
}

status_t vboxsf_close(fs_volume* _volume, fs_vnode* _vnode, void* _cookie) {
	TRACE
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_file_cookie* cookie = _cookie;
	
	int rc = vboxCallClose(&g_clientHandle, &volume->map, cookie->handle);
	dprintf("vboxCallClose returned %d\n", rc);
	return (rc == 0)? B_OK : B_ERROR;
}

status_t vboxsf_free_cookie(fs_volume *volume, fs_vnode *vnode, void *cookie) {
	// FIXME should the path be freed here?
	free(cookie);
	return B_OK;
}

status_t vboxsf_read(fs_volume* _volume, fs_vnode* _vnode, void* _cookie, off_t pos, void *buffer, size_t *length) {
	TRACE
	vboxsf_volume* volume = _volume->private_volume;
	vboxsf_vnode* vnode = _vnode->private_node;
	vboxsf_file_cookie* cookie = _cookie;
	
	if (*length > 0xFFFFFFFF) {
		// TODO should this just read as much as it can?
		dprintf("vboxsf_read: length too big\n");
		return B_BAD_VALUE;
	}
	
	uint32_t l = *length;
	void* other_buffer = malloc(l);
	int rc = vboxCallRead(&g_clientHandle, &volume->map, cookie->handle, pos, &l, other_buffer, false);
	memcpy(buffer, other_buffer, l);
	free(other_buffer);
	
	dprintf("vboxCallRead returned %d\n", rc);
	*length = l;
	return (rc == 0)? B_OK : B_ERROR;
}

static status_t std_ops(int32 op, ...) {
	TRACE
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
	
	vboxsf_read_fs_info, // read_fs_info
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
	vboxsf_lookup, // lookup
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
	vboxsf_read_stat, // read_stat
	NULL, // write_stat
	NULL, // preallocate
	NULL, // create
	vboxsf_open, // open
	vboxsf_close, // close
	vboxsf_free_cookie, // free_cookie
	vboxsf_read, // read
	NULL, // write
	NULL, // create_dir
	NULL, // remove_dir
	vboxsf_open_dir, // open_dir
	NULL, // close_dir
	vboxsf_free_dir_cookie, // free_dir_cookie
	vboxsf_read_dir, // read_dir
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