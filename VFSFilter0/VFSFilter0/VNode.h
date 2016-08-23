//
//  VNode.h
//  VFSFilter0
//
//  Created by slava on 21/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__VNode__
#define __VFSFilter0__VNode__

#include "Common.h"
#include "RecursionEngine.h"
#include "ApplicationsData.h"

//--------------------------------------------------------------------

errno_t
QvrAdjustVnodeSizeByBackingVnode(
                                 __in vnode_t vnode,
                                 __in vnode_t backingVnode,
                                 __in vfs_context_t  context
                                 );

//--------------------------------------------------------------------

typedef struct{
    char dirPath[ MAXPATHLEN + 1 ];
} VnodeData;

/*
 this one should be redesigned
 */
class VNodeMap: public DataMap{

public:
    
    static void Init()
    {
        Lock = IOLockAlloc();
        assert( Lock );
    }
    
private:
    static VNodeMap   InstanceForAppData; // vnode to ApplicationData
    static VNodeMap   InstanceForVnodeIO; // vnode to vnodeIO ( i.e. a backing vnode )
    static VNodeMap   InstanceForShadowToVnode; // shadow vnode to vnode ( i.e. a reverse mappong )
    static VNodeMap   InstanceForHookedVnodes; // vnodes for which QvrHookVnodeVop was called
    static IOLock*    Lock;
    
public:
    
    //---------------------------------------------------------------------
    
    static bool  addHookedVnode( __in vnode_t  vn ){ return InstanceForHookedVnodes.addDataByKey( vn, (void*)vn ); }
    static void  removeHookedVnode( __in vnode_t vn ){ InstanceForHookedVnodes.removeKey( vn ); }
    static bool  isVnodeHooked( __in vnode_t vn ){ return 0x0 != InstanceForHookedVnodes.getDataByKey( vn ); }
    
    //---------------------------------------------------------------------

    static bool  addVnodeAppData( __in vnode_t  vn, __in const ApplicationData* data ){ return InstanceForAppData.addDataByKey( vn, (void*)data ); }
    static void  removeVnodeAppData( __in vnode_t vn ){ InstanceForAppData.removeKey( vn ); }
    static const ApplicationData* getVnodeAppData( __in vnode_t vn ){ return (const ApplicationData*)InstanceForAppData.getDataByKey( vn ); }
    
    //---------------------------------------------------------------------
    
    static void addVnodeShadowReverse( __in vnode_t  vnodeShadow, __in vnode_t vn )
    {
        assert( vnodeShadow != vn );
        
        vnode_t oldVn = NULLVP;
        
        IOLockLock( VNodeMap::Lock );
        {
            vnode_t currentVnode = getVnodeShadowReverse( vnodeShadow );
            bool    add = false;
            
            if( NULLVP == currentVnode ){
                
                add = true;
                
            } else if( vn != currentVnode ){
                
                InstanceForShadowToVnode.removeKey( vnodeShadow );
                oldVn = currentVnode;
                add = true;
            }
            
            if( add && InstanceForShadowToVnode.addDataByKey( vnodeShadow, (void*)vn ) )
                vnode_get( vn );
        }
        IOLockUnlock( VNodeMap::Lock );
        
        if( oldVn )
            vnode_put( oldVn );
    }
    
    static void    removeShadowReverse( __in vnode_t  vnodeShadow )
    {
        vnode_t vn = NULLVP;
        
        IOLockLock( VNodeMap::Lock );
        {
            vn= getVnodeShadowReverse( vnodeShadow );
            if( vn )
                InstanceForShadowToVnode.removeKey( vnodeShadow );
        }
        IOLockUnlock( VNodeMap::Lock );
        
        //
        // release a reference taken by addVnodeShadowReverse
        //
        if( vn )
            vnode_put( vn );
    }
    
    static const vnode_t getVnodeShadowReverseRef( __in vnode_t vnodeShadow )
    /*the returned vnode is referenced, a caller must release it by vnode_put()*/
    {
        vnode_t vn;
        
        IOLockLock( VNodeMap::Lock );
        {
            vn = getVnodeShadowReverse( vnodeShadow );
            if( vn )
                vnode_get( vn );
        }
        IOLockUnlock( VNodeMap::Lock );
        
        return vn;
    }
    
    static void releaseAllShadowReverse()
    {
        while( ! InstanceForShadowToVnode.isEmpty() ){
            
            vnode_t   vn = NULLVP;
            
            IOLockLock( VNodeMap::Lock );
            {
                vn = (vnode_t)InstanceForShadowToVnode.removeFirstEntryAndReturnItsData();
            }
            IOLockUnlock( VNodeMap::Lock );
            
            if( vn )
                vnode_put( vn );
        }
    }
    
    static void addVnodeIO( __in vnode_t  vn, __in vnode_t vnodeIO )
    {
        assert( vn != vnodeIO );
        IOLockLock( VNodeMap::Lock );
        {
            if( NULLVP == getVnodeIO( vn ) ){
                
                if( InstanceForVnodeIO.addDataByKey( vn, (void*)vnodeIO ) )
                    vnode_get( vnodeIO );
                
            } else {
                
                assert( vnodeIO == getVnodeIO( vn ) );
            }
        }
        IOLockUnlock( VNodeMap::Lock );
    }
    
    static void    removeVnodeIO( __in vnode_t vn )
    {
        vnode_t vnodeIO = NULLVP;
        
        IOLockLock( VNodeMap::Lock );
        {
            vnodeIO = getVnodeIO( vn );
            if( vnodeIO )
                InstanceForVnodeIO.removeKey( vn );
        }
        IOLockUnlock( VNodeMap::Lock );
        
        //
        // release a reference taken by addVnodeIO
        //
        if( vnodeIO )
            vnode_put( vnodeIO );
    }
    
    static const vnode_t getVnodeIORef( __in vnode_t vn )
    /*the returned vnode is referenced, a caller must release it by vnode_put()*/
    {
        vnode_t vnodeIO;
        
        IOLockLock( VNodeMap::Lock );
        {
            vnodeIO = getVnodeIO( vn );
            if( vnodeIO )
                vnode_get( vnodeIO );
        }
        IOLockUnlock( VNodeMap::Lock );
        
        return vnodeIO;
    }
    
    static void releaseAllVnodeIO()
    {
        while( ! InstanceForVnodeIO.isEmpty() ){
            
            vnode_t   vnodeIO = NULLVP;
            
            IOLockLock( VNodeMap::Lock );
            {
                vnodeIO = (vnode_t)InstanceForVnodeIO.removeFirstEntryAndReturnItsData();
            }
            IOLockUnlock( VNodeMap::Lock );
            
            if( vnodeIO )
                vnode_put( vnodeIO );
        }
    }
    
    //---------------------------------------------------------------------
    
private:
    
    static const vnode_t getVnodeIO( __in vnode_t vn )
    /*the returned vnode is not referenced*/
    {
        return (vnode_t)InstanceForVnodeIO.getDataByKey( vn );
    }
    
    static const vnode_t getVnodeShadowReverse( __in vnode_t vnodeShadow )
    /*the returned vnode is not referenced*/
    {
        return (vnode_t)InstanceForShadowToVnode.getDataByKey( vnodeShadow );
    }
};

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__VNode__) */
