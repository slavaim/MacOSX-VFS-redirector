//
//  VersionDependent.cpp
//  VFSFilter0
//
//  Created by slava on 21/06/2015.
//  Copyright (c) 2015 slava. All rights reserved.
//

#include "Common.h"
#include "VersionDependent.h"
#include "VNodeHook.h"
#include <AvailabilityMacros.h>

//--------------------------------------------------------------------

errno_t
QvrVnodeGetSize(vnode_t vp, off_t *sizep, vfs_context_t ctx)
{
	struct vnode_attr	va;
	int			error;
    
	VATTR_INIT(&va);
	VATTR_WANTED(&va, va_data_size);
	error = vnode_getattr(vp, &va, ctx);
	if (!error)
		*sizep = va.va_data_size;
	return(error);
}

//--------------------------------------------------------------------

errno_t
QvrVnodeSetsize(vnode_t vp, off_t size, int ioflag, vfs_context_t ctx)
{
	struct vnode_attr	va;
    
	VATTR_INIT(&va);
	VATTR_SET(&va, va_data_size, size);
	va.va_vaflags = ioflag & 0xffff;
	return(vnode_setattr(vp, &va, ctx));
}

//--------------------------------------------------------------------

//
// The following code is full of lie. For a commercial release a version
// independent hooker must be implemented.
//

#if !defined(MAC_OS_X_VERSION_10_10)
    #error "Looks like you are compiling with the old SDK"
#endif

#if !defined(MAC_OS_X_VERSION_10_11) || MAC_OS_X_VERSION_MIN_REQUIRED <= MAC_OS_X_VERSION_10_11

// compile for 10.11 or older version

/*
 * This structure describes the vnode operation taking place.
 */
struct vnodeop_desc_Yosemite {
    int	vdesc_offset;		/* offset in vector--first for speed */
    const char *vdesc_name;		/* a readable name for debugging */
    int	vdesc_flags;		/* VDESC_* flags */
    
    /*
     * These ops are used by bypass routines to map and locate arguments.
     * Creds and procs are not needed in bypass routines, but sometimes
     * they are useful to (for example) transport layers.
     * Nameidata is useful because it has a cred in it.
     */
    int	*vdesc_vp_offsets;	/* list ended by VDESC_NO_OFFSET */
    int	vdesc_vpp_offset;	/* return vpp location */
    int	vdesc_cred_offset;	/* cred location, if any */
    int	vdesc_proc_offset;	/* proc location, if any */
    int	vdesc_componentname_offset; /* if any */
    int	vdesc_context_offset;	/* context location, if any */
    /*
     * Finally, we've got a list of private data (about each operation)
     * for each transport layer.  (Support to manage this list is not
     * yet part of BSD.)
     */
    caddr_t	*vdesc_transports;
};

LIST_HEAD(buflists_Yosemite, buf);
SLIST_HEAD(klist_Yosemite, knote);

typedef struct {
    unsigned long		opaque[2];
} lck_mtx_t_Yosemite;


struct vnode_Yosemite {
    lck_mtx_t_Yosemite v_lock;			/* vnode mutex */
    TAILQ_ENTRY(vnode) v_freelist;		/* vnode freelist */
    TAILQ_ENTRY(vnode) v_mntvnodes;		/* vnodes for mount point */
    LIST_HEAD(, namecache) v_nclinks;	/* name cache entries that name this vnode */
    LIST_HEAD(, namecache) v_ncchildren;	/* name cache entries that regard us as their parent */
    vnode_t	 v_defer_reclaimlist;		/* in case we have to defer the reclaim to avoid recursion */
    uint32_t v_listflag;			/* flags protected by the vnode_list_lock (see below) */
    uint32_t v_flag;			/* vnode flags (see below) */
    uint16_t v_lflag;			/* vnode local and named ref flags */
    uint8_t	 v_iterblkflags;		/* buf iterator flags */
    uint8_t	 v_references;			/* number of times io_count has been granted */
    int32_t	 v_kusecount;			/* count of in-kernel refs */
    int32_t	 v_usecount;			/* reference count of users */
    int32_t	 v_iocount;			/* iocounters */
    void *   v_owner;			/* act that owns the vnode */
    uint16_t v_type;			/* vnode type */
    uint16_t v_tag;				/* type of underlying data */
    uint32_t v_id;				/* identity of vnode contents */
    union {
        struct mount	*vu_mountedhere;/* ptr to mounted vfs (VDIR) */
        struct socket	*vu_socket;	/* unix ipc (VSOCK) */
        struct specinfo	*vu_specinfo;	/* device (VCHR, VBLK) */
        struct fifoinfo	*vu_fifoinfo;	/* fifo (VFIFO) */
        struct ubc_info *vu_ubcinfo;	/* valid for (VREG) */
    } v_un;
    struct	buflists_Yosemite v_cleanblkhd;		/* clean blocklist head */
    struct	buflists_Yosemite v_dirtyblkhd;		/* dirty blocklist head */
    struct klist_Yosemite v_knotes;			/* knotes attached to this vnode */
    /*
     * the following 4 fields are protected
     * by the name_cache_lock held in
     * excluive mode
     */
    kauth_cred_t	v_cred;			/* last authorized credential */
    kauth_action_t	v_authorized_actions;	/* current authorized actions for v_cred */
    int		v_cred_timestamp;	/* determine if entry is stale for MNTK_AUTH_OPAQUE */
    int		v_nc_generation;	/* changes when nodes are removed from the name cache */
    /*
     * back to the vnode lock for protection
     */
    int32_t		v_numoutput;			/* num of writes in progress */
    int32_t		v_writecount;			/* reference count of writers */
    const char *v_name;			/* name component of the vnode */
    vnode_t v_parent;			/* pointer to parent vnode */
    struct lockf	*v_lockf;		/* advisory lock list head */
    int 	(**v_op)(void *);		/* vnode operations vector */
    mount_t v_mount;			/* ptr to vfs we are in */
    void *	v_data;				/* private data for fs */
#if CONFIG_MACF
    struct label *v_label;			/* MAC security label */
#endif
#if CONFIG_TRIGGERS
    vnode_resolve_t v_resolve;		/* trigger vnode resolve info (VDIR only) */
#endif /* CONFIG_TRIGGERS */
};

typedef struct vnode_Yosemite vnode_internal;

#define VNOP_OFFSET( vnodeop_desc )   (sizeof(VOPFUNC)*((vnodeop_desc_Yosemite*)&vnodeop_desc)->vdesc_offset)

struct mount_header_Yosemite {
    TAILQ_ENTRY(mount) mnt_list;		/* mount list */
    int32_t		mnt_count;		/* reference on the mount */
    lck_mtx_t_Yosemite	mnt_mlock;		/* mutex that protects mount point */
    struct vfsops	*mnt_op;		/* operations on fs */
    struct vfstable	*mnt_vtable;		/* configuration info */
};

struct vfstable_header_Yosemite {
	struct	vfsops *vfc_vfsops;	/* filesystem operations vector */
	char	vfc_name[MFSNAMELEN];	/* filesystem type name */
	int	vfc_typenum;		/* historic filesystem type number */
	int	vfc_refcount;		/* number mounted of this type */
	int	vfc_flags;		/* permanent flags */
	int	(*vfc_mountroot)(mount_t, vnode_t, vfs_context_t);	/* if != NULL, routine to mount root */
	struct	vfstable *vfc_next;	/* next in list */
	int32_t	vfc_reserved1;
	int32_t vfc_reserved2;
	int 		vfc_vfsflags;	/* for optional types */
	void *		vfc_descptr;	/* desc table allocated address */
	int			vfc_descsize;	/* size allocated for desc table */
	struct sysctl_oid	*vfc_sysctl;	/* dynamically registered sysctl node */
};

typedef struct mount_header_Yosemite mount_header_internal;
typedef struct vfstable_header_Yosemite vfstable_header_internal;

#else

// compile for Sierra(10.12)

/*
 * This structure describes the vnode operation taking place.
 */
struct vnodeop_desc_Sierra {
	int	vdesc_offset;		/* offset in vector--first for speed */
	const char *vdesc_name;		/* a readable name for debugging */
	int	vdesc_flags;		/* VDESC_* flags */
    
	/*
	 * These ops are used by bypass routines to map and locate arguments.
	 * Creds and procs are not needed in bypass routines, but sometimes
	 * they are useful to (for example) transport layers.
	 * Nameidata is useful because it has a cred in it.
	 */
	int	*vdesc_vp_offsets;	/* list ended by VDESC_NO_OFFSET */
	int	vdesc_vpp_offset;	/* return vpp location */
	int	vdesc_cred_offset;	/* cred location, if any */
	int	vdesc_proc_offset;	/* proc location, if any */
	int	vdesc_componentname_offset; /* if any */
	int	vdesc_context_offset;	/* context location, if any */
	/*
	 * Finally, we've got a list of private data (about each operation)
	 * for each transport layer.  (Support to manage this list is not
	 * yet part of BSD.)
	 */
	caddr_t	*vdesc_transports;
};

LIST_HEAD(buflists_Sierra, buf);
SLIST_HEAD(klist_Sierra, knote);

typedef struct {
    unsigned long		opaque[2];
} lck_mtx_t_Sierra;

struct vnode_Sierra {
	lck_mtx_t_Sierra v_lock;			/* vnode mutex */
	TAILQ_ENTRY(vnode) v_freelist;		/* vnode freelist */
	TAILQ_ENTRY(vnode) v_mntvnodes;		/* vnodes for mount point */
    TAILQ_HEAD(, namecache) v_ncchildren;	/* name cache entries that regard us as their parent */
    LIST_HEAD(, namecache) v_nclinks;	/* name cache entries that name this vnode */
    vnode_t	 v_defer_reclaimlist;		/* in case we have to defer the reclaim to avoid recursion */
    uint32_t v_listflag;			/* flags protected by the vnode_list_lock (see below) */
	uint32_t v_flag;			/* vnode flags (see below) */
	uint16_t v_lflag;			/* vnode local and named ref flags */
	uint8_t	 v_iterblkflags;		/* buf iterator flags */
	uint8_t	 v_references;			/* number of times io_count has been granted */
	int32_t	 v_kusecount;			/* count of in-kernel refs */
	int32_t	 v_usecount;			/* reference count of users */
	int32_t	 v_iocount;			/* iocounters */
	void *   v_owner;			/* act that owns the vnode */
	uint16_t v_type;			/* vnode type */
	uint16_t v_tag;				/* type of underlying data */
	uint32_t v_id;				/* identity of vnode contents */
	union {
		struct mount	*vu_mountedhere;/* ptr to mounted vfs (VDIR) */
		struct socket	*vu_socket;	/* unix ipc (VSOCK) */
		struct specinfo	*vu_specinfo;	/* device (VCHR, VBLK) */
		struct fifoinfo	*vu_fifoinfo;	/* fifo (VFIFO) */
        struct ubc_info *vu_ubcinfo;	/* valid for (VREG) */
	} v_un;
	struct	buflists_Sierra v_cleanblkhd;		/* clean blocklist head */
	struct	buflists_Sierra v_dirtyblkhd;		/* dirty blocklist head */
	struct klist_Sierra v_knotes;			/* knotes attached to this vnode */
    /*
	 * the following 4 fields are protected
	 * by the name_cache_lock held in
	 * excluive mode
	 */
    kauth_cred_t	v_cred;			/* last authorized credential */
    kauth_action_t	v_authorized_actions;	/* current authorized actions for v_cred */
    int		v_cred_timestamp;	/* determine if entry is stale for MNTK_AUTH_OPAQUE */
    int		v_nc_generation;	/* changes when nodes are removed from the name cache */
    /*
	 * back to the vnode lock for protection
	 */
	int32_t		v_numoutput;			/* num of writes in progress */
	int32_t		v_writecount;			/* reference count of writers */
	const char *v_name;			/* name component of the vnode */
	vnode_t v_parent;			/* pointer to parent vnode */
	struct lockf	*v_lockf;		/* advisory lock list head */
	int 	(**v_op)(void *);		/* vnode operations vector */
	mount_t v_mount;			/* ptr to vfs we are in */
	void *	v_data;				/* private data for fs */
#if CONFIG_MACF
	struct label *v_label;			/* MAC security label */
#endif
#if CONFIG_TRIGGERS
	vnode_resolve_t v_resolve;		/* trigger vnode resolve info (VDIR only) */
#endif /* CONFIG_TRIGGERS */
};

typedef struct vnode_Sierra vnode_internal;

#define VNOP_OFFSET( vnodeop_desc )   (sizeof(VOPFUNC)*((vnodeop_desc_Sierra*)&vnodeop_desc)->vdesc_offset)

struct mount_header_Sierra {
    TAILQ_ENTRY(mount) mnt_list;		/* mount list */
    int32_t		mnt_count;		/* reference on the mount */
    lck_mtx_t_Sierra	mnt_mlock;		/* mutex that protects mount point */
    struct vfsops	*mnt_op;		/* operations on fs */
    struct vfstable	*mnt_vtable;		/* configuration info */
};

struct vfstable_header_Sierra {
	struct	vfsops *vfc_vfsops;	/* filesystem operations vector */
	char	vfc_name[MFSNAMELEN];	/* filesystem type name */
	int	vfc_typenum;		/* historic filesystem type number */
	int	vfc_refcount;		/* number mounted of this type */
	int	vfc_flags;		/* permanent flags */
	int	(*vfc_mountroot)(mount_t, vnode_t, vfs_context_t);	/* if != NULL, routine to mount root */
	struct	vfstable *vfc_next;	/* next in list */
	int32_t	vfc_reserved1;
	int32_t vfc_reserved2;
	int 		vfc_vfsflags;	/* for optional types */
	void *		vfc_descptr;	/* desc table allocated address */
	int			vfc_descsize;	/* size allocated for desc table */
	struct sysctl_oid	*vfc_sysctl;	/* dynamically registered sysctl node */
};

typedef struct mount_header_Sierra mount_header_internal;
typedef struct vfstable_header_Sierra vfstable_header_internal;

#endif //

//--------------------------------------------------------------------

//
// the gDldFakeVnodeopEntries  array must be synchronized with the gQvrVnodeOpvOffsetDesc array,
// i.e. the arrays must contain the same number and types of entries excluding the terminating ones
// and the vnop_default_desc entry
//
static QvrVnodeOpvOffsetDesc  gQvrVnodeOpvOffsetDesc[] = {
    
	{ &vnop_lookup_desc,   VNOP_OFFSET(vnop_lookup_desc)   },           /* lookup */
	{ &vnop_create_desc,   VNOP_OFFSET(vnop_create_desc)   },           /* create */
    { &vnop_open_desc,     VNOP_OFFSET(vnop_open_desc)     },           /* open */
    { &vnop_close_desc,    VNOP_OFFSET(vnop_close_desc)    },		    /* close */
    { &vnop_inactive_desc, VNOP_OFFSET(vnop_inactive_desc) },	        /* inactive */
    { &vnop_reclaim_desc,  VNOP_OFFSET(vnop_reclaim_desc)  },           /* reclaim */
	{ &vnop_read_desc,     VNOP_OFFSET(vnop_read_desc)     },		    /* read */
	{ &vnop_write_desc,    VNOP_OFFSET(vnop_write_desc)    },		    /* write */
	{ &vnop_pagein_desc,   VNOP_OFFSET(vnop_pagein_desc)   },		    /* Pagein */
	{ &vnop_pageout_desc,  VNOP_OFFSET(vnop_pageout_desc)  },		    /* Pageout */
    { &vnop_mmap_desc,     VNOP_OFFSET(vnop_mmap_desc)     },           /* mmap */
    { &vnop_rename_desc,   VNOP_OFFSET(vnop_rename_desc)   },           /* rename */
    { &vnop_exchange_desc, VNOP_OFFSET(vnop_exchange_desc) },           /* exchange */
    { &vnop_getattr_desc,  VNOP_OFFSET(vnop_getattr_desc) },            /* getattr */
    { (struct vnodeop_desc*)NULL, QVR_VOP_UNKNOWN_OFFSET }
};

//
// exclude the terminating entry from the valid entries number
//
static int gNumberOfOpvOffsetDescs = sizeof( gQvrVnodeOpvOffsetDesc )/sizeof( gQvrVnodeOpvOffsetDesc[0] ) - 0x1;

//--------------------------------------------------------------------

QvrVnodeOpvOffsetDesc*
QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc(
                                          __in struct vnodeop_desc *opve_op
                                          )
{
    for( int i = 0x0; NULL != gQvrVnodeOpvOffsetDesc[ i ].opve_op; ++i ){
        
        if( opve_op == gQvrVnodeOpvOffsetDesc[ i ].opve_op )
            return &gQvrVnodeOpvOffsetDesc[ i ];
        
    }// end for
    
    return ( QvrVnodeOpvOffsetDesc* )NULL;
}

//--------------------------------------------------------------------

VOPFUNC*
QvrGetVnodeOpVector(
    __in vnode_t vn
    )
{
    return ((vnode_internal*)vn)->v_op;
}

//--------------------------------------------------------------------

VOPFUNC
QvrGetVnop(
    __in vnode_t   vn,
    __in struct vnodeop_desc *opve_op
    )
{
    QvrVnodeOpvOffsetDesc*   desc = QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc( opve_op );
    assert( desc );
    if( ! desc )
        return NULL;
    
    VOPFUNC* vnopVector = QvrGetVnodeOpVector( vn );
    
    return  vnopVector[ desc->offset / sizeof( VOPFUNC ) ];
}

//--------------------------------------------------------------------

const char*
GetVnodeNamePtr(
    __in vnode_t vn
    )
{
    return ((vnode_internal*)vn)->v_name;
}

//--------------------------------------------------------------------

int
QvrGetVnodeVfsFlags(
    __in vnode_t vnode
    )
{
    // vp->v_mount->mnt_vtable->vfc_vfsflags & VFC_VFSVNOP_PAGEINV2
    
    mount_header_internal*  v_mount = (mount_header_internal*)vnode_mount( vnode );
    assert( v_mount );
    vfstable_header_internal*  mnt_vtable = (vfstable_header_internal*)v_mount->mnt_vtable;
    assert( mnt_vtable );
    
    return mnt_vtable->vfc_vfsflags;
}

//--------------------------------------------------------------------

