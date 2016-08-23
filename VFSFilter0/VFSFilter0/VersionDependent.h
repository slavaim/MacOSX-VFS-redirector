//
//  VersionDependent.h
//  VFSFilter0
//
//  Created by slava on 21/06/2015.
//  Copyright (c) 2015 slava. All rights reserved.
//

#ifndef __VFSFilter0__VersionDependent__
#define __VFSFilter0__VersionDependent__

#include "Common.h"

//--------------------------------------------------------------------

//
// the structure defines an offset from the start of a v_op vector for a function
// implementing a corresponding vnode operation
//
typedef struct _QvrVnodeOpvOffsetDesc {
	struct vnodeop_desc *opve_op;   /* which operation this is, NULL for the terminating entry */
	vm_offset_t offset;		/* offset in bytes from the start of v_op, (-1) means "unknown" */
} QvrVnodeOpvOffsetDesc;

//--------------------------------------------------------------------

#define	VFC_VFSVNOP_PAGEINV2	0x2000
#define	VFC_VFSVNOP_PAGEOUTV2	0x4000

#define CN_SKIPNAMECACHE	0x40000000	/* skip cache during lookup(), allow FS to handle all components */

//--------------------------------------------------------------------

errno_t
QvrVnodeGetSize(vnode_t vp, off_t *sizep, vfs_context_t ctx);

errno_t
QvrVnodeSetsize(vnode_t vp, off_t size, int ioflag, vfs_context_t ctx);

VOPFUNC*
QvrGetVnodeOpVector(
                    __in vnode_t vn
                    );

QvrVnodeOpvOffsetDesc*
QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc(
                                          __in struct vnodeop_desc *opve_op
                                          );

VOPFUNC
QvrGetVnop(
           __in vnode_t   vn,
           __in struct vnodeop_desc *opve_op
           );

const char*
GetVnodeNamePtr(
    __in vnode_t vn
    );

int
QvrGetVnodeVfsFlags(
                    __in vnode_t vnode
                    );

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__VersionDependent__) */
