//
//  Kauth.cpp
//  VFSFilter0
//
//  Created by slava on 3/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#include "Kauth.h"
#include "VFSHooks.h"
#include "VFSFilter0.h"
#include "VNodeHook.h"
#include "ApplicationsData.h"
#include "VNode.h"
#include "WaitingList.h"
#include "VersionDependent.h"

//--------------------------------------------------------------------

QvrIOKitKAuthVnodeGate*     gVnodeGate;

//--------------------------------------------------------------------

#define super OSObject
OSDefineMetaClassAndStructors( QvrIOKitKAuthVnodeGate, OSObject)

//--------------------------------------------------------------------

IOReturn
QvrIOKitKAuthVnodeGate::RegisterVnodeScopeCallback(void)
{    
    //
    // register our listener
    //
    this->VnodeListener = kauth_listen_scope( KAUTH_SCOPE_VNODE,                              // for the vnode scope
                                              QvrIOKitKAuthVnodeGate::VnodeAuthorizeCallback, // using this callback
                                              this );                                         // give a cookie to callback
    
    if( NULL == this->VnodeListener ){
        
        DBG_PRINT_ERROR( ( "kauth_listen_scope failed\n" ) );
        return kIOReturnInternalError;
        
    }
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

int
QvrIOKitKAuthVnodeGate::VnodeAuthorizeCallback(
                                                  kauth_cred_t    credential, // reference to the actor's credentials
                                                  void           *idata,      // cookie supplied when listener is registered
                                                  kauth_action_t  action,     // requested action
                                                  uintptr_t       arg0,       // the VFS context
                                                  uintptr_t       arg1,       // the vnode in question
                                                  uintptr_t       arg2,       // parent vnode, or NULL
                                                  uintptr_t       arg3)       // pointer to an errno value
{
    QvrIOKitKAuthVnodeGate*    _this;
    vnode_t                    vnode = (vnode_t)arg1;
    
    assert( preemption_enabled() );
    
    //
    // if this is a dead vnode then skip it
    //
    if( vnode_isrecycled( vnode ) )
        return KAUTH_RESULT_DEFER;
    
    _this = (QvrIOKitKAuthVnodeGate*)idata;
    
    //
    // VNON vnode is created by devfs_devfd_lookup() for /dev/fd/X vnodes that
    // are not of any interest for us
    // VSOCK is created for UNIX sockets
    // etc.
    //
    enum vtype   vnodeType = vnode_vtype( vnode );    
    if( VREG != vnodeType &&
        VDIR != vnodeType )
        return KAUTH_RESULT_DEFER;
    
    const ApplicationData* appData = QvrGetApplicationDataByContext( (vfs_context_t)arg0, ADT_OpenExisting );
    if( appData ){
        
        QvrHookVnodeVopAndParent( vnode );
        cache_purge( vnode );
    }
    
    const ApplicationData* appDataForVnode = VNodeMap::getVnodeAppData( vnode );
    
    //
    // check that a trusted application is trying to open a protected file
    //
    bool allowAccess = true; //( appDataForVnode == NULL ) || ( appData == appDataForVnode );
    
    return allowAccess? KAUTH_RESULT_DEFER : KAUTH_RESULT_DENY;
}

//--------------------------------------------------------------------

int
QvrIOKitKAuthVnodeGate::MacVnodeCheckLookup( kauth_cred_t cred,
                        struct vnode *dvp,
                        struct label *dlabel,
                        struct componentname *cnp
                        )
{
    const ApplicationData* appData = QvrGetApplicationDataByContext( vfs_context_current(), ADT_OpenExisting );
    
    if( ! appData || RecursionEngine::IsRecursiveCall() )
        return 0;
    
    //
    // Force lookup to go to the filesystem, vnode_lookup() and
    // cache_purge( vnode ) can't be called here
    // as the caller holds nonrecursive namei lock
    // that is reacquired by vnode_lookup and cache_purge,
    // FYI a callstack with a held lock and a deadlock
    // in cache_purge
    /*
     0xffffff809d883820 0xffffff800501a30f machine_switch_context((thread_t) old = 0xffffff80132babf0, (thread_continue_t) continuation = 0x0000000000000000, (thread_t) new = 0xffffff801367da80)
     0xffffff809d8838b0 0xffffff8004f52ddc thread_invoke((thread_t) self = 0xffffff80132babf0, (thread_t) thread = <register rax is not available>, , (ast_t) reason = <>, )
     0xffffff809d8838f0 0xffffff8004f5084f thread_block_reason((thread_continue_t) continuation = <>, , (void *) parameter = <>, , (ast_t) reason = <Unimplemented opcode DW_OP_piece.>, )
     0xffffff809d883960 0xffffff8005016a08 lck_rw_lock_exclusive_gen((lck_rw_t *) lck = 0xffffff801021a200)
     0xffffff809d883990 0xffffff80051333d8 name_cache_lock [inlined](void)
     0xffffff809d883990 0xffffff80051333cc cache_purge((vnode_t) vp = 0xffffff80130591e0)
     0xffffff809d8839e0 0xffffff7f876eb696 QvrIOKitKAuthVnodeGate::MacVnodeCheckLookup(ucred*, vnode*, label*, componentname*)((kauth_cred_t) cred = 0xffffff8012f5eea0, (vnode *) dvp = 0xffffff80102f6960, (label *) dlabel = 0x0000000000000000, (componentname *) cnp = 0xffffff809d883e88)
     0xffffff809d883a20 0xffffff800553889e mac_vnode_check_lookup((vfs_context_t) ctx = <>, , (vnode *) dvp = 0xffffff80102f6960, (componentname *) cnp = 0xffffff809d883e88)
     0xffffff809d883aa0 0xffffff8005132352 cache_lookup_path((nameidata *) ndp = 0xffffff809d883d38, (componentname *) cnp = <register rdx is not available>, , (vnode_t) dp = <>, , (vfs_context_t) ctx = 0xffffff80133bf3f0, (int *) dp_authorized = 0xffffff809d883b04, (vnode_t) last_dp = 0x0000000000000000)
     0xffffff809d883b80 0xffffff800513efed lookup((nameidata *) ndp = 0xffffff809d883d38)
     0xffffff809d883cb0 0xffffff800513ea95 namei((nameidata *) ndp = 0xffffff809d883d38)
     0xffffff809d883d00 0xffffff8005152005 nameiat((nameidata *) ndp = 0xffffff809d883d38, (int) dirfd = <>, )
     0xffffff809d883f00 0xffffff8005129fd6 getattrlistat_internal((vfs_context_t) ctx = 0xffffff80133bf3f0, (user_addr_t) path = <register rdx is not available>, , (attrlist *) alp = 0xffffff809d883f28, (user_addr_t) attributeBuffer = 3220249968, (size_t) bufferSize = 918, (uint64_t) options = 37, (uio_seg) segflg = <no location, value may have been optimized out>, , (uio_seg) pathsegflg = <>, , (int) fd = <register rsi is not available>, )
     0xffffff809d883f50 0xffffff8005123227 getattrlist((proc_t) p = <>, , (getattrlist_args *) uap = <>, , (int32_t *) retval = <>, )
     0xffffff809d883fb0 0xffffff800544d924 unix_syscall((x86_saved_state_t *) state = 0xffffff8013299020)
     */
    //
    // N.B. the same is true for every function that calls namei() e.g. vnode_lookup() will deadlock if called in MacVnodeCheckLookup
    //
    
    //
    // instead of cache_purge use a flag that disables cache_lookup_locked() calling,
    // CN_SKIPNAMECACHE is a private flag and its value must be verified for each Mac OS X major release
    //
    cnp->cn_flags |= CN_SKIPNAMECACHE;
    
    return 0;
}

//--------------------------------------------------------------------

QvrIOKitKAuthVnodeGate* QvrIOKitKAuthVnodeGate::withCallbackRegistration(
    __in com_VFSFilter0* _provider
    )
/*
 the caller must call the release() function for the returned object when it is not longer needed
 */
{
    IOReturn                   RC;
    QvrIOKitKAuthVnodeGate*    pKAuthVnodeGate;
    
    pKAuthVnodeGate = new QvrIOKitKAuthVnodeGate();
    assert( pKAuthVnodeGate );
    if( !pKAuthVnodeGate ){
        
        DBG_PRINT_ERROR( ( "QvrIOKitKAuthVnodeGate::withCallbackRegistration QvrIOKitKAuthVnodeGate allocation failed\n" ) );
        return NULL;
    }
    
    //
    // IOKit base classes initialization
    //
    if( !pKAuthVnodeGate->init() ){
        
        DBG_PRINT_ERROR( ( "QvrIOKitKAuthVnodeGate::withCallbackRegistration init() failed\n" ) );
        pKAuthVnodeGate->release();
        return NULL;
    }
    
    pKAuthVnodeGate->macPolicy = QvrMacPolicy::createPolicyObject( &pKAuthVnodeGate->mpcServiceProtection, NULL );
    assert( pKAuthVnodeGate->macPolicy );
    if( !pKAuthVnodeGate->macPolicy ){
        
        DBG_PRINT_ERROR( ( "QvrMacPolicy::createPolicyObject() failed\n" ) );
        pKAuthVnodeGate->release();
        return NULL;
    }
    
    pKAuthVnodeGate->provider = _provider;
    
    //
    // register the callback, it will be active immediatelly after registration, i.e. before control leaves the function
    //
    RC = pKAuthVnodeGate->RegisterVnodeScopeCallback();
    assert( kIOReturnSuccess == RC );
    if( kIOReturnSuccess != RC ){
        
        DBG_PRINT_ERROR( ( "pKAuthVnodeGate->RegisterVnodeScopeCallback() failed with the 0x%X error\n", RC ) );
        pKAuthVnodeGate->release();
        return NULL;
    }
    
    pKAuthVnodeGate->macPolicy->registerMacPolicy();
    
    return pKAuthVnodeGate;
}

//--------------------------------------------------------------------

bool QvrIOKitKAuthVnodeGate::init()
{
    if(! super::init() )
        return false;
    
    mpoServiceProtection.mpo_vnode_check_lookup = &QvrIOKitKAuthVnodeGate::MacVnodeCheckLookup;
    
    mpcServiceProtection.mpc_name               = "MYFILTER";
    mpcServiceProtection.mpc_fullname           = "My Filter MAC";
    mpcServiceProtection.mpc_field_off          = NULL;		/* no label slot */
    mpcServiceProtection.mpc_labelnames         = NULL;		/* no policy label names */
    mpcServiceProtection.mpc_labelname_count    = 0;		/* count of label names is 0 */
    mpcServiceProtection.mpc_ops                = &mpoServiceProtection;	/* policy operations */
    mpcServiceProtection.mpc_loadtime_flags     = 0;
    mpcServiceProtection.mpc_runtime_flags      = 0;
    
    return true;
}

//--------------------------------------------------------------------

void QvrIOKitKAuthVnodeGate::free()
{
    if( macPolicy ){
        macPolicy->unRegisterMacPolicy();
        macPolicy->release();
    }
    
    super::free();
}

//--------------------------------------------------------------------

void QvrIOKitKAuthVnodeGate::sendVFSDataToClient( __in struct _VFSData* data )
{
    provider->sendVFSDataToClient( data );
}

//--------------------------------------------------------------------

