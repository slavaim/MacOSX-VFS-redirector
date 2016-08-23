//
//  VNode.cpp
//  VFSFilter0
//
//  Created by slava on 21/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#include "VNode.h"
#include "VersionDependent.h"

//--------------------------------------------------------------------

VNodeMap   VNodeMap::InstanceForAppData;
VNodeMap   VNodeMap::InstanceForVnodeIO;
VNodeMap   VNodeMap::InstanceForHookedVnodes;
VNodeMap   VNodeMap::InstanceForShadowToVnode;
IOLock*    VNodeMap::Lock;

//--------------------------------------------------------------------

errno_t
QvrAdjustVnodeSizeByBackingVnode(
    __in vnode_t vnode,
    __in vnode_t backingVnode,
    __in vfs_context_t  context
    )
{
    off_t   newSize;
    off_t   oldSize;
    errno_t error;
    
    error = QvrVnodeGetSize( vnode, &oldSize, context );
    if( error )
        return error;
    
    error = QvrVnodeGetSize( backingVnode, &newSize, gSuperUserContext );
    if( error )
        return error;
    
    if( newSize > oldSize )
        error = QvrVnodeSetsize( vnode, newSize, 0x0, context );
        
    return error;
}

//--------------------------------------------------------------------
