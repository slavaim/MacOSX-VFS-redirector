//
//  VNodeHook.cpp
//  VFSFilter0
//
//  Created by slava on 6/22/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#include "VNodeHook.h"
#include "VersionDependent.h"
#include "VmPmap.h"
#include "VFSHooks.h"
#include "VNode.h"

//--------------------------------------------------------------------

//
// the following two arrays must obey the rule - for any entry in the gQvrVnodeVopHookEntries array
// there must be a corresponding entry in the gQvrVnodeVopEnumEntries array and in the gQvrVnodeOpvOffsetDesc
// gQvrFakeVnodeopEntries arrays, vice versa is not true - the last three mentioned arrays might contain
// more descriptors than the gQvrVnodeVopHookEntries array, so they can be filled in advance
//

//
//
// hook descriptors for the vnode's o_pv vector,
// this array is iterated when hooking is performed,
// so it defines the order of hooks, the correct
// order is when reclaim and close are hooked first
// and the open and create are last so there will not be
// a case when a vnode is discovered on create but its
// reclaim is missed as the reclaim routine has not yet been
// hooked
//


//
// the lookup and create must be hooked ater all over vnode operations
// to provide correct processing for covered vnodes when it will not happen
// that covering vnode will sneak to the covered FSD
//

struct vnodeopv_entry_desc gQvrVnodeVopHookEntries[] = {
    { &vnop_reclaim_desc,  (VOPFUNC)QvrFsdReclaimHookEx2 },             /* reclaim */
    { &vnop_read_desc,     (VOPFUNC)QvrVnopReadHookEx2 },		        /* read */
    { &vnop_open_desc,     (VOPFUNC)QvrVnopOpenHookEx2 },		        /* open */
    { &vnop_write_desc,    (VOPFUNC)QvrVnopWriteHookEx2 },		        /* write */
    { &vnop_pagein_desc,   (VOPFUNC)QvrVnopPageinHookEx2 },		        /* Pagein */
    { &vnop_pageout_desc,  (VOPFUNC)QvrVnopPageoutHookEx2 },		    /* Pageout */
    { &vnop_lookup_desc,   (VOPFUNC)QvrVnopLookupHookEx2 },		        /* lookup */
    { &vnop_create_desc,   (VOPFUNC)QvrVnopCreateHookEx2 },		        /* create */
    { &vnop_close_desc,    (VOPFUNC)QvrVnopCloseHookEx2 },		        /* close */
    { &vnop_inactive_desc, (VOPFUNC)QvrVnopInactiveHookEx2 },	        /* inactive */
    //{ &vnop_mmap_desc,     (VOPFUNC)QvrFsdMmapHook  },                /* mmap */
    { &vnop_rename_desc,   (VOPFUNC)QvrVnopRenameHookEx2 },             /* rename */
    { &vnop_exchange_desc, (VOPFUNC)QvrVnopExchangeHookEx2 },           /* exchange */
    { &vnop_getattr_desc,  (VOPFUNC)QvrVnopGetattrHookEx2 },            /* getattr */
    { (struct vnodeop_desc*)NULL, (VOPFUNC)(int(*)())NULL }
};

//
// defines a mapping from the lookup entries to indices
//
struct vnodeopv_entry_desc gQvrVnodeVopEnumEntries[] = {
    { &vnop_lookup_desc,   (VOPFUNC)QvrVopEnum_lookup },		    /* lookup */
    { &vnop_create_desc,   (VOPFUNC)QvrVopEnum_create },		    /* create */
    { &vnop_open_desc,     (VOPFUNC)QvrVopEnum_open },		        /* open */
    { &vnop_close_desc,    (VOPFUNC)QvrVopEnum_close },		        /* close */
    { &vnop_access_desc,   (VOPFUNC)QvrVopEnum_access },            /* access */
    { &vnop_inactive_desc, (VOPFUNC)QvrVopEnum_inactive },	        /* inactive */
    { &vnop_reclaim_desc,  (VOPFUNC)QvrVopEnum_reclaim },           /* reclaim */
    { &vnop_read_desc,     (VOPFUNC)QvrVopEnum_read },		        /* read */
    { &vnop_write_desc,    (VOPFUNC)QvrVopEnum_write },		        /* write */
    { &vnop_pagein_desc,   (VOPFUNC)QvrVopEnum_pagein },		    /* Pagein */
    { &vnop_pageout_desc,  (VOPFUNC)QvrVopEnum_pageout },		    /* Pageout */
    { &vnop_strategy_desc, (VOPFUNC)QvrVopEnum_strategy },          /* Strategy */
    { &vnop_mmap_desc,     (VOPFUNC)QvrVopEnum_mmap  },             /* mmap */
    { &vnop_rename_desc,   (VOPFUNC)QvrVopEnum_rename },
    { &vnop_pathconf_desc, (VOPFUNC)QvrVopEnum_pathconf },
    { &vnop_exchange_desc, (VOPFUNC)QvrVopEnum_exchange },          /* exchange */
    { &vnop_getattr_desc,  (VOPFUNC)QvrVopEnum_getattr },           /* getattr */
    { (struct vnodeop_desc*)NULL, (VOPFUNC)QvrVopEnum_Max }
};

//--------------------------------------------------------------------

QvrVnodeHooksHashTable* QvrVnodeHooksHashTable::sVnodeHooksHashTable = NULL;

//--------------------------------------------------------------------

struct vnodeopv_entry_desc*
QvrRetriveVnodeOpvEntryDescByVnodeOpDesc(
                                         __in vnodeopv_entry_desc*  vnodeOpvEntryDescArray,
                                         __in struct vnodeop_desc *opve_op
                                         )
{
    for( int i = 0x0; NULL != vnodeOpvEntryDescArray[ i ].opve_op; ++i ){
        
        if( opve_op == vnodeOpvEntryDescArray[ i ].opve_op )
            return &vnodeOpvEntryDescArray[ i ];
        
    }// end for
    
    return ( vnodeopv_entry_desc* )NULL;
}

//--------------------------------------------------------------------

struct vnodeopv_entry_desc*
QvrRetriveVnodeOpvEntryDescByVnodeOp(
                                     __in vnodeopv_entry_desc*  vnodeOpvEntryDescArray,
                                     __in VOPFUNC vnodeOp
                                     )
{
    for( int i = 0x0; NULL != vnodeOpvEntryDescArray[ i ].opve_op; ++i ){
        
        if( vnodeOp == vnodeOpvEntryDescArray[ i ].opve_impl )
            return &vnodeOpvEntryDescArray[ i ];
        
    }// end for
    
    return ( vnodeopv_entry_desc* )NULL;
}

//--------------------------------------------------------------------

VOPFUNC
QvrGetOriginalVnodeOp(
                      __in vnode_t      vnode,
                      __in QvrVopEnum   indx
                      )
{
    VOPFUNC             original = NULL;
    VOPFUNC*            v_op;
    QvrVnodeHookEntry*  existingEntry;
    
    assert( preemption_enabled() );
    assert( QvrVnodeHooksHashTable::sVnodeHooksHashTable );
    assert( QvrVopEnum_Unknown< indx && indx < QvrVopEnum_Max );
    
    v_op = QvrGetVnodeOpVector( vnode );
    
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->LockShared();
    {// start of the lock
        existingEntry = QvrVnodeHooksHashTable::sVnodeHooksHashTable->RetrieveEntry( v_op, true );
    }// end of the lock
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->UnLockShared();
    
    if( !existingEntry ){
        
        //
        // this is a case of an operations table unhooked in the midle of the hooking
        // function execution, so retrieve the current table entry, this case is
        // a very rare one ( I doubt that it will ever happen ) so there is no need
        // for any optimizahion here
        //
        
        //
        // perform the access under the lock protection to avoid a race condition with a possible ongoing hooking
        //
        QvrVnodeHooksHashTable::sVnodeHooksHashTable->LockShared();
        {// start of the lock
            
            //
            // check that the vnode has not been rehooked since the last check
            //
            existingEntry = QvrVnodeHooksHashTable::sVnodeHooksHashTable->RetrieveEntry( v_op, true );
            if( !existingEntry ){
                
                VOPFUNC*                 v_op;
                QvrVnodeOpvOffsetDesc*   offsetDescEntry;
                vnodeopv_entry_desc*     enumDescEntry;
                
                //
                // get the v_op vector's address
                //
                v_op = QvrGetVnodeOpVector( vnode );
                assert( v_op );
                
                //
                // get the enum descriptor for the index
                //
                enumDescEntry = QvrRetriveVnodeOpvEntryDescByVnodeOp( gQvrVnodeVopEnumEntries, (VOPFUNC)indx );
                assert( enumDescEntry );
                
                //
                // get the offset descriptor using the operation descriptor from the retrieved enum descriptor
                //
                offsetDescEntry = QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc( enumDescEntry->opve_op );
                assert( offsetDescEntry );
                assert( QVR_VOP_UNKNOWN_OFFSET != offsetDescEntry->offset );
                assert( enumDescEntry->opve_op == offsetDescEntry->opve_op );
                
                original =  *(VOPFUNC*)((vm_offset_t)v_op + offsetDescEntry->offset);
                
            }// end if( !existingEntry )
            
        }// end of the lock
        QvrVnodeHooksHashTable::sVnodeHooksHashTable->UnLockShared();
        
        if( !existingEntry ){
            
            assert( original );
            return original;
            
        }// end if( existingEntry )
        
    }
    
    assert( existingEntry );
    
    original = existingEntry->getOrignalVop( indx );
    
    existingEntry->release();
    
    assert( original );
    if( !original ){
        
        DBG_PRINT_ERROR( ("QvrGetOriginalVnodeOp(%p,%d) failed\n", (void*)vnode, (int)indx ) );
    }
    
    return original;
}

//--------------------------------------------------------------------

IOReturn
QvrHookVnodeVop(
                __inout vnode_t vnode,
                __inout bool* isVopHooked // if an error is returned the value is not defined
)
{
    VOPFUNC*  v_op;
    IOReturn  RC = kIOReturnSuccess;
    
    assert( preemption_enabled() );
    assert( NULL != QvrVnodeHooksHashTable::sVnodeHooksHashTable );
    assert( (VOPFUNC*)QVR_VOP_UNKNOWN_OFFSET != QvrGetVnodeOpVector( vnode ) );
    
    if( (VOPFUNC*)QVR_VOP_UNKNOWN_OFFSET == QvrGetVnodeOpVector( vnode ) || !QvrVnodeHooksHashTable::sVnodeHooksHashTable )
        return kIOReturnError;
    
    if( vnode_isrecycled( vnode ) ){
        
        //
        // the vnode has been put in a dead state
        // by vclean()
        // 	vp->v_mount = dead_mountp;
        //  vp->v_op = dead_vnodeop_p;
        //  vp->v_tag = VT_NON;
        //  vp->v_data = NULL;
        //
        // we should not hook dead_vnodeop_p as hooking
        // it results in processing vnode as a normal one
        // which is not true
        //
        return kIOReturnNoDevice;
        
    } // if( vnode_isrecycled( vnode ) )
    
    //
    // if you are changing the following condition
    // do not forget to change the one in QvrUnHookVnodeVop()
    //
    if( VREG != vnode_vtype( vnode ) ){
        
        //
        // we are interested only in disk related vnodes
        //
        *isVopHooked = false;
        return kIOReturnSuccess;
    }
    
    //
    // get the v_op vector's address
    //
    v_op = QvrGetVnodeOpVector( vnode );
    
    assert( NULL != v_op );
    assert( QvrVirtToPhys( (vm_offset_t)v_op ) );
    
    QvrVnodeHookEntry*  existingEntry = NULL;
    QvrVnodeHookEntry*  newEntry = NULL;
    
    //
    // check that the v_op has not been hooked already, the returned entry is not referenced
    //
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->LockShared();
    {// start of the lock
        
        //
        // get a referenced entry
        //
        existingEntry = QvrVnodeHooksHashTable::sVnodeHooksHashTable->RetrieveEntry( v_op, true );
        if( existingEntry ){
            
            //
            // account for the new vnode, the increment is an atomic
            // operation, and the hash is protected by the shared lock, so
            // unhook can't sneak in and unhook the vop table as before
            // unhooking reacquires the lock exclusive
            //
            existingEntry->incrementVnodeCounter();
        }
        
    }// end of the lock
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->UnLockShared();
    
    if( existingEntry ){
        
        //
        // already hooked
        //
        assert( 0x0 != existingEntry->getVnodeCounter() );
        
        //
        // RetrieveEntry() returns a referenced entry
        //
        existingEntry->release();
        *isVopHooked = true;
        
        return kIOReturnSuccess;
    }
    
    newEntry = QvrVnodeHookEntry::newEntry();
    assert( newEntry );
    if( !newEntry )
        return kIOReturnNoMemory;
    
    assert( kIOReturnSuccess == RC );
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->LockExclusive();
    {// start of the lock
        
        //
        // check again as the lock has been reacquired
        //
        existingEntry = QvrVnodeHooksHashTable::sVnodeHooksHashTable->RetrieveEntry( v_op, false );
        if( existingEntry ){
            
            //
            // account for the new vnode
            //
            existingEntry->incrementVnodeCounter();
            
            goto __exit_with_lock;
        }
        
        //
        // add a new entry before hook, as it is hard to unhook if the add fails
        // the hooks might be already in work waiting on the lock to get the
        // original function(!), as the lock is hold exclusively we are protected
        // from the entry access
        //
        if( !QvrVnodeHooksHashTable::sVnodeHooksHashTable->AddEntry( v_op, newEntry ) ){
            
            assert( !"QvrVnodeHooksHashTable::sVnodeHooksHashTable->AddEntry failed" );
            DBG_PRINT_ERROR( ( "QvrHookVnodeVop()->QvrVnodeHooksHashTable::sVnodeHooksHashTable->AddEntry failed\n" ) );
            
            RC = kIOReturnNoMemory;
            goto __exit_with_lock;
            
        }// end if
        
#if defined( DBG )
        OSIncrementAtomic( &gQvrVNodeHookCount );
#endif//DBG
        
        //
        // account for the new vnode
        //
        newEntry->incrementVnodeCounter();
        
        //
        // iterate through the registered hooks
        //
        for( int i = 0x0; NULL != gQvrVnodeVopHookEntries[ i ].opve_op; ++i ){
            
            QvrVnodeOpvOffsetDesc*   offsetDescEntry;
            vnodeopv_entry_desc*     enumDescEntry;
            
            offsetDescEntry = QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc( gQvrVnodeVopHookEntries[ i ].opve_op );
            assert( offsetDescEntry );
            if( !offsetDescEntry ){
                
                DBG_PRINT_ERROR( ( "QvrHookVnodeVop()->QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc(%d) failed\n", i ) );
                continue;
            }
            
            enumDescEntry = QvrRetriveVnodeOpvEntryDescByVnodeOpDesc( gQvrVnodeVopEnumEntries, offsetDescEntry->opve_op );
            assert( enumDescEntry );
            if( !enumDescEntry ){
                
                DBG_PRINT_ERROR( ( "QvrHookVnodeVop()->QvrRetriveVnodeOpvEntryDescByVnodeOpDesc(%d) failed\n", i ) );
                continue;
            }
            
            assert( offsetDescEntry->opve_op == enumDescEntry->opve_op );
            assert( QVR_VOP_UNKNOWN_OFFSET != offsetDescEntry->offset );
            assert( gQvrVnodeVopHookEntries[ i ].opve_impl );
            
            VOPFUNC       original;
            VOPFUNC       hook;
            QvrVopEnum    indx;
            unsigned int  bytes;
            
            original = *(VOPFUNC*)((vm_offset_t)v_op + offsetDescEntry->offset);
            hook = gQvrVnodeVopHookEntries[ i ].opve_impl;
            indx = *(QvrVopEnum*)(&enumDescEntry->opve_impl);// just to calm the compiler
            
            assert( hook && original );
            
            if( ( VREG != vnode_vtype( vnode ) && VDIR != vnode_vtype( vnode ) ) &&
               &vnop_strategy_desc == gQvrVnodeVopHookEntries[ i ].opve_op ){
                
                //
                // do not hook the device's vnode strategic routine as
                // it is hard to define the device's vnode from the strategic
                // routine's parameters, the only present vnode is for
                // the real FSD's vnode which initiated IO, using it is
                // impossible as this gives an infinite recursion - for
                // example in the HFS case the sequence of calls is
                /*
                 #169 0x0031db1a in buf_strategy (devvp=0x57b4f38, ap=0x31d237f4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/vfs_bio.c:951
                 #170 0x004c3e26 in hfs_vnop_strategy (ap=0x31d237f4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/hfs/hfs_readwrite.c:2539
                 #171 0x4642d0a3 in QvrFsdStrategytHook (ap=0x31d237f4) at /work/DeviceLockProject/DeviceLockIOKitDriver/QvrVNodeHook.cpp:583
                 #172 0x00359e34 in VNOP_STRATEGY (bp=0x2d833500) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/kpi_vfs.c:5885
                 #173 0x00322d3b in cluster_io (vp=0x7d85cb8, upl=0x604a480, upl_offset=4096, f_offset=4096, non_rounded_size=0, flags=5, real_bp=0x0, iostate=0x31d23a08, callback=0, callback_arg=0 0) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/vfs_cluster.c:1410
                 #174 0x00327736 in cluster_read_copy (vp=0x7d85cb8, uio=0x31d23e60, io_req_size=512, filesize=2097152, flags=2048, callback=0, callback_arg=0x0) at /work/Mac_OS_X_kernel/10_6_4/xnu 1504.7.4/bsd/vfs/vfs_cluster.c:3565
                 #175 0x0032896c in cluster_read_direct (vp=0x7d85cb8, uio=0x31d23e60, filesize=2097152, read_type=0x31d23c40, read_length=0x31d23c44, flags=2048, callback=0, callback_arg=0x0) at / ork/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/vfs_cluster.c:4197
                 #176 0x00326c21 in cluster_read_ext (vp=0x7d85cb8, uio=0x31d23e60, filesize=2097152, xflags=2048, callback=0, callback_arg=0x0) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs vfs_cluster.c:3257
                 #177 0x00326a7b in cluster_read (vp=0x7d85cb8, uio=0x31d23e60, filesize=2097152, xflags=2048) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/vfs_cluster.c:3207
                 #178 0x004c52d3 in hfs_vnop_read (ap=0x31d23d80) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/hfs/hfs_readwrite.c:174
                 #179 0x4642dae1 in QvrFsdReadHook (ap=0x31d23d80) at /work/DeviceLockProject/DeviceLockIOKitDriver/QvrVNodeHook.cpp:219
                 #180 0x0035717e in VNOP_READ (vp=0x7d85cb8, uio=0x31d23e60, ioflag=2048, ctx=0x31d23f0c) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/kpi_vfs.c:3458
                 #181 0x0034d237 in vn_read (fp=0x6348af0, uio=0x31d23e60, flags=0, ctx=0x31d23f0c) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/vfs/vfs_vnops.c:740
                 #182 0x00515bc0 in fo_read (fp=0x6348af0, uio=0x31d23e60, flags=0, ctx=0x31d23f0c) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/kern/kern_descrip.c:4773
                 #183 0x00544650 in dofileread (ctx=0x31d23f0c, fp=0x6348af0, bufp=4299495856, nbyte=512, offset=-1, flags=0, retval=0x65a4ae4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/kern sys_generic.c:353
                 #184 0x005441a3 in read_nocancel (p=0x57faa80, uap=0x64669e8, retval=0x65a4ae4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/kern/sys_generic.c:198
                 #185 0x005440ec in read (p=0x57faa80, uap=0x64669e8, retval=0x65a4ae4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/kern/sys_generic.c:181
                 #186 0x005b19b5 in unix_syscall64 (state=0x64669e4) at /work/Mac_OS_X_kernel/10_6_4/xnu-1504.7.4/bsd/dev/i386/systemcalls.c:365
                 */
                // so as you see at the frame 169 the specfs's(redirection from devfs) strategy routine should be called but a vnode is not
                // provided here
                //
                newEntry->setOriginalVopAsNotHooked( indx );
                continue;
            }
            
            //
            // save the original function before changing it
            //
            newEntry->setOriginalVop( indx, original );
            
            //
            // change to the hooking function
            //
            bytes = QvrWriteWiredSrcToWiredDst( (vm_offset_t)&hook,
                                               (vm_offset_t)v_op + offsetDescEntry->offset,
                                               sizeof( VOPFUNC ) );
            
            assert( sizeof( VOPFUNC ) == bytes );
            
        }// end for
        
        
    __exit_with_lock:;
        
    }// end of the lock
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->UnLockExclusive();
    
    assert( newEntry );
    
    //
    // the entry has been referenced by AddEntry() or was not added at all,
    // in any case it must be released
    //
    newEntry->release();
    
    if( kIOReturnSuccess == RC ){
        
        *isVopHooked = true;
        VNodeMap::addHookedVnode( vnode );
    }
    
    return RC;
}

//--------------------------------------------------------------------

void
QvrUnHookVnodeVop(
                  __inout vnode_t vnode
                  )
{
    VOPFUNC*  v_op;
    
    assert( preemption_enabled() );
    assert( NULL != QvrVnodeHooksHashTable::sVnodeHooksHashTable );
    
    //
    // see QvrHookVnodeVop(), we are not hooking all vnodes
    //
    if( VREG != vnode_vtype( vnode ) || !VNodeMap::isVnodeHooked( vnode ) ){
        
        //
        // we are interested only in disk related vnodes
        //
        return;
    }
    
    if( (VOPFUNC*)QVR_VOP_UNKNOWN_OFFSET == QvrGetVnodeOpVector( vnode ) || !QvrVnodeHooksHashTable::sVnodeHooksHashTable )
        return;
    
    //
    // get the v_op vector's address
    //
    v_op = QvrGetVnodeOpVector( vnode );
    
    assert( NULL != v_op );
    assert( QvrVirtToPhys( (vm_offset_t)v_op ) );
    
    QvrVnodeHookEntry*  existingEntry = NULL;
    
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->LockShared();
    {// start of the lock
        existingEntry = QvrVnodeHooksHashTable::sVnodeHooksHashTable->RetrieveEntry( v_op, true );
    }// end of the lock
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->UnLockShared();
    
    if( !existingEntry )
        return;
    
    VNodeMap::removeHookedVnode( vnode );
    
    //
    // fast check
    //
    if( 0x1 != existingEntry->decrementVnodeCounter() ){
        
        existingEntry->release();
        return;
    }
    
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->LockExclusive();
    {// start of the lock
        
        //
        // the counter might have been incremented as the entry has not been removed
        // from the hash table
        //
        if( 0x0 != existingEntry->getVnodeCounter() )
            goto __exit_with_lock;
        
        existingEntry->release();
        existingEntry = NULL;
        
        //
        // remove the entry for the vnode operations, the returned entry is referenced,
        // the returned entry might be NULL as there is a time slot when the race is possible
        // i.e. when the shared lock is released and the exclusive lock is not yet acquired
        // the concurrent thread might sneak in and remove the entry from the hash table and unhook
        // the vnode operations vector
        //
        existingEntry = QvrVnodeHooksHashTable::sVnodeHooksHashTable->RemoveEntry( v_op );
        if( !existingEntry )
            goto __exit_with_lock;
        
        assert( 0x0 == existingEntry->getVnodeCounter() );
        
#if defined( DBG )
        assert( 0x0 != gQvrVNodeHookCount );
        OSDecrementAtomic( &gQvrVNodeHookCount );
#endif//DBG
        
        //
        // iterate through the registered hooks
        //
        for( int i = 0x0; NULL != gQvrVnodeVopHookEntries[ i ].opve_op; ++i ){
            
            QvrVnodeOpvOffsetDesc*   offsetDescEntry;
            vnodeopv_entry_desc*     enumDescEntry;
            
            offsetDescEntry = QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc( gQvrVnodeVopHookEntries[ i ].opve_op );
            assert( offsetDescEntry );
            if( !offsetDescEntry ){
                
                DBG_PRINT_ERROR( ( "QvrHookVnodeVop()->QvrRetriveVnodeOpvOffsetDescByVnodeOpDesc(%d) failed\n", i ) );
                continue;
            }
            
            enumDescEntry = QvrRetriveVnodeOpvEntryDescByVnodeOpDesc( gQvrVnodeVopEnumEntries, offsetDescEntry->opve_op );
            assert( enumDescEntry );
            if( !enumDescEntry ){
                
                DBG_PRINT_ERROR( ( "QvrHookVnodeVop()->QvrRetriveVnodeOpvEntryDescByVnodeOpDesc(%d) failed\n", i ) );
                continue;
            }
            
            assert( offsetDescEntry->opve_op == enumDescEntry->opve_op );
            assert( QVR_VOP_UNKNOWN_OFFSET != offsetDescEntry->offset );
            assert( gQvrVnodeVopHookEntries[ i ].opve_impl );
            
            VOPFUNC       original;
            QvrVopEnum    indx;
            unsigned int  bytes;
            
            indx = *(QvrVopEnum*)(&enumDescEntry->opve_impl);// just to calm the compiler
            
            //
            // the hooking could have been deliberately skipped, as in the case of the
            // strategic routine for specfs
            //
            if( !existingEntry->isHooked( indx ) )
                continue;
            
            original = existingEntry->getOrignalVop( indx );
            assert( original );
            
            //
            // restore to the original function
            //
            bytes = QvrWriteWiredSrcToWiredDst( (vm_offset_t)&original,
                                               (vm_offset_t)v_op + offsetDescEntry->offset,
                                               sizeof( VOPFUNC ) );
            
            assert( sizeof( VOPFUNC ) == bytes );
            
        }// end for
        
        
    __exit_with_lock:;
        
    }// end of the lock
    QvrVnodeHooksHashTable::sVnodeHooksHashTable->UnLockExclusive();
    
    //
    // the entry returned by RemoveEntry() or RetrieveEntry() is referenced
    //
    if( existingEntry )
        existingEntry->release();
}

//--------------------------------------------------------------------

void
QvrHookVnodeVopAndParent(
    __inout vnode_t vnode
    )
{
    bool isHooked;
    IOReturn  RC;
    
    RC = QvrHookVnodeVop( vnode, &isHooked );
    assert( kIOReturnSuccess == RC || kIOReturnNoDevice == RC );
    
    vnode_t  parent = vnode_getparent( vnode );
    if( parent ){
        
        RC = QvrHookVnodeVop( parent, &isHooked );
        assert( kIOReturnSuccess == RC || kIOReturnNoDevice == RC );
        vnode_put( parent );
    }
}
                

void
QvrUnHookVnodeVopAndParent(
    __inout vnode_t vnode
    )
{
    QvrUnHookVnodeVop( vnode );
    
    vnode_t  parent = vnode_getparent( vnode );
    if( parent ){
        
        QvrUnHookVnodeVop( parent );
        vnode_put( parent );
    }
}

//--------------------------------------------------------------------

QvrVnodeHooksHashTable*
QvrVnodeHooksHashTable::withSize( int size, bool non_block )
{
    QvrVnodeHooksHashTable* vNodeHooksHashTable;
    
    assert( preemption_enabled() );
    
    vNodeHooksHashTable = new QvrVnodeHooksHashTable();
    assert( vNodeHooksHashTable );
    if( !vNodeHooksHashTable )
        return NULL;
    
    vNodeHooksHashTable->RWLock = IORWLockAlloc();
    assert( vNodeHooksHashTable->RWLock );
    if( !vNodeHooksHashTable->RWLock ){
        
        delete vNodeHooksHashTable;
        return NULL;
    }
    
    vNodeHooksHashTable->HashTable = ght_create( size, non_block );
    assert( vNodeHooksHashTable->HashTable );
    if( !vNodeHooksHashTable->HashTable ){
        
        IORWLockFree( vNodeHooksHashTable->RWLock );
        vNodeHooksHashTable->RWLock = NULL;
        
        delete vNodeHooksHashTable;
        return NULL;
    }
    
    return vNodeHooksHashTable;
}

//--------------------------------------------------------------------

#define super OSObject

OSDefineMetaClassAndStructors( QvrVnodeHookEntry, OSObject )

VOPFUNC  QvrVnodeHookEntry::vopNotHooked = (VOPFUNC)(-1);

//--------------------------------------------------------------------

bool QvrVnodeHookEntry::init()
{
    if( !super::init() ){
        
        assert( !"something awful happened with the system as OSObject::init failed" );
        return false;
    }
    
    this->vNodeCounter = 0x0;
    bzero( this->origVop, sizeof( this->origVop ) );
    
#if defined( DBG )
    this->inHash       = false;
#endif
    
    return true;
};


void QvrVnodeHookEntry::free()
{
    assert( 0x0 == this->vNodeCounter );
#if defined( DBG )
    assert( !this->inHash );
#endif
    
    super::free();
};

//--------------------------------------------------------------------

bool
QvrVnodeHooksHashTable::CreateStaticTableWithSize( int size, bool non_block )
{
    assert( !QvrVnodeHooksHashTable::sVnodeHooksHashTable );
    
    QvrVnodeHooksHashTable::sVnodeHooksHashTable = QvrVnodeHooksHashTable::withSize( size, non_block );
    assert( QvrVnodeHooksHashTable::sVnodeHooksHashTable );
    
    return ( NULL != QvrVnodeHooksHashTable::sVnodeHooksHashTable );
}

void
QvrVnodeHooksHashTable::DeleteStaticTable()
{
    if( NULL != QvrVnodeHooksHashTable::sVnodeHooksHashTable ){
        
        QvrVnodeHooksHashTable::sVnodeHooksHashTable->free();
        
        delete QvrVnodeHooksHashTable::sVnodeHooksHashTable;
        
        QvrVnodeHooksHashTable::sVnodeHooksHashTable = NULL;
    }// end if
}

//--------------------------------------------------------------------

void
QvrVnodeHooksHashTable::free()
{
    ght_hash_table_t* p_table;
    ght_iterator_t iterator;
    void *p_key;
    ght_hash_entry_t *p_e;
    
    assert( preemption_enabled() );
    
    p_table = this->HashTable;
    assert( p_table );
    if( !p_table )
        return;
    
    this->HashTable = NULL;
    
    for( p_e = (ght_hash_entry_t*)ght_first( p_table, &iterator, (const void**)&p_key );
        NULL != p_e;
        p_e = (ght_hash_entry_t*)ght_next( p_table, &iterator, (const void**)&p_key ) ){
        
        assert( !"Non emprty hash!" );
        DBG_PRINT_ERROR( ("QvrVnodeHooksHashTable::free() found an entry for an object(0x%p)\n", *(void**)p_key ) );
        
        QvrVnodeHookEntry* entry = (QvrVnodeHookEntry*)p_e->p_data;
        assert( entry );
        entry->release();
        
        p_table->fn_free( p_e, p_e->size );
    }
    
    ght_finalize( p_table );
    
    IORWLockFree( this->RWLock );
    this->RWLock = NULL;
    
}

//--------------------------------------------------------------------

bool
QvrVnodeHooksHashTable::AddEntry(
                                 __in VOPFUNC* v_op,
                                 __in QvrVnodeHookEntry* entry
                                 )
/*
 the caller must allocate space for the entry and
 free it only after removing the entry from the hash,
 the entry is referenced, so a caller can release it
 */
{
    GHT_STATUS_CODE RC;
    
    RC = ght_insert( this->HashTable, entry, sizeof( v_op ), &v_op );
    assert( GHT_OK == RC );
    if( GHT_OK != RC ){
        
        DBG_PRINT_ERROR( ( "QvrVnodeHooksHashTable::AddEntry->ght_insert( 0x%p, 0x%p ) failed RC = 0x%X\n",
                          (void*)v_op, (void*)entry, RC ) );
    } else {
        
        entry->retain();
#if defined( DBG )
        entry->inHash = true;
#endif//DBG
    }
    
    return ( GHT_OK == RC );
}

//--------------------------------------------------------------------

QvrVnodeHookEntry*
QvrVnodeHooksHashTable::RemoveEntry(
                                    __in VOPFUNC* v_op
                                    )
/*
 the returned entry is referenced!
 */
{
    QvrVnodeHookEntry* entry;
    
    //
    // the entry was refernced when was added to the hash table
    //
    entry = (QvrVnodeHookEntry*)ght_remove( this->HashTable, sizeof( v_op ), &v_op );
    
#if defined( DBG )
    if( entry ){
        
        assert( true == entry->inHash );
        entry->inHash = false;
        
    }
#endif//DBG
    
    return entry;
}

//--------------------------------------------------------------------

QvrVnodeHookEntry*
QvrVnodeHooksHashTable::RetrieveEntry(
                                      __in VOPFUNC* v_op,
                                      __in bool reference
                                      )
/*
 the returned entry is referenced if the refernce's value is "true"
 */
{
    QvrVnodeHookEntry* entry;
    
    entry = (QvrVnodeHookEntry*)ght_get( this->HashTable, sizeof( v_op ), &v_op );
    
    if( entry && reference )
        entry->retain();
    
    return entry;
}

//--------------------------------------------------------------------

