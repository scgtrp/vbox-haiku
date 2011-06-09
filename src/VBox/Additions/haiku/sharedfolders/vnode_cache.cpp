#include "vboxsf.h"
#include <util/OpenHashTable.h>

struct HashTableDefinition {
	typedef uint32 KeyType;
	typedef	vboxsf_vnode ValueType;

	size_t HashKey(uint32 key) const
	{
		return key;
	}

	size_t Hash(vboxsf_vnode* value) const
	{
		return HashKey(value->vnode);
	}

	bool Compare(uint32 key, vboxsf_vnode* value) const
	{
		return value->vnode == key;
	}

	vboxsf_vnode*& GetLink(vboxsf_vnode* value) const
	{
		return value->next;
	}
};

static BOpenHashTable<HashTableDefinition> g_cache;
static ino_t g_next_vnid = 1;

extern "C" status_t vboxsf_new_vnode(PVBSFMAP map, PSHFLSTRING path, PSHFLSTRING name, vboxsf_vnode** p) {
	// TODO check if one already exists with this path+map
	vboxsf_vnode* vn = (vboxsf_vnode*)malloc(sizeof(vboxsf_vnode));
	if (vn == NULL) {
		return B_NO_MEMORY;
	}
	dprintf("creating new vnode at %p with path=%p (%s)\n", vn, path->String.utf8, path->String.utf8);
	vn->map = map;
	vn->path = path;
	if (name) {
		vn->name = name;
	}
	else {
		const char* cname = strrchr((char*)path->String.utf8, '/');
		if (!cname)
			vn->name = path; // no slash, assume this *is* the filename
		else
			vn->name = make_shflstring(cname);
	}
	vn->vnode = g_next_vnid++; // FIXME probably need locking here
	*p = vn;
	dprintf("vboxsf: allocated %p (path=%p name=%p)\n", vn, vn->path, vn->name);
	return g_cache.Insert(vn);
}

extern "C" status_t vboxsf_get_vnode(fs_volume* volume, ino_t id, fs_vnode* vnode,
	int* _type, uint32* _flags, bool reenter) {
	vboxsf_vnode* vn = g_cache.Lookup(id);
	if (vn) {
		vnode->private_node = vn;
		return B_OK;
	}
	else {
		return B_ERROR;
	}
}

extern "C" status_t vboxsf_put_vnode(fs_volume* volume, fs_vnode* vnode, bool reenter) {
	g_cache.Remove((vboxsf_vnode*)vnode->private_node);
}
