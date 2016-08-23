//
//  VNodeHook.h
//  VFSFilter0
//
//  Created by slava on 6/22/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__VNodeHook__
#define __VFSFilter0__VNodeHook__

#include <IOKit/assert.h>
#include <libkern/c++/OSObject.h>
#include <IOKit/assert.h>

#ifdef __cplusplus
extern "C" {
#endif
    
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/vnode.h>
    
#ifdef __cplusplus
}
#endif

#include "Common.h"
#include "CommonHashTable.h"

//--------------------------------------------------------------------

#define QVR_VOP_UNKNOWN_OFFSET ((vm_offset_t)(-1))

//--------------------------------------------------------------------

typedef enum _QvrVopEnum{
    QvrVopEnum_Unknown = 0x0,
    
    QvrVopEnum_access,
    QvrVopEnum_advlock,
    QvrVopEnum_allocate,
    QvrVopEnum_blktooff,
    QvrVopEnum_blockmap,
    QvrVopEnum_bwrite,
    QvrVopEnum_close,
    QvrVopEnum_copyfile,
    QvrVopEnum_create,
    QvrVopEnum_default,
    QvrVopEnum_exchange,
    QvrVopEnum_fsync,
    QvrVopEnum_getattr,
    QvrVopEnum_getxattr,
    QvrVopEnum_inactive,
    QvrVopEnum_ioctl,
    QvrVopEnum_link,
    QvrVopEnum_listxattr,
    QvrVopEnum_lookup,
    QvrVopEnum_kqfilt_add,
    QvrVopEnum_kqfilt_remove,
    QvrVopEnum_mkdir,
    QvrVopEnum_mknod,
    QvrVopEnum_mmap,
    QvrVopEnum_mnomap,
    QvrVopEnum_offtoblock,
    QvrVopEnum_open,
    QvrVopEnum_pagein,
    QvrVopEnum_pageout,
    QvrVopEnum_pathconf,
    QvrVopEnum_read,
    QvrVopEnum_readdir,
    QvrVopEnum_readdirattr,
    QvrVopEnum_readlink,
    QvrVopEnum_reclaim,
    QvrVopEnum_remove,
    QvrVopEnum_removexattr,
    QvrVopEnum_rename,
    QvrVopEnum_revoke,
    QvrVopEnum_rmdir,
    QvrVopEnum_searchfs,
    QvrVopEnum_select,
    QvrVopEnum_setattr,
    QvrVopEnum_setxattr,
    QvrVopEnum_strategy,
    QvrVopEnum_symlink,
    QvrVopEnum_whiteout,
    QvrVopEnum_write,
    QvrVopEnum_getnamedstreamHook,
    QvrVopEnum_makenamedstreamHook,
    QvrVopEnum_removenamedstreamHook,
    
    QvrVopEnum_Max
} QvrVopEnum;

//--------------------------------------------------------------------

class QvrVnodeHookEntry: public OSObject
{
    
    OSDeclareDefaultStructors( QvrVnodeHookEntry )
    
#if defined( DBG )
    friend class QvrVnodeHooksHashTable;
#endif//DBG
    
private:
    
    //
    // the number of vnodes which we are aware of for this v_op vector
    //
    SInt32 vNodeCounter;
    
    //
    // the value is used to mark the origVop's entry as
    // corresponding to not hooked function ( i.e. skipped deliberately )
    //
    static VOPFUNC  vopNotHooked;
    
    //
    // an original functions array,
    // for not present functons the values are set to NULL( notional assertion,
    // it was niether checked no taken into account by the code ),
    // for functions which hooking was skipped deliberately the
    // value is set to vopNotHooked
    //
    VOPFUNC  origVop[ QvrVopEnum_Max ];
    
#if defined( DBG )
    bool   inHash;
#endif
    
protected:
    
    virtual bool init();
    virtual void free();
    
public:
    
    //
    // allocates the new entry
    //
    static QvrVnodeHookEntry* newEntry()
    {
        QvrVnodeHookEntry* entry;
        
        assert( preemption_enabled() );
        
        entry = new QvrVnodeHookEntry();
        assert( entry ) ;
        if( !entry )
            return NULL;
        
        //
        // the init is very simple and must alvays succeed
        //
        entry->init();
        
        return entry;
    }
    
    
    VOPFUNC
    getOrignalVop( __in QvrVopEnum   indx ){
        
        assert( indx < QvrVopEnum_Max );
        return this->origVop[ (int)indx ];
    }
    
    void
    setOriginalVop( __in QvrVopEnum   indx, __in VOPFUNC orig ){
        
        assert( indx < QvrVopEnum_Max );
        assert( NULL == this->origVop[ (int)indx ] );
        
        this->origVop[ (int)indx ] = orig;
    }
    
    void
    setOriginalVopAsNotHooked( __in QvrVopEnum   indx ){
        
        this->setOriginalVop( indx, this->vopNotHooked );
    }
    
    bool
    isHooked( __in QvrVopEnum indx ){
        
        //
        // NULL is invalid, vopNotHooked means not hooked deliberately
        //
        return ( this->vopNotHooked != this->origVop[ (int)indx ] );
    }
    
    //
    // returns te value before the increment
    //
    SInt32
    incrementVnodeCounter(){
        
        assert( this->vNodeCounter < 0x80000000 );
        return OSIncrementAtomic( &this->vNodeCounter );
    }
    
    //
    // returns te value before the decrement
    //
    SInt32
    decrementVnodeCounter(){
        
        assert( this->vNodeCounter > 0x0 && this->vNodeCounter < 0x80000000);
        return OSDecrementAtomic( &this->vNodeCounter );
    }
    
    SInt32
    getVnodeCounter(){
        
        return this->vNodeCounter;
    }
    
};

//--------------------------------------------------------------------

class QvrVnodeHooksHashTable
{
    
private:
    
    ght_hash_table_t*  HashTable;
    IORWLock*          RWLock;
    
#if defined(DBG)
    thread_t           ExclusiveThread;
#endif//DBG
    
    //
    // returns an allocated hash table object
    //
    static QvrVnodeHooksHashTable* withSize( int size, bool non_block );
    
    //
    // free must be called before the hash table object is deleted
    //
    void free();
    
    //
    // as usual for IOKit the desctructor and constructor do nothing
    // as it is impossible to return an error from the constructor
    // in the kernel mode
    //
    QvrVnodeHooksHashTable()
    {
        
        this->HashTable = NULL;
        this->RWLock = NULL;
#if defined(DBG)
        this->ExclusiveThread = NULL;
#endif//DBG
        
    }
    
    //
    // the destructor checks that the free() has been called
    //
    ~QvrVnodeHooksHashTable()
    {
        
        assert( !this->HashTable && !this->RWLock );
    };
    
public:
    
    static bool CreateStaticTableWithSize( int size, bool non_block );
    static void DeleteStaticTable();
    
    //
    // adds an entry to the hash table, the entry is referenced so the caller must
    // dereference the entry if it has been referenced
    //
    bool   AddEntry( __in VOPFUNC* v_op, __in QvrVnodeHookEntry* entry );
    
    //
    // removes the entry from the hash and returns the removed entry, NULL if there
    // is no entry for an object, the returned entry is referenced
    //
    QvrVnodeHookEntry*   RemoveEntry( __in VOPFUNC* v_op );
    
    //
    // returns an entry from the hash table, the returned entry is referenced
    // if the refrence's value is "true"
    //
    QvrVnodeHookEntry*   RetrieveEntry( __in VOPFUNC* v_op, __in bool reference = true );
    
    
    void
    LockShared()
    {   assert( this->RWLock );
        assert( preemption_enabled() );
        
        IORWLockRead( this->RWLock );
    };
    
    
    void
    UnLockShared()
    {   assert( this->RWLock );
        assert( preemption_enabled() );
        
        IORWLockUnlock( this->RWLock );
    };
    
    
    void
    LockExclusive()
    {
        assert( this->RWLock );
        assert( preemption_enabled() );
        
#if defined(DBG)
        assert( current_thread() != this->ExclusiveThread );
#endif//DBG
        
        IORWLockWrite( this->RWLock );
        
#if defined(DBG)
        assert( NULL == this->ExclusiveThread );
        this->ExclusiveThread = current_thread();
#endif//DBG
        
    };
    
    
    void
    UnLockExclusive()
    {
        assert( this->RWLock );
        assert( preemption_enabled() );
        
#if defined(DBG)
        assert( current_thread() == this->ExclusiveThread );
        this->ExclusiveThread = NULL;
#endif//DBG
        
        IORWLockUnlock( this->RWLock );
    };
    
    static QvrVnodeHooksHashTable* sVnodeHooksHashTable;
};

//--------------------------------------------------------------------

//
// success doen't mean that a vnode's operations table has
// been hooked, it can be skipped as ia not interested for us
//
extern
IOReturn
QvrHookVnodeVop(
                __inout vnode_t vnode,
                __inout bool* isVopHooked
                );

extern
VOPFUNC
QvrGetOriginalVnodeOp(
                      __in vnode_t      vnode,
                      __in QvrVopEnum   indx
                      );

extern
void
QvrUnHookVnodeVop(
                  __inout vnode_t vnode
                  );

extern
void
QvrHookVnodeVopAndParent(
                         __inout vnode_t vnode
                         );

extern
void
QvrUnHookVnodeVopAndParent(
                           __inout vnode_t vnode
                           );

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__VNodeHook__) */
