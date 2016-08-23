//
//  VFSHooks.cpp
//  VFSFilter0
//
//  Created by slava on 3/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifdef __cplusplus
extern "C" {
#endif
    
#include <vfs/vfs_support.h>
#include <sys/ubc.h>
#include <sys/buf.h>
#include <sys/vm.h>
    
#ifdef __cplusplus
}
#endif

#include "VFSHooks.h"
#include "VmPmap.h"
#include "VFSFilter0UserClient.h"
#include "Kauth.h"
#include "RecursionEngine.h"
#include <sys/fcntl.h>
#include "VNode.h"
#include "VNodeHook.h"
#include "VersionDependent.h"
#include "ApplicationsData.h"

//--------------------------------------------------------------------

static bool gSynchroniseWithOriginal = true;
static bool gRedirectionIsNullAndVoid = false; // TEST

//--------------------------------------------------------------------

errno_t
QvrReadInCacheFromBackingFile(
                              __in vnode_t  vnode,
                              __in vnode_t  vnodeIO,
                              __in off_t  offset,
                              __in size_t size
                              );

//--------------------------------------------------------------------

IOReturn
VFSHookInit()
{
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

void
VFSHookRelease()
{
}

//--------------------------------------------------------------------

#define SHADOW_PREFIX  ".QS_"

errno_t
QvrConvertToShadowCopyPath(
    __in  const char* path,
    __out char**      _shadowPath,
    __out vm_size_t*  _shadowPathSize
    )
/*
 converts a path by adding SHADOW_PREFIX to a file name, e.g.
 /a/b/c/file.docxx -> /a/b/c/.QS_file.docxx
 OR
 name.docxx -> .QS_name.docxx
 */
{
    size_t      pathLen;
    size_t      shadowPathLen;
    size_t      prefixLen;
    vm_size_t   shadowPathSize;
    char*       shadowPath;
    
    pathLen = strlen( path );
    prefixLen = strlen( SHADOW_PREFIX );
    shadowPathLen = pathLen + prefixLen;
    shadowPathSize = shadowPathLen + sizeof('\0');
    
    shadowPath = (char*)IOMalloc( shadowPathSize );
    assert( shadowPath );
    if( ! shadowPath )
        return ENOMEM;
    
    assert( pathLen < shadowPathLen &&  shadowPathLen < shadowPathSize );
    
    memcpy( shadowPath, path, pathLen + sizeof('\0'));
    
    //
    // now move file name right to prefixLen positions
    // and squeeze the prefix
    //
    int   pos = pathLen;
    while( pos != (-1) && shadowPath[ pos ] != '/' ){
        pos -= 1;
    }
    
    //
    // pos points to the most right '/' or one position in front of string
    // if there is no slashes in the path, i.e. a file name was provided instead
    // the fully qualified path
    //
    pos += 1;
    
    if( 0x0 == strncasecmp( &shadowPath[ pos ], SHADOW_PREFIX, prefixLen ) ){
        
        //
        // this is already a shadow path/name
        //
        goto __exit;
    }
    
    //
    // move the file name to the right, do not forget about terminating zero
    //
    memmove( &shadowPath[ pos+prefixLen ], &shadowPath[ pos ], (pathLen + sizeof('\0'))- pos);
    
    //
    // squeeze in the prefix
    //
    memcpy( &shadowPath[ pos ], SHADOW_PREFIX, prefixLen );
    
    assert( strlen( shadowPath ) == (strlen( path ) + strlen( SHADOW_PREFIX )) );
    assert( strlen( shadowPath ) < shadowPathSize );
    
__exit:
    
    *_shadowPath = shadowPath;
    *_shadowPathSize = shadowPathSize;
    
    return 0;
}

void
QvrFreeShadowPath(
    __in char*   shadowPath,
    __in size_t  shadowPathSize
    )
{
    IOFree( shadowPath, shadowPathSize );
}

//--------------------------------------------------------------------

bool
QvrIsExtensionEqual(
    __in const char* path,
    __in const char* extension
)
{
    size_t  pathLen = strlen( path );
    size_t  extLen  = strlen( extension );
    
    if( extLen >= pathLen )
        return false;
    
    return 0x0 == strncasecmp( &path[ pathLen - extLen ], extension, extLen );
}

//--------------------------------------------------------------------

errno_t
QvrConvertToRedirectedPath(
    __in const char*  file, // a file name stripped from the directories , i.e. "file" not /A/B/C/file, may contain '/' as a prefix
    __in const char*  redirectedDir, // without the terminating '/' , may be emty "" but not NULL
    __out char**      _redirectedFilePath,
    __out vm_size_t*  _nameBufferSize
    )
{
    assert( 0 == strlen(redirectedDir) || '/' != redirectedDir[strlen(redirectedDir) - 1] );
    
    const char*  prefix = redirectedDir;
    size_t       prefixLength = strlen(prefix);
    bool         slashRequired = ( 0 == prefixLength || file[0] == '/' || file[0] == '\0') ?  false : true;
    size_t       slashLength = (slashRequired ? sizeof('/') : 0);
    vm_size_t    nameBufferSize = prefixLength + slashLength + strlen(file) + sizeof(L'\0');
    char*        redirectedFilePath = (char*)IOMalloc( nameBufferSize );
    
    assert( redirectedFilePath );
    if( ! redirectedFilePath )
        return ENOMEM;
    
    if( prefixLength ){
        
        memcpy( redirectedFilePath, prefix, prefixLength );
        
        if( slashRequired )
            redirectedFilePath[ prefixLength ] = '/';
        
    } else {
        
        //
        // a caller wants to convert only the file name,
        // by providing a full path with an empty redirectedDir,
        // in that case a leading '/' must be removed
        //
        while( '/' == file[ 0 ] )
            file = &file[ 1 ];
        
        //
        // in that case the slash doesn't make sense
        //
        assert( ! slashRequired );
    }
    
    memcpy( redirectedFilePath + prefixLength + slashLength, file, strlen(file) + sizeof('\0') );
    
    //
    // convert directories to be part of the name by replacing / to $
    //
    size_t i = prefixLength + slashLength;
    
    //
    // leading / in the name is not harmfull
    //
    while( '/' == redirectedFilePath[ i ] ) ++i;
    
    //
    // replace '/' to $
    //
    while( '\0' != redirectedFilePath[ i ] )
    {
        if( '/' == redirectedFilePath[ i ] )
            redirectedFilePath[ i ] = '$';
        
        ++i;
    } // end while
    
    *_redirectedFilePath = redirectedFilePath;
    *_nameBufferSize     = nameBufferSize;
    return 0;
}

void
QvrFreeRedirectedPath(
    __in char*   redirectedFilePath,
    __in size_t  nameBufferSize
    )
{
    IOFree( redirectedFilePath, nameBufferSize );
}

//--------------------------------------------------------------------

errno_t
QvrConvertToShadowAndThenRedirectedPath(
    __in const char*  path, // a file name stripped from the directories , i.e. "file" not /A/B/C/file, may contain '/' as a prefix
    __in const char*  redirectedDir, // without the terminating '/' , may be emty "" but not NULL
    __out char**      redirectedFilePath,
    __out vm_size_t*  redirectedFilePathSize
    )
{
    errno_t    error;
    char*      shadowPath;
    vm_size_t  shadowPathSize;
    
    
    error = QvrConvertToShadowCopyPath( path,
                                        &shadowPath,
                                        &shadowPathSize );
    assert( ! error );
    if( error )
        return error;
    
    error = QvrConvertToRedirectedPath( shadowPath,
                                        redirectedDir,
                                        redirectedFilePath,
                                        redirectedFilePathSize );
    assert( ! error );
    
    QvrFreeShadowPath( shadowPath, shadowPathSize );
    
    return error;
}

//--------------------------------------------------------------------

static
vnode_t
QvrGetBackingVnodeForRedirectedIO(
    __in vnode_t   vn,
    __in_opt const ApplicationData* appData,
    __in bool      forceUpdate
    )
/*
 a caller must release the vnode with vnode_put()
 */
{
    vnode_t   backingVnode = NULLVP;
    char*     redirectedFilePath = NULL;
    vm_size_t nameBufferSize = 0;
    char*     vnodePath = NULL;
    int       vnodePathLength = MAXPATHLEN;
    int       error;
    
    if( VREG != vnode_vtype( vn ) || (appData && !appData->redirectIO) )
        goto __exit;
    
    if( forceUpdate ){
        
        VNodeMap::removeVnodeIO( vn );
        
    } else {
        
        backingVnode = VNodeMap::getVnodeIORef( vn );
        if( backingVnode )
            goto __exit;
    }
    
    vnodePath = (char*)IOMalloc( MAXPATHLEN );
    assert( vnodePath );
    if( ! vnodePath )
        goto __exit;
    
    //
    // query vnode's name
    //
    error = vn_getpath( vn, vnodePath, &vnodePathLength );
    if( error )
        goto __exit;
    
    error = QvrConvertToRedirectedPath( vnodePath, //GetVnodeNamePtr(vn),
                                        appData->redirectTo,
                                        &redirectedFilePath,
                                        &nameBufferSize );
    if( error )
        goto __exit;
    
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        error = vnode_lookup( redirectedFilePath,
                              0x0, //VNODE_LOOKUP_NOFOLLOW,
                              &backingVnode,
                              gSuperUserContext );
    }
    RecursionEngine::LeaveRecursiveCall();
    if( error )
        goto __exit;
    
    VNodeMap::addVnodeIO( vn, backingVnode );
    
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        //
        // refill the cache with the new data from the backing file
        // as the cache might contain invalid data read in by mdworker or similar
        // appication prefetching data
        //
        off_t  fileSize;
        
        error = QvrVnodeGetSize( vn, &fileSize, gSuperUserContext );
        assert( ! error );
        if( ! error ){
            
            //
            // invalidate all exisiting pages, i.e. make all pages non resident
            //
            ubc_msync(vn, 0, i386_round_page(fileSize), NULL, UBC_INVALIDATE);
            
            //
            // if there is something left then overwrite it, the current solution is a brute force one
            // by reading the whole file in the cache, TO DO modify only resident pages
            //
            if( ubc_pages_resident( vn ) ){
                
                error = QvrReadInCacheFromBackingFile( vn, backingVnode, 0,  i386_round_page(fileSize) );
                assert( ! error );
            }
        } // end if( ! error )
    }
    RecursionEngine::LeaveRecursiveCall();
    
__exit:
    
    if( redirectedFilePath )
        QvrFreeRedirectedPath( redirectedFilePath, nameBufferSize );
    
    if( vnodePath )
        IOFree( vnodePath, MAXPATHLEN );
    
    return backingVnode;
}

void
QvrAssociateVnodeForRedirectedIO(
    __in vnode_t                vn,
    __in const ApplicationData* appData,
    __in bool                   forceUpdate
    )

{
    //
    // QvrGetBackingVnodeForRedirectedIO puts an entry into VNodeMap
    //
    assert( appData->redirectIO );
    vnode_t vnodeIO = QvrGetBackingVnodeForRedirectedIO( vn, appData, forceUpdate );
    if( vnodeIO )
        vnode_put( vnodeIO );
}

//--------------------------------------------------------------------

int
QvrVnopLookupHookEx2(
    __inout struct vnop_lookup_args *ap
    )
/*
 struct vnop_lookup_args {
 struct vnodeop_desc *a_desc;
 struct vnode *a_dvp;
 struct vnode **a_vpp;
 struct componentname *a_cnp;
 vfs_context_t a_context;
 } *ap)
 */
{
    int (*origVnop)(struct vnop_lookup_args *ap);
    
    origVnop = (int (*)(struct vnop_lookup_args*))QvrGetOriginalVnodeOp( ap->a_dvp, QvrVopEnum_lookup );
    assert( origVnop );
    
    //
    // the logic is as follows
    // - get an application data for ADT_CreateNew to know whether redirectIO might be employed
    // - if redirectIO is applicable get original vnode
    // - check whether the original vnode has an association with ADT_CreateNew data
    // - if ADT_CreateNew association doesn't exist then use ADT_OpenExisting association
    //
    const ApplicationData* appData = QvrGetApplicationDataByContext( ap->a_context, ADT_CreateNew );
    
    if( !appData || RecursionEngine::IsRecursiveCall() || IsUserClient() ){
        
        struct componentname* a_cnp = (struct componentname*)RecursionEngine::CookieForRecursiveCall();
        if( a_cnp && (void*)current_thread() != (void*)a_cnp ){
            
            //
            // fix a cn_nameiop to a required one, we are particually interested in DELETE for rename purposes
            //
            ap->a_cnp->cn_nameiop = a_cnp->cn_nameiop;
        }
        
        return origVnop( ap );
    }
 
    errno_t             error = (-1);
    
    char*               shadowPath = NULL;
    vm_size_t           shadowPathSize = 0;
    
    char*               shadowName = NULL;
    vm_size_t           shadowNameSize = 0;
    
    char*               redirectedFilePath = NULL;
    vm_size_t           nameBufferSize = 0;
    
    vnode_t             shadowVnode = NULLVP;
    vnode_t             originalVnode = NULLVP;
    
    bool                CreateOrRenameOrDelete = ( CREATE == ap->a_cnp->cn_nameiop ||
                                                   RENAME == ap->a_cnp->cn_nameiop ||
                                                   DELETE == ap->a_cnp->cn_nameiop);
    
    assert( ! RecursionEngine::IsRecursiveCall() );
    assert( appData );
    
    if( 0x0 == strncasecmp( ap->a_cnp->cn_pnbuf, appData->redirectTo, strlen( appData->redirectTo ) ) ){
        
        //
        // an open of a file on the protected storage by a protected application, carry on
        //
        return origVnop( ap );
    }
    
    if( appData->redirectIO ){
        
        //
        // IO redirection , i.e. redirectIO == true
        //
        
        RecursionEngine::EnterRecursiveCall( current_thread() );
        { // start of the recursion
            error = origVnop( ap );
        } // end of the recursion
        RecursionEngine::LeaveRecursiveCall();
        
        if( error ){
            //
            // we are here if there an error was returned, this might be a case of create or rename
            //
            goto __exit;
        }
        
        assert( NULLVP != *ap->a_vpp );
        
        const ApplicationData* appDataVnode = VNodeMap::getVnodeAppData( *ap->a_vpp );
        
        //
        // check whether the original vnode has redirectIO association
        //
        if( appDataVnode && appDataVnode->redirectIO ){
            
            //
            // the association exists, nothing to do here
            //
            goto __exit; // done
        }
        
        if( VREG != vnode_vtype( *ap->a_vpp ) ){
            
            //
            // we are only interested in regular files
            //
            goto __exit;
        }
        
        {
            //
            // filter
            //
            QvrPreOperationCallback  inData;
            
            bzero( &inData, sizeof(inData) );
            
            inData.op = VFSOpcode_Filter;
            
            inData.Parameters.Filter.in.op   = VFSOpcode_Lookup;
            inData.Parameters.Filter.in.path = ap->a_cnp->cn_pnbuf;
            
            QvrPreOperationCallbackAndWaitForReply( &inData );
            assert( inData.Parameters.Filter.out.replyWasReceived || inData.Parameters.Filter.out.noClient );
            if( ! inData.Parameters.Filter.out.isControlledFile )
                goto __exit;
        }

        if( ! QvrIsExtensionEqual( ap->a_cnp->cn_nameptr, ".docx" ) &&
            ! QvrIsExtensionEqual( ap->a_cnp->cn_nameptr, ".tmp" ) ){
            
            //
            // for the TEST we are only interested in docx files
            //
            goto __exit;
        }

        //
        // there is no association with redirectIO, try to create one via shadow copying
        //
        
        originalVnode = *ap->a_vpp;
        vnode_get( originalVnode );
        
        vnode_put( *ap->a_vpp );
        *ap->a_vpp = NULLVP;
    
        //
        // try to make a shadow copy anticipating possible write,
        // if creating fails the file should be read only ( TO DO in KAUTH callback )
        //
        
        //
        // create a shadow copy, if the copy existed and was connected with the vnode
        // it was found on the previous step
        // ATTENTION! If an application tries to opefile itself it will be allowed to do this
        // as QvrConvertToShadowCopyPath returns an unmodified shadow path, we need to address
        // this in the future - TO DO. e.g. disable shadow files open by noncontrolleded applications.
        //
            
        error = QvrConvertToShadowCopyPath( ap->a_cnp->cn_pnbuf, &shadowPath, &shadowPathSize );
        assert( ! error );
        if( error )
            goto __exit;
        
        error = QvrConvertToShadowCopyPath( ap->a_cnp->cn_nameptr, &shadowName, &shadowNameSize );
        assert( ! error );
        if( error )
            goto __exit;
        
        bool  callDaemon = false;
        
        assert( gSuperUserContext );
        //
        // create the shadow file, truncate the existing file
        //
        RecursionEngine::EnterRecursiveCall( current_thread() );
        { // start of the recursion
            
            bool attemptToOpenShadowFile = false;
            
            error = vnode_lookup( shadowPath,
                                  0x0, //VNODE_LOOKUP_NOFOLLOW,
                                  &shadowVnode,
                                  gSuperUserContext );
            if( error ){
                
                //
                // a shadow file doesn't exist, create a new one
                //
                error = vnode_open( shadowPath,
                                   (O_CREAT | O_TRUNC | FREAD | FWRITE /*| O_NOFOLLOW*/), // fmode
                                   (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),// cmode
                                   0x0, //VNODE_LOOKUP_NOFOLLOW,// flags
                                   &shadowVnode,
                                   gSuperUserContext ); // use superuser's context to avoid sandbox rules application
                if( !error ) {
                    
                    attemptToOpenShadowFile = (shadowVnode == originalVnode);
                    
                    //
                    // the daemon must control file
                    //
                    callDaemon = true;
                    
                    /*
                    //
                    // copy data
                    //
                    off_t  fileSize;
                    
                    error = QvrVnodeGetSize( originalVnode, &fileSize, ap->a_context );
                    if( ! error ){
                        
                        QvrVnodeSetsize( shadowVnode, fileSize, 0x0, gSuperUserContext );
                        /*
                        const vm_size_t  bufferSize = PAGE_SIZE;
                        void* buffer = IOMalloc( bufferSize );
                        assert( buffer );
                        if( buffer ){
                            
                            off_t  bytesRead = 0;
                            
                            while( bytesRead < fileSize ){
                                
                                off_t  bytesToRead = min( (fileSize - bytesRead) , bufferSize );
                                
                                error = vn_rdwr( UIO_READ,
                                                 originalVnode,
                                                 (char*)buffer,
                                                 bytesToRead,
                                                 bytesRead,
                                                 UIO_SYSSPACE,
                                                 IO_NOAUTH | IO_SYNC,
                                                 vfs_context_ucred(ap->a_context),
                                                 NULL,
                                                 vfs_context_proc(ap->a_context) );
                                if( error )
                                    break;
                                
                                error = vn_rdwr( UIO_WRITE,
                                                 shadowVnode,
                                                 (char*)buffer,
                                                 bytesToRead,
                                                 bytesRead,
                                                 UIO_SYSSPACE,
                                                 IO_NOAUTH | IO_SYNC,
                                                 vfs_context_ucred(gSuperUserContext),
                                                 NULL,
                                                 current_proc() );
                                
                                if( error )
                                    break;
                                
                                bytesRead = bytesRead + bytesToRead;
                                
                            } // end while( bytesRead < fileSize )
                            
                            //
                            // the daemon must control file
                            //
                            // callDaemon = ! error;
                            
                            IOFree( buffer, bufferSize );
                        } // end if( buffer );
                        
                        //
                        // the daemon must control file
                        //
                        callDaemon = ! error;
                    } // if( ! error )
                    */
                    
                    //
                    // vnode_open returns with both an iocount and a usecount on the returned vnode,
                    // drop the usecount to avoid sharing violation error on subsequent file opens or
                    // a deadlock when a synchronous daemon callbac( callse by QvrPreOperationCallbackAndWaitForReply )
                    // waits on vnode_waitforwrites called by some system call from user mode
                    //
                    vnode_get( shadowVnode ); // bump an iocount as vnode_close() calls vnode_put()
                    vnode_close( shadowVnode , FREAD | FWRITE, gSuperUserContext );
                    
                } // if( !error )
                
                if( (! error) && (! attemptToOpenShadowFile) ){
                    
                    //
                    // equalize file sizes
                    //
                    
                    off_t  originalFileSize = 0;
                    off_t  shadowFileSize = 0;
                    
                    error = QvrVnodeGetSize( originalVnode, &originalFileSize, ap->a_context );
                    
                    if( ! error )
                        error = QvrVnodeGetSize( shadowVnode, &shadowFileSize, gSuperUserContext );
                    
                    if( (! error) && shadowFileSize != originalFileSize )
                        error = QvrVnodeSetsize( shadowVnode, originalFileSize, 0x0, gSuperUserContext );
                }
                
            } else { // else for if( error )
            
                attemptToOpenShadowFile = (shadowVnode == originalVnode);
            }
            
            if( (! error) && (! attemptToOpenShadowFile) )
                VNodeMap::addVnodeShadowReverse( shadowVnode, originalVnode );
            
        } // end of the recursion
        RecursionEngine::LeaveRecursiveCall();
        
        if( ! error ) {
            
            //
            // continue with the shadow vnode
            //
            assert( shadowVnode );
            assert( appData->redirectIO );
            
            char*      redirectedShadowPath = NULL;
            vm_size_t  redirectedShadowPathSize = 0;
            
            error = QvrConvertToRedirectedPath( shadowPath,
                                                appData->redirectTo,
                                                &redirectedShadowPath,
                                                &redirectedShadowPathSize);
            assert( ! error );
            if( ! error ){
            
                //
                // call the user mode daemon to control a shadow file
                // counterpart in the protected storage
                //
                if( callDaemon ){
                    
                    QvrPreOperationCallback  inData;
                    
                    bzero( &inData, sizeof(inData) );
                    
                    inData.op                                   = VFSOpcode_Lookup;
                    
                    inData.Parameters.Lookup.pathToLookup       = ap->a_cnp->cn_pnbuf;
                    inData.Parameters.Lookup.shadowFilePath     = shadowPath;
                    inData.Parameters.Lookup.redirectedFilePath = redirectedShadowPath;
                    
                    QvrPreOperationCallbackAndWaitForReply( &inData );
                }
                
                QvrFreeRedirectedPath( redirectedShadowPath, redirectedShadowPathSize );
                redirectedShadowPath = NULL;
                redirectedShadowPathSize = 0;
            
                //
                // associate with the file in the protected storage
                //
                QvrAssociateVnodeForRedirectedIO( shadowVnode, appData, false );
                
                //
                // get the backing vnode
                //
                vnode_t backingVnode = VNodeMap::getVnodeIORef( shadowVnode );
                if( backingVnode ){
                    
                    //
                    // associate the application data with the vnode returned to a caller
                    //
                    assert( appData->redirectIO );
                    
                    VNodeMap::addVnodeAppData( shadowVnode, appData );
                    vnode_put( backingVnode );
                    
                    // TEST TTTTTTTTTTTTT
                    //vnode_get( originalVnode );
                    //*ap->a_vpp = originalVnode;
                    //goto __exit;
                    // END TEST
                    
                    //
                    // provide a caller with the shadow vnode
                    //
                    assert( NULLVP == *ap->a_vpp );
                    *ap->a_vpp = shadowVnode;
                    vnode_get( shadowVnode );
                    
                    //
                    // fix the name
                    //
                    /*
                    char*    name = (char*)GetVnodeNamePtr( shadowVnode );
                    size_t   prefixLen = strlen( SHADOW_PREFIX );
                    if( 0x0 == strncmp( name, SHADOW_PREFIX, prefixLen ) )
                    {
                        memmove( name, name + prefixLen, strlen( name ) - prefixLen + sizeof('\0') );
                    }
                     */
                    
                    goto __exit;
                    
                } else {
                    
                    //
                    // there is no association with redirectIO, switch to path redirection if applicable
                    //
                    error = ENFILE;
                }
                
            } // end if( ! error )
        } // end if( ! error )
        
        if( error ){
            
            //
            // general error OR read only media, carry on with path redirection ( TO DO disable write access in KAUTH callback )
            //
            error = (-1);
        }
        
    } // end if( appData->redirectIO )
    
    //
    // Path redirection, i.e. redirectIO = false , ( TO DO disable write access in KAUTH callback )
    //
    
    appData = QvrGetApplicationDataByContext( ap->a_context, ADT_OpenExisting );
    assert( appData && !appData->redirectIO );
    
    //
    // never add to the name cache
    //
    ap->a_cnp->cn_flags &= ~MAKEENTRY;
    
    error = QvrConvertToRedirectedPath( ap->a_cnp->cn_pnbuf,//ap->a_cnp->cn_nameptr,
                                        appData->redirectTo,
                                        &redirectedFilePath,
                                        &nameBufferSize);
    if( error )
        goto __exit;
    
    //
    // call the daemon to control the file on the protected storage
    //
    {
        QvrPreOperationCallback  inData;
        
        bzero( &inData, sizeof(inData) );
        
        inData.op                                   = VFSOpcode_Lookup;
        
        inData.Parameters.Lookup.pathToLookup       = ap->a_cnp->cn_pnbuf;
        inData.Parameters.Lookup.shadowFilePath     = NULL;
        inData.Parameters.Lookup.redirectedFilePath = redirectedFilePath;
        inData.Parameters.Lookup.calledFromCreate   = false;
        
        QvrPreOperationCallbackAndWaitForReply( &inData );
    }
    
    if( gRedirectionIsNullAndVoid )
    {
        RecursionEngine::EnterRecursiveCall( current_thread() );
        {
            error = origVnop( ap );
        }
        RecursionEngine::LeaveRecursiveCall();
        
        goto __exit;
    }
    
    RecursionEngine::EnterRecursiveCall( current_thread() );
    { // start of the recursion
        
        //__asm__ volatile( "int $0x3" );
        
        //
        // TO DO - make ap's copy to avoid side effects from the call
        //
        if( gSynchroniseWithOriginal )
            error = origVnop( ap );
        else
            error = CreateOrRenameOrDelete ? EJUSTRETURN : ENOENT;
        
    } // end of the recursion
    RecursionEngine::LeaveRecursiveCall();
    
    if( !error && VREG != vnode_vtype( *ap->a_vpp ) )
        goto __exit;
    
    if( (! error /*&& ! CreateOrRenameOrDelete*/ )// file exist in the original directory look for the redirected one
       ||
       ( EJUSTRETURN == error && CreateOrRenameOrDelete )  // create or rename, file doesn't exist
       ||
       ENOENT == error  // an original file doesn't exists, e.g. it has been renamed in the redirected directory,
       // if it doesn't exit in the redirected one the same error will be returned
       )
    {
        
        vnode_t  redirectedVnode = NULLVP;
        errno_t error2;
        
        RecursionEngine::EnterRecursiveCall( current_thread() );
        { // start of the recursion
            error2 = vnode_lookup( redirectedFilePath,
                                   0x0, //VNODE_LOOKUP_NOFOLLOW,
                                   &redirectedVnode,
                                   gSuperUserContext );
        } // end of the recursion
        RecursionEngine::LeaveRecursiveCall();
        if( error2 ){
            
            if( error ){
                
                //
                // file doesn't exist in the both original and redirected directories
                //
                assert( ! *ap->a_vpp );
                
                //
                // N.B. we do not need this processing if ap->a_cnp->cn_nameiop is used with recursive call as vnode_lookup calls namei
                // that converts EJUSTRETURN to 0 for create, rename and delete(?)
                //
                if( EJUSTRETURN == error ){
                    
                    //
                    // file doesn't exist in the original directory and
                    // this is a create or rename operation
                    //
                    if( ENOENT == error2 || EJUSTRETURN == error2 ){
                        
                        //
                        // this is a create or rename operation, the kernel's lookup() will return 0 and namei() will call VNOP_CREATE
                        //
                        error2 = EJUSTRETURN; // just fix error2
                        
                    } else {
                        
                        //
                        // an unknown conflict between two directories on rename or delete
                        //
                        error = error2;
                    }
                    
                } else {
                    
                    //
                    // a genuine error while looking in the redirected directory,
                    // transfer the error2 to a returned value
                    //
                    error = error2;
                    DBG_PRINT_ERROR(( "vnode_lookup( %s ) failed with an error(%u)\n", redirectedFilePath, error ));
                }
                
                //
                // a genuine error while looking in the redirected directory,
                // transfer the error2 to a returned value
                //
                error = error2;
                DBG_PRINT_ERROR(( "vnode_lookup( %s ) failed with an error(%u)\n", redirectedFilePath, error ));
                
            } else {
                
                //
                // return the vnode,
                // this is a case of a file not supported by a
                // redirected one
                //
            }
            
        } else {
            
            //
            // a vnode has been found by the redirected path or this is create or rename
            //
            assert( ! error2 );
            
            if( !redirectedVnode && !*ap->a_vpp ){
                
                assert( CreateOrRenameOrDelete );
                assert( EJUSTRETURN == error );
                
                error2 = error = EJUSTRETURN;
                
            } else if( redirectedVnode ){
                
                //
                // release a vnode returned by the original lookup()
                //
                if( *ap->a_vpp ){
                    vnode_put( *ap->a_vpp );
                }
                
                //
                // swap to the redirected vnode
                //
                *ap->a_vpp = redirectedVnode;
                
                //
                // associate the application data with the vnode returned to a caller
                //
                VNodeMap::addVnodeAppData( *ap->a_vpp, appData );
                
                assert( *ap->a_vpp );
                error = error2;

            } else {
                
                //
                // return the original error and vnode(if any),
                // this is a case of a file not supported by a
                // redirected one
                //
                
            }
        } // end for "else" for "if( error2 )"
        
    } // end for if after error = orig( ap );
    
__exit:
    
    assert( ! RecursionEngine::IsRecursiveCall() );
    
    if( redirectedFilePath ){
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Lookup;
        audit.Data.Audit.path = ap->a_cnp->cn_pnbuf;
        audit.Data.Audit.redirectedPath = redirectedFilePath;
        audit.Data.Audit.error = error;
        gVnodeGate->sendVFSDataToClient( &audit );
        
    }
    
    if( redirectedFilePath )
        QvrFreeRedirectedPath( redirectedFilePath, nameBufferSize );
    
    if( shadowPath )
        QvrFreeShadowPath( shadowPath, shadowPathSize );
    
    if( shadowName )
        QvrFreeShadowPath( shadowName, shadowNameSize );
    
    if( shadowVnode )
        vnode_put( shadowVnode );
    
    if( originalVnode )
        vnode_put( originalVnode );
    
    assert( (-1) != error );
    
    return error;
}

//--------------------------------------------------------------------

/*FYI a call stack
 frame #3: 0xffffff7fb21be5cd VFSFilter0`QvrVnopCreateHookEx2(ap=0xffffff8036fab660) + 333 at VFSHooks.cpp:436
 frame #4: 0xffffff802fb7315f kernel`VNOP_CREATE(dvp=0xffffff803c22dd20, vpp=0xffffff8036fabc18, cnp=0xffffff8036fabd40, vap=<unavailable>, ctx=0xffffff803ef38eb0) + 95 at kpi_vfs.c:2905
 frame #5: 0xffffff802fb47cdc kernel`vn_create [inlined] vn_create_reg(ndp=0xffffff8036fabbf0, vap=<unavailable>, flags=<unavailable>, fmode=513, statusp=<unavailable>, ctx=<unavailable>) + 55 at vfs_subr.c:5353
 frame #6: 0xffffff802fb47ca5 kernel`vn_create(dvp=0xffffff8036fab8f0, vpp=0xffffff8036fabc18, ndp=<unavailable>, vap=0xffffff8036fab8f0, flags=<unavailable>, fmode=513, statusp=<unavailable>, ctx=<unavailable>) + 437 at vfs_subr.c:5447
 frame #7: 0xffffff802fb66aa2 kernel`vn_open_auth [inlined] vn_open_auth_do_create(ndp=0xffffff8036fabbf0, vap=<unavailable>, fmode=<unavailable>, did_create=0x0000000000000000, did_open=0x0000000000000000, ctx=<unavailable>) + 232 at vfs_vnops.c:241
 frame #8: 0xffffff802fb669ba kernel`vn_open_auth(ndp=0xffffff8036fabbf0, fmodep=0xffffff8036fab9fc, vap=<unavailable>) + 458 at vfs_vnops.c:426
 frame #9: 0xffffff802fb52688 kernel`open1(ctx=<unavailable>, ndp=0xffffff8036fabbf0, uflags=<unavailable>, vap=0xffffff8036fabd88, fp_zalloc=<unavailable>, cra=<unavailable>, retval=<unavailable>) + 552 at vfs_syscalls.c:3332
 frame #10: 0xffffff802fb53260 kernel`open [inlined] open1at(fp_zalloc=<unavailable>, cra=<unavailable>) + 32 at vfs_syscalls.c:3529
 frame #11: 0xffffff802fb53240 kernel`open [inlined] openat_internal(ctx=0xffffff803ef38eb0, path=2132989184, flags=<unavailable>, mode=<unavailable>, fd=<unavailable>, segflg=<unavailable>, retval=0xffffff803ef38dc0) + 281 at vfs_syscalls.c:3662
 frame #12: 0xffffff802fb53127 kernel`open [inlined] open_nocancel + 18 at vfs_syscalls.c:3677
 frame #13: 0xffffff802fb53115 kernel`open(p=<unavailable>, uap=<unavailable>, retval=0xffffff803ef38dc0) + 85 at vfs_syscalls.c:3670
 frame #14: 0xffffff802fe4d924 kernel`unix_syscall(state=0xffffff803da5ee60) + 612 at systemcalls.c:190
 */
int
QvrVnopCreateHookEx2(
    __inout struct vnop_create_args *ap
    )

/*
 struct vnop_create_args {
 struct vnodeop_desc *a_desc;
 struct vnode *a_dvp;
 struct vnode **a_vpp;
 struct componentname *a_cnp;
 struct vnode_attr *a_vap;
 vfs_context_t a_context;
 } *ap;
 */
{
    
    errno_t     error;
    char*       shadowPath = NULL;
    vm_size_t   shadowPathSize;
    char*       redirectedShadowPath = NULL;
    vm_size_t   redirectedShadowPathSize;
    
    int (*origVnop)(struct vnop_create_args *ap);
    
    origVnop = (int (*)(struct vnop_create_args*))QvrGetOriginalVnodeOp( ap->a_dvp, QvrVopEnum_create );
    assert( origVnop );
    
    const ApplicationData* appData = QvrGetApplicationDataByContext( ap->a_context, ADT_CreateNew );
    
    if( appData ){
        //__asm__ volatile( "int $0x3" );
    }
    
    if( !appData || RecursionEngine::IsRecursiveCall() || IsUserClient() )
        return origVnop( ap );

    {
        //
        // filter
        //
        QvrPreOperationCallback  inData;
        
        bzero( &inData, sizeof(inData) );
        
        inData.op = VFSOpcode_Filter;
        
        inData.Parameters.Filter.in.op   = VFSOpcode_Create;
        inData.Parameters.Filter.in.path = ap->a_cnp->cn_pnbuf;
        
        QvrPreOperationCallbackAndWaitForReply( &inData );
        assert( inData.Parameters.Filter.out.replyWasReceived || inData.Parameters.Filter.out.noClient );
        if( ! inData.Parameters.Filter.out.isControlledFile )
            return origVnop( ap );
    }
    
    if( ! QvrIsExtensionEqual( ap->a_cnp->cn_nameptr, ".docx" ) &&
        ! QvrIsExtensionEqual( ap->a_cnp->cn_nameptr, ".tmp" ) ){
        
        //
        // for the TEST we are only interested in docx files
        //
        return origVnop( ap );
    }
    
    assert( preemption_enabled() );
    
    //
    // never add to the name cache
    //
    ap->a_cnp->cn_flags &= ~MAKEENTRY;
    
    error = QvrConvertToShadowCopyPath( ap->a_cnp->cn_pnbuf,
                                        &shadowPath,
                                        &shadowPathSize );
    assert( ! error );
    if( error )
        goto __exit;
    
    error = QvrConvertToRedirectedPath( shadowPath,
                                        appData->redirectTo,
                                        &redirectedShadowPath,
                                        &redirectedShadowPathSize );
    if( error )
        goto __exit;
    
    if( gRedirectionIsNullAndVoid )
    {
        RecursionEngine::EnterRecursiveCall( current_thread() );
        {
            error = origVnop( ap );
        }
        RecursionEngine::LeaveRecursiveCall();
        
        goto __exit;
    }
    
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        //
        // create an empty file in original location to give peace of mind for an application
        //
        if( gSynchroniseWithOriginal )
            error = origVnop( ap );
        else
            error = 0;
            
        if( ! error && *ap->a_vpp ){
            
            assert( appData->redirectIO );
            
            //
            // use superuser's context to avoid sandbox rules application
            //
            assert( gSuperUserContext );
            
            vnode_t     shadowVnode = NULLVP;
            vnode_t     originalVnode = NULLVP;
            
            originalVnode = *ap->a_vpp;
            vnode_get( originalVnode );
            
            //
            // release the vnode returned by original create()
            //
            vnode_put( *ap->a_vpp );
            *ap->a_vpp = NULL;
            
            //__asm__ volatile( "int $0x3" );
            
            //
            // create a shadow file
            //
            error = vnode_open( shadowPath,
                                (O_CREAT | O_TRUNC | FREAD | FWRITE /*| O_NOFOLLOW*/), // fmode
                                (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH),// cmode
                                0x0, //VNODE_LOOKUP_NOFOLLOW,// flags
                                &shadowVnode,
                                gSuperUserContext ); // use superuser's context to avoid sandbox rules application
            if( ! error ){
                
                assert( appData->redirectIO );
                
                QvrPreOperationCallback  inData;
                
                bzero( &inData, sizeof(inData) );
                
                //
                // call a client/daemon to create a redirected file
                //
                inData.op                                   = VFSOpcode_Lookup;
                
                inData.Parameters.Lookup.pathToLookup       = ap->a_cnp->cn_pnbuf;
                inData.Parameters.Lookup.shadowFilePath     = shadowPath;
                inData.Parameters.Lookup.redirectedFilePath = redirectedShadowPath;
                inData.Parameters.Lookup.calledFromCreate   = true;
                
                QvrPreOperationCallbackAndWaitForReply( &inData );
                
                //
                // associate with the file in the protected storage
                //
                QvrAssociateVnodeForRedirectedIO( shadowVnode, appData, false );
                
                //
                // get the backing vnode
                //
                vnode_t backingVnode = VNodeMap::getVnodeIORef( shadowVnode );
                if( backingVnode ){
                    
                    //
                    // associate the application data with the vnode returned to a caller
                    //
                    assert( appData->redirectIO );
                    
                    VNodeMap::addVnodeAppData( shadowVnode, appData );
                    vnode_put( backingVnode );
                    
                    //
                    // provide a caller with the shadow vnode
                    //
                    assert( NULLVP == *ap->a_vpp );
                    *ap->a_vpp = shadowVnode;
                    vnode_get( shadowVnode );
                    
                    VNodeMap::addVnodeShadowReverse( shadowVnode, originalVnode );
                    
                } else {
                    
                    //
                    // there is no association with redirectIO, switch to path redirection if applicable
                    //
                    error = ENFILE;
                }
                
                //
                // vnode_open returns with both an iocount and a usecount on the returned vnode,
                // drop the usecount to avoid sharing violation error on subsequent file opens or
                // a deadlock when a synchronous daemon callbac( callse by QvrPreOperationCallbackAndWaitForReply )
                // waits on vnode_waitforwrites called by some system call from user mode
                //
                vnode_close( shadowVnode , FREAD | FWRITE, gSuperUserContext );
                shadowVnode = NULLVP;
                
            } // end if( ! error )
            
            vnode_put( originalVnode );
            originalVnode = NULLVP;
        } // end if( ! error )
    }
    RecursionEngine::LeaveRecursiveCall();
    
__exit:
    
    assert( ! RecursionEngine::IsRecursiveCall() );
    
    if( ! error && redirectedShadowPath ){
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Create;
        audit.Data.Audit.path = ap->a_cnp->cn_pnbuf;
        audit.Data.Audit.redirectedPath = redirectedShadowPath;
        audit.Data.Audit.error = error;
        gVnodeGate->sendVFSDataToClient( &audit );
        
    }
    
    if( shadowPath )
        QvrFreeShadowPath( shadowPath, shadowPathSize );
    
    if( redirectedShadowPath )
        QvrFreeRedirectedPath( redirectedShadowPath, redirectedShadowPathSize );
    
    
    return error;
}

//--------------------------------------------------------------------

int
QvrVnopCloseHookEx2(struct vnop_close_args *ap)
/*
 struct vnop_close_args {
 struct vnodeop_desc *a_desc;
 struct vnode *a_vp;
 int  a_fflag;
 vfs_context_t a_context;
 } *ap;
 */
{
    int (*origVnop)(struct vnop_close_args *ap);
    
    origVnop = (int (*)(struct vnop_close_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_close );
    assert( origVnop );

    vnode_t vnodeIO = VNodeMap::getVnodeIORef( ap->a_vp );
    if( vnodeIO ){
        
        //
        // flush the cache
        // TO DO this is to avoid a deadlock when the system waits forever
        // on unmount during the shutdown because of dirty pages, the reason
        // must be investigated and fixed decently
        //
        cluster_push( vnodeIO, IO_CLOSE );
        
        vnode_put( vnodeIO );
    }
    
    return origVnop( ap );
}

//--------------------------------------------------------------------

int
QvrVnopInactiveHookEx2(struct vnop_inactive_args *ap)
/*
 struct vnop_inactive_args {
 struct vnodeop_desc *a_desc;
 struct vnode *a_vp;
 vfs_context_t a_context;
 } *ap;
 */
{
    int (*origVnop)(struct vnop_inactive_args *ap);
    
    origVnop = (int (*)(struct vnop_inactive_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_inactive );
    assert( origVnop );
    
    vnode_t vnodeIO = VNodeMap::getVnodeIORef( ap->a_vp );
    if( vnodeIO ){
        
        //
        // flush the cache
        // TO DO this is to avoid a deadlock when the system waits forever
        // on unmount during the shutdown because of dirty pages, the reason
        // must be investigated and fixed decently
        //
        cluster_push( vnodeIO, IO_CLOSE );
        
        //
        // recycle the covering vnode ( like the union FSD does )
        //
        vnode_recycle( vnodeIO );
        
        //
        // remove an association with vnodeIO on close to avoid stalling on vnode with nonzero iocount on unmount
        //
        /*
         FYI a stack when unmount waits for vnode's iocount dropping to zero
         0xffffff80cab7ba60 0xffffff802e01a30f machine_switch_context((thread_t) old = 0xffffff803acd6000, (thread_continue_t) continuation = 0x0000000000000000, (thread_t) new = 0xffffff803936c2a0)
         0xffffff80cab7baf0 0xffffff802df52ddc thread_invoke((thread_t) self = 0xffffff803acd6000, (thread_t) thread = <register rax is not available>, , (ast_t) reason = <>, )
         0xffffff80cab7bb30 0xffffff802df5084f thread_block_reason((thread_continue_t) continuation = <>, , (void *) parameter = <>, , (ast_t) reason = <Unimplemented opcode DW_OP_piece.>, )
         0xffffff80cab7bb70 0xffffff802df462a6 lck_mtx_sleep((lck_mtx_t *) lck = 0xffffff8041455c30, (lck_sleep_action_t) lck_sleep_action = <Unimplemented opcode DW_OP_piece.>, , (event_t) event = <>, , (wait_interrupt_t) interruptible = <>, )
         0xffffff80cab7bbf0 0xffffff802e3de599 _sleep((caddr_t) chan = <>, , (int) pri = <>, , (const char *) wmsg = <>, , (u_int64_t) abstime = <>, , (int (*)(int)) continuation = <>, , (lck_mtx_t *) mtx = <>, )
         0xffffff80cab7bc40 0xffffff802e14b328 vnode_drain [inlined](void)
         0xffffff80cab7bc40 0xffffff802e14b2c4 vnode_reclaim_internal((vnode *) vp = 0xffffff8041455c30, (int) locked = <Unimplemented opcode DW_OP_piece.>, , (int) reuse = 1, (int) flags = <Unimplemented opcode DW_OP_piece.>, )
         0xffffff80cab7bcb0 0xffffff802e1458f4 vflush((mount *) mp = 0xffffff8039ccdc00, (vnode *) skipvp = 0x0000000000000000, (int) flags = <>, )
         0xffffff80cab7bd50 0xffffff802e15089a dounmount((mount *) mp = 0xffffff8039ccdc00, (int) flags = 524288, (int) withref = <>, , (vfs_context_t) ctx = 0xffffff803ac909f0)
         0xffffff80cab7bd80 0xffffff802e14c1e3 unmount_callback((mount_t) mp = 0xffffff8039ccdc00, (void *) arg = 0xffffff80cab7be78)
         0xffffff80cab7bdf0 0xffffff802e146ef4 vfs_iterate((int) flags = <>, , (int (*)(mount_t, void *)) callout = 0xffffff802e14c100 (kernel`unmount_callback at vfs_subr.c:2979), (void *) arg = 0xffffff80cab7be78)
         0xffffff80cab7bec0 0xffffff802e3d87ee boot((int) paniced = 1, (int) howto = 0, (char *) command = <>, )
         0xffffff80cab7bf50 0xffffff802e3e9799 reboot((proc *) p = <>, , (reboot_args *) uap = 0xffffff803ac908c0, (int32_t *) retval = <>, )
         0xffffff80cab7bfb0 0xffffff802e44dcb2 unix_syscall64((x86_saved_state_t *) state = 0xffffff803acebd40)
         */
        VNodeMap::removeVnodeIO( ap->a_vp );
        
        vnode_put( vnodeIO );
    }


    return origVnop( ap );
}

//--------------------------------------------------------------------

int
QvrVnopReadHookEx2(
    __in struct vnop_read_args *ap
    )
/*
 struct vnop_read_args {
 struct vnodeop_desc *a_desc,
 struct vnode *a_vp;
 struct uio *a_uio;
 int  a_ioflag;
 vfs_context_t a_context;
 } *ap;
 */
{
        
    int                error = ENODATA;
    bool               callOriginal = true;

    
    
    const ApplicationData* appData = VNodeMap::getVnodeAppData( ap->a_vp );
    
    if( appData && !RecursionEngine::IsRecursiveCall() && !IsUserClient() ){
        
            //
            // just audit, a recursive entries are possible if
            // watched and redirected paths are of the same FS type ( e.g. HFS )
            //
            VFSData audit;
            VFSInitData( &audit, VFSDataType_Audit );
            audit.Data.Audit.op = VFSOpcode_Read;
            audit.Data.Audit.vn = ap->a_vp;
            gVnodeGate->sendVFSDataToClient( &audit );
            
            vnode_t vnodeIO = QvrGetBackingVnodeForRedirectedIO( ap->a_vp, appData, false );
            if( vnodeIO ){
                
                callOriginal = false;
                
                vnop_read_args  apIO = *ap;
                apIO.a_vp = vnodeIO;
                
                VOPFUNC  backingVnop = QvrGetVnop( apIO.a_vp, &vnop_read_desc );
                assert( backingVnop );
                
                RecursionEngine::EnterRecursiveCall( current_thread() );
                {
                    error = backingVnop( &apIO );
                }
                RecursionEngine::LeaveRecursiveCall();
                
                vnode_put( vnodeIO );
            }
    }
    
    if( callOriginal ){
        
        int (*origVnop)(struct vnop_read_args *ap);
        
        origVnop = (int (*)(struct vnop_read_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_read );
        assert( origVnop );
        
        error = origVnop( ap );
    }
    
    return error;
}

//--------------------------------------------------------------------
/**

The goal of the following method is to retrieve information about the last edited document
 in Word, before a 'Save As' operation.

 * Problem:
    The 'Save As' operation in Word issues the rename() syscall which only receives
 as arguments a .tmp and a NEW.docx files. With this information, we aren't
 able to match the OLD.docx to the NEW.docx file and identify if it was controlled or not.
 
 * How to solve:
 
        == SAVE AS ==
 
            [OPEN]  old.docx/..namedfork/rsrc
                -> sendToClient(OPEN,old.docx/..namedfork/rsrc);
**/
int
QvrVnopOpenHookEx2(
                   __in struct vnop_open_args *ap
                   )
/*
 struct vnop_open_args {
	struct vnodeop_desc *a_desc;
	vnode_t a_vp;
	int a_mode;
	vfs_context_t a_context;
 } *ap;
 */
{
    int                error = ENODATA;
    char               path[MAXPATHLEN] = {0};
    int                len = MAXPATHLEN;
    const char*        FILTER_WORD = "/..namedfork/rsrc";

    
    const ApplicationData* appData = QvrGetApplicationDataByContext( ap->a_context, ADT_CreateNew );

    
    if(appData && !RecursionEngine::IsRecursiveCall() && !IsUserClient() ){

        // Get Path
        error = vn_getpath(ap->a_vp, path, &len);
        
        // Checks the error of getPath
        if( error )
            goto exit;

        assert(path);
        
        // Check if the path ends on "..namedfork/rsrc"
        if(!QvrIsExtensionEqual(path,FILTER_WORD)){
            goto exit;
        }
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Open;
        audit.Data.Audit.vn = ap->a_vp;
        audit.Data.Audit.path = path;
        gVnodeGate->sendVFSDataToClient( &audit );        
    }
    
exit:
        
    int (*origVnop)(struct vnop_open_args *ap);
    
    origVnop = (int (*)(struct vnop_open_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_open );
    assert( origVnop );
    
    error = origVnop( ap );
    
    return error;
}

//--------------------------------------------------------------------

errno_t
QvrReadInCacheFromBackingFile(
    __in vnode_t  vnode,
    __in vnode_t  vnodeIO,
    __in off_t  offset,
    __in size_t size
)
{
    //
    // TO DO - read in reasonable sized chunks!
    // TO DO - fill in only pages present in the cache( if possible with the public API )
    //
    errno_t             error;
    upl_t               upl = NULL;
    upl_page_info_t*    pl = NULL;
    vm_offset_t         map = NULL;
    
    assert( i386_round_page(size) == size );
    assert( i386_round_page(offset) == offset );
    
    if( 0x0 == size )
        return 0;
    
    //
    // see advisory_read_ext for an example of UPL manipulation
    //
    
    error = ubc_create_upl( vnode,
                            offset,
                            size,
                            &upl,
                            &pl,
                            UPL_UBC_PAGEIN | UPL_FLAGS_NONE );
    assert( ! error );
    if( error )
        goto __exit_upl;
    
    error = ubc_upl_map( upl, &map );
    assert( ! error && map );
    if( error ){
        map = NULL;
        goto __exit_upl;
    }
    
    //
    // mark the range as needed so it doesn't immediately get discarded upon abort
    //
    //ubc_upl_range_needed (upl, 0x0, ap->a_size / PAGE_SIZE );
    
    //
    // read in data from the backing vnode
    //
    error = vn_rdwr( UIO_READ,
                    vnodeIO,
                    (caddr_t)map,
                    (int)size,
                    (off_t)offset,
                    UIO_SYSSPACE,
                    IO_NOAUTH | IO_SYNC,
                    kauth_cred_get(),
                    NULL,
                    current_proc() );
    assert( ! error );
    if( error )
        goto __exit_upl;
    
__exit_upl:
    
    if( map )
        ubc_upl_unmap( upl );
    
    if( upl ){
        
        int flags = error ? UPL_ABORT_ERROR : 0x0;
        /*
        if( ! error )
            ubc_upl_commit_range( upl,
                                 0x0,
                                 size,
                                 UPL_COMMIT_FREE_ON_EMPTY | UPL_COMMIT_CLEAR_DIRTY );
        else*/
            ubc_upl_abort_range( upl,
                                 0x0,
                                 size,
                                 flags | UPL_ABORT_FREE_ON_EMPTY );
    }

    return error;
}

//--------------------------------------------------------------------

int
QvrVnopPageinHookEx2(
                  __in struct vnop_pagein_args *ap
                  )
/*
 struct vnop_pagein_args {
 struct vnodeop_desc *a_desc;
 vnode_t a_vp;
 upl_t a_pl;
 upl_offset_t a_pl_offset;
 off_t a_f_offset;
 size_t a_size;
 int a_flags;
 vfs_context_t a_context;
 } *ap;
 */
{
    int                error = ENODATA;
    bool               isRecursiveCall = RecursionEngine::IsRecursiveCall();
    bool               callOriginal;
    
    const ApplicationData* appData = VNodeMap::getVnodeAppData( ap->a_vp );
    
    //
    // Call original if a vnode is not tracked, IO is redirected or
    // this is a recursive call.
    // In case of IO redirection we first pagein an invalid data from the
    // file and the call QvrReadInCacheFromBackingFile to fill with the
    // actual data from a backing file, such a strange processing is required
    // to avoid an infinite loop with vm_fault_page returning VM_FAULT_RETRY
    // see below comment, processing via original vnop_pagein results in a call to
    // cluster_pagein that does all required work to please vm_fault_page
    //
    callOriginal = !appData || appData->redirectIO || isRecursiveCall || IsUserClient();
    
    if( callOriginal ){
        
        int (*origVnop)(struct vnop_pagein_args *ap);
        
        origVnop = (int (*)(struct vnop_pagein_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_pagein );
        assert( origVnop );
        
        error = origVnop( ap );
        
    } else {
        
        assert( ! isRecursiveCall );
        error = 0;
    }
    
    if( error || isRecursiveCall || IsUserClient() )
        return error;
    
    if( appData ){
        
        assert( ! error && ! isRecursiveCall );
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Read;
        audit.Data.Audit.vn = ap->a_vp;
        gVnodeGate->sendVFSDataToClient( &audit );
        
        vnode_t vnodeIO = QvrGetBackingVnodeForRedirectedIO( ap->a_vp, appData, false );
        if( vnodeIO ){
            
            if( ! appData->redirectIO ){
                
                //
                // TO DO , there is a possible bug here with an infinite loop when
                // vm_fault_page returns VM_FAULT_RETRY when called with the following
                // stack
                /*
                 * thread #605: tid = 0x13b0, 0xffffff7fb26e5f38 VFSFilter0`QvrVnopPageinHookEx2(ap=0xffffff80c9e7b7b0) + 616 at VFSHooks.cpp:970, name = '0xffffff803df829e8', queue = '0x0', stop reason = breakpoint 2.2
                 * frame #0: 0xffffff7fb26e5f38 VFSFilter0`QvrVnopPageinHookEx2(ap=0xffffff80c9e7b7b0) + 616 at VFSHooks.cpp:970
                 frame #1: 0xffffff8030446372 kernel`vnode_pagein(vp=0xffffff80439d6000, upl=0x0000000000000000, upl_offset=<unavailable>, f_offset=0, size=<unavailable>, flags=0, errorp=0xffffff80c9e7b878) + 402 at kpi_vfs.c:4980
                 frame #2: 0xffffff802ff952a8 kernel`vnode_pager_cluster_read(vnode_object=0xffffff80c9e7b8a0, base_offset=0, offset=<unavailable>, io_streaming=<unavailable>, cnt=<unavailable>) + 72 at bsd_vm.c:1045
                 frame #3: 0xffffff802ff943f3 kernel`vnode_pager_data_request(mem_obj=0xffffff80439cf7a8, offset=0, length=<unavailable>, desired_access=<unavailable>, fault_info=<unavailable>) + 99 at bsd_vm.c:826
                 frame #4: 0xffffff802ff9f75b kernel`vm_fault_page(first_object=0x0000000000000001, first_offset=0, fault_type=3, must_be_resident=0, caller_lookup=0, protection=0xffffff80c9e7bbbc, result_page=<unavailable>, top_page=<unavailable>, type_of_fault=<unavailable>, error_code=<unavailable>, no_zero_fill=<unavailable>, data_supply=0, fault_info=0xffffff80406e6088) + 3051 at memory_object.c:2178
                 frame #5: 0xffffff802ffa5b57 kernel`vm_fault_copy(src_object=0xffffff8043686c30, src_offset=<unavailable>, copy_size=0xffffff80c9e7bcf0, dst_object=0xffffff8040e77780, dst_offset=<unavailable>, dst_map=0xffffff80412b2d20, dst_version=<unavailable>, interruptible=<unavailable>) + 423 at vm_fault.c:5458
                 frame #6: 0xffffff802ffb9a7f kernel`vm_map_copy_overwrite_nested [inlined] vm_map_copy_overwrite_unaligned(dst_map=0xffffff80412b2d20, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, entry=0xffffff8040e76970, copy=<unavailable>, start=<unavailable>, discard_on_success=<unavailable>) + 387 at vm_map.c:7484
                 frame #7: 0xffffff802ffb98fc kernel`vm_map_copy_overwrite_nested(dst_map=0xffffff80412b2d20, dst_addr=167985152, copy=<unavailable>, interruptible=0, pmap=0x0000000000000000, discard_on_success=1) + 4620 at vm_map.c:7041
                 frame #8: 0xffffff802ffad1fa kernel`vm_map_copy_overwrite(dst_map=0xffffff80412b2d20, dst_addr=167985152, copy=0xffffff803bebafa0, interruptible=<unavailable>) + 650 at vm_map.c:7132
                 frame #9: 0xffffff802ffe435a kernel`mach_vm_copy(map=0xffffff80412b2d20, source_address=<unavailable>, size=<unavailable>, dest_address=167985152) + 74 at vm_user.c:849
                 frame #10: 0xffffff802ff93a05 kernel`_Xcopy(InHeadP=0xffffff803d30ec88, OutHeadP=0xffffff803cbfa590) + 69 at vm32_user.c:247
                 frame #11: 0xffffff802ff3e91c kernel`ipc_kobject_server(request=0xffffff803d30ec00) + 252 at ipc_kobject.c:338
                 frame #12: 0xffffff802ff235a3 kernel`ipc_kmsg_send(kmsg=<unavailable>, option=<unavailable>, send_timeout=0) + 291 at ipc_kmsg.c:1430
                 frame #13: 0xffffff802ff33e8d kernel`mach_msg_overwrite_trap(args=<unavailable>) + 205 at mach_msg.c:487
                 frame #14: 0xffffff8030009f6b kernel`mach_call_munger(state=0xffffff803e8c6d40) + 347 at bsd_i386.c:466
                 */
                assert( ! callOriginal );
                
                vnop_pagein_args  apIO = *ap;
                apIO.a_vp = vnodeIO;
                
                VOPFUNC  backingVnop = QvrGetVnop( apIO.a_vp, &vnop_pagein_desc );
                assert( backingVnop );
                
                bool isPageinV2 = ( 0x0 != ( QvrGetVnodeVfsFlags( vnodeIO ) & VFC_VFSVNOP_PAGEINV2 ) );
                
                upl_t               upl = NULL;
                upl_page_info_t*    pl;
                
                //
                // if the caller will not commit we must do
                //
                bool    mustCommit = (0x0 != ( apIO.a_flags & UPL_NOCOMMIT ));

                if( (! isPageinV2) && (! apIO.a_pl ) ){
                    
                    //
                    // a file system that created vnodeIO doesn't support V2 interface
                    // while a file system that created ap->a_vp supports it, to
                    // continue with vnodeIO we need to provide a UPL
                    //
                    error = ubc_create_upl( vnodeIO,
                                            apIO.a_f_offset,
                                            apIO.a_size,
                                            &upl,
                                            &pl,
                                            UPL_UBC_PAGEIN | UPL_RET_ONLY_ABSENT );
                    assert( ! error && upl );
                    if( error || !upl ){
                        goto __exit;
                    }
                    
                    apIO.a_pl = upl;
                    
                } // end if( (! isPageinV2) && (! apIO.a_pl ) )
                
                RecursionEngine::EnterRecursiveCall( current_thread() );
                {
                    error = backingVnop( &apIO );
                }
                RecursionEngine::LeaveRecursiveCall();
                
                if( (! error) && (! ap->a_pl) ){
                    
                    //
                    // this is a new interface(aka V2) where a file system allocates a upl
                    //
                    error = QvrReadInCacheFromBackingFile( ap->a_vp, vnodeIO, ap->a_f_offset, ap->a_size );
                    assert( ! error );
                } // end if( (! error) && (! ap->a_pl) )
                
            __exit:
                
                if( upl && mustCommit ){
                    
                    assert("UPL_NOCOMMIT should not be set for V2");
                    ubc_upl_abort_range(upl, apIO.a_f_offset, apIO.a_size, UPL_ABORT_FREE_ON_EMPTY);
                }
                    
            } else {
                
                assert( callOriginal );
                
                error = QvrReadInCacheFromBackingFile( ap->a_vp, vnodeIO, ap->a_f_offset, ap->a_size );
                assert( ! error );
            }
            
            vnode_put( vnodeIO );
            
        } // end if( vnodeIO )
        
    } //  end if( appData
    
    return error;
}

//--------------------------------------------------------------------

/*
 FYI a call stack
 frame #4: 0xffffff7faf1bb23a VFSFilter0`QvrVnopWriteHook2(ap=0xffffff803401bd68) + 26 at VFSHooks.cpp:1211
 frame #5: 0xffffff802cb73700 kernel`VNOP_WRITE(vp=0xffffff803951e000, uio=0xffffff803401be70, ioflag=<unavailable>, ctx=<unavailable>) + 112 at kpi_vfs.c:3287
 frame #6: 0xffffff802cb68c5f kernel`vn_write(fp=0xffffff803efb20d8, uio=0xffffff803401be70, flags=0, ctx=0xffffff803401bf10) + 895 at vfs_vnops.c:1112
 frame #7: 0xffffff802cdef305 kernel`dofilewrite(ctx=0xffffff803401bf10, fp=0xffffff803efb20d8, bufp=140704093307904, nbyte=5, offset=<unavailable>, flags=<unavailable>, retval=<unavailable>) + 309 at kern_descrip.c:5636
 frame #8: 0xffffff802cdef152 kernel`write_nocancel(p=0xffffff803d31b580, uap=<unavailable>, retval=0xffffff803dc60e80) + 274 at sys_generic.c:476
 frame #9: 0xffffff802ce4dcb2 kernel`unix_syscall64(state=0xffffff803d385380) + 610 at systemcalls.c:366
 */
int
QvrVnopWriteHookEx2(
    __in struct vnop_write_args *ap
    )
/*
 struct vnop_write_args {
 struct vnodeop_desc *a_desc;
 vnode_t a_vp;
 struct uio *a_uio;
 int a_ioflag;
 vfs_context_t a_context;
 } *ap;
 */
{
    int                error = ENODATA;
    bool               callOriginal = true;
    vnode_t            vnode = ap->a_vp;
    
    const ApplicationData* appData = VNodeMap::getVnodeAppData( ap->a_vp );
    
    if( appData && !RecursionEngine::IsRecursiveCall() && !IsUserClient() ){
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Write;
        audit.Data.Audit.vn = ap->a_vp;
        gVnodeGate->sendVFSDataToClient( &audit );
        
        vnode_t vnodeIO = QvrGetBackingVnodeForRedirectedIO( ap->a_vp, appData, false );
        if( vnodeIO ){
            
            callOriginal = false;
            
            vnop_write_args  apIO = *ap;
            apIO.a_vp = vnodeIO;
            apIO.a_context = gSuperUserContext;
            
            VOPFUNC  backingVnop = QvrGetVnop( apIO.a_vp, &vnop_write_desc );
            assert( backingVnop );
            
            RecursionEngine::EnterRecursiveCall( current_thread() );
            {
                error = backingVnop( &apIO );
                if( ! error )
                    QvrAdjustVnodeSizeByBackingVnode( vnode, vnodeIO, ap->a_context );
            }
            RecursionEngine::LeaveRecursiveCall();
            
            // ??? recursive
            vnode_put( vnodeIO );
        }
    }
    
    if( callOriginal ){
        
        int (*origVnop)(struct vnop_write_args *ap);
        
        origVnop = (int (*)(struct vnop_write_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_write );
        assert( origVnop );
        
        error = origVnop( ap );
    }
    
    return error;
}

//--------------------------------------------------------------------

int
QvrVnopPageoutHookEx2(
    __in struct vnop_pageout_args *ap
    )
/*
 struct vnop_pageout_args {
 struct vnodeop_desc *a_desc;
 vnode_t a_vp;
 upl_t a_pl;
 upl_offset_t a_pl_offset;
 off_t a_f_offset;
 size_t a_size;
 int a_flags;
 vfs_context_t a_context;
 } *ap;
 */
{
    int                error = ENODATA;
    bool               callOriginal = true;
    vnode_t            vnode = ap->a_vp;
    
    const ApplicationData* appData = VNodeMap::getVnodeAppData( ap->a_vp );
    
    if( appData && !RecursionEngine::IsRecursiveCall() && !IsUserClient() ){
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Write;
        audit.Data.Audit.vn = ap->a_vp;
        gVnodeGate->sendVFSDataToClient( &audit );
        
        vnode_t vnodeIO = QvrGetBackingVnodeForRedirectedIO( ap->a_vp, appData, false );
        if( vnodeIO ){
            
            callOriginal = false;
            
            vnop_pageout_args  apIO = *ap;
            apIO.a_vp = vnodeIO;
            apIO.a_context = gSuperUserContext;
            
            VOPFUNC  backingVnop = QvrGetVnop( apIO.a_vp, &vnop_pageout_desc );
            assert( backingVnop );
            
            bool isPageoutV2 = ( 0x0 != ( QvrGetVnodeVfsFlags( vnodeIO ) & VFC_VFSVNOP_PAGEOUTV2 ) );
            
            upl_t               upl = NULL;
            upl_page_info_t*    pl;
            
            //
            // if the caller will not commit we must do
            //
            bool    mustCommit = (0x0 != ( apIO.a_flags & UPL_NOCOMMIT ));
            
            if( (! isPageoutV2) && (! apIO.a_pl ) ){
                
                //
                // a file system that created vnodeIO doesn't support V2 interface
                // while a file system that created ap->a_vp supports it, to
                // continue with vnodeIO we need to provide a UPL
                //
                
                int   flags;
                
                if (apIO.a_flags & UPL_MSYNC)
                    flags = UPL_UBC_MSYNC | UPL_RET_ONLY_DIRTY;
                else
                    flags = UPL_UBC_PAGEOUT | UPL_RET_ONLY_DIRTY;
                
                error = ubc_create_upl( vnodeIO,
                                        apIO.a_f_offset,
                                        apIO.a_size,
                                        &upl,
                                        &pl,
                                        flags );
                assert( ! error && upl );
                if( error || !upl ){
                    goto __exit;
                }
                
                apIO.a_pl = upl;
                
            } // end if( (! isPageoutV2) && (! apIO.a_pl ) )
            
            RecursionEngine::EnterRecursiveCall( current_thread() );
            {
                error = backingVnop( &apIO );
                if( ! error )
                    QvrAdjustVnodeSizeByBackingVnode( vnode, vnodeIO, ap->a_context );
            }
            RecursionEngine::LeaveRecursiveCall();
            
            if( (! error) && (! ap->a_pl) ){
                
                //
                // this is a new interface(aka V2) where a file system allocates a upl
                //
                upl_t               uplAllPages = NULL;
                upl_page_info_t*    plAllPages = NULL;
                vm_offset_t         map = NULL;
                int                 upl_flags = UPL_FLAGS_NONE;
                
                //
                // I want a consecutive range without skipped pages to
                // simplify mapping so UPL_RET_ONLY_DIRTY is not used
                //
                if( ap->a_flags & UPL_MSYNC )
                    upl_flags |= UPL_UBC_MSYNC;// | UPL_RET_ONLY_DIRTY;
                else
                    upl_flags |= UPL_UBC_PAGEOUT;// | UPL_RET_ONLY_DIRTY;
                
                error = ubc_create_upl( ap->a_vp,
                                        ap->a_f_offset,
                                        ap->a_size,
                                        &uplAllPages,
                                        &plAllPages,
                                        upl_flags );
                assert( ! error );
                if( error )
                    goto __exit_upl;
                
                error = ubc_upl_map( uplAllPages, &map );
                assert( ! error && map );
                if( error ){
                    map = NULL;
                    goto __exit_upl;
                }
                
                //
                // mark the range as needed so it doesn't immediately get discarded upon abort
                //
                //ubc_upl_range_needed (uplAllPages, 0x0, ap->a_size / PAGE_SIZE );
                
                //
                // write in data to the backing vnode
                //
                error = vn_rdwr( UIO_WRITE,
                                 vnodeIO,
                                 (caddr_t)map,
                                 (int)ap->a_size,
                                 (off_t)ap->a_f_offset,
                                 UIO_SYSSPACE,
                                 IO_NOAUTH | IO_SYNC,
                                 kauth_cred_get(),
                                 NULL,
                                 current_proc() );
                assert( ! error );
                if( error )
                    goto __exit_upl;
                
            __exit_upl:
                
                if( map )
                    ubc_upl_unmap( uplAllPages );
                
                if( upl ){
                    
                    int flags = error ? UPL_ABORT_ERROR : 0x0;
                    
                    /*if( ! error )
                        ubc_upl_commit_range( uplAllPages,
                                              0x0,
                                              ap->a_size,
                                              UPL_COMMIT_FREE_ON_EMPTY | UPL_COMMIT_CLEAR_DIRTY );
                    else*/
                        ubc_upl_abort_range( uplAllPages,
                                             0x0,
                                             ap->a_size,
                                             flags | UPL_ABORT_FREE_ON_EMPTY );
                } // end if( upl )
            } // end if( (! error) && (! ap->a_pl) )
            
        __exit:
            
            if( upl && mustCommit ){
                
                assert(!"UPL_NOCOMMIT should not be set for V2");
                ubc_upl_abort_range(upl, apIO.a_f_offset, apIO.a_size, UPL_ABORT_FREE_ON_EMPTY);
            }
            
            // ??? recursive
            vnode_put( vnodeIO );
        }
    }
    
    if( callOriginal ){
        
        int (*origVnop)(struct vnop_pageout_args *ap);
        
        origVnop = (int (*)(struct vnop_pageout_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_pageout );
        assert( origVnop );
        
        origVnop( ap );
    }
    
    return error;
}

//--------------------------------------------------------------------
/*
 FYI a call stack
 * thread #4: tid = 0x08bb, 0xffffff7f889bd21b VFSFilter0`QvrVnopRenameHookEx(ap=0xffffff809ff4b6e0, type=QvrVnodeRedirected) + 1003 at VFSHooks.cpp:1131, name = '0xffffff801360ca80', queue = '0x0', stop reason = step over
 * frame #0: 0xffffff7f889bd21b VFSFilter0`QvrVnopRenameHookEx(ap=0xffffff809ff4b6e0, type=QvrVnodeRedirected) + 1003 at VFSHooks.cpp:1131
 frame #1: 0xffffff7f889bc07a VFSFilter0`QvrVnopRenameHook2(ap=0xffffff809ff4b6e0) + 26 at VFSHooks.cpp:1170
 frame #2: 0xffffff800637512b kernel`VNOP_RENAME(fdvp=0xffffff8018aacf00, fvp=0xffffff80193ce5a0, fcnp=<unavailable>, tdvp=0xffffff8018aacf00, tvp=0x0000000000000000, tcnp=<unavailable>, ctx=0xffffff80134a6a30) + 107 at kpi_vfs.c:4012
 frame #3: 0xffffff80063746c3 kernel`vn_rename(fdvp=0xffffff8018aacf00, fvpp=0xffffff809ff4bed8, fcnp=<unavailable>, fvap=<unavailable>, tdvp=0xffffff8018aacf00, tvpp=<unavailable>, tcnp=0xffffff8018a7caf0, tvap=<unavailable>, flags=<unavailable>, ctx=0xffffff80134a6a30) + 1315 at kpi_vfs.c:3827
 frame #4: 0xffffff8006361ccd kernel`renameat_internal(segflg=<unavailable>, flags=<unavailable>, ctx=<unavailable>, fromfd=<unavailable>, from=<unavailable>, tofd=<unavailable>, to=<unavailable>) + 2317 at vfs_syscalls.c:6917
 frame #5: 0xffffff800664dcb2 kernel`unix_syscall64(state=0xffffff8013462260) + 610 at systemcalls.c:366
 */
int
QvrVnopRenameHookEx2(
                    __in struct vnop_rename_args *ap
                    )
/*
 struct vnop_rename_args {
 struct vnodeop_desc *a_desc;
 vnode_t a_fdvp;
 vnode_t a_fvp;
 struct componentname *a_fcnp;
 vnode_t a_tdvp;
 vnode_t a_tvp;
 struct componentname *a_tcnp;
 vfs_context_t a_context;
 } *ap;
 */
{
    errno_t    error = 0;
    vnode_t    oldFvp = NULLVP;
    vnode_t    newFvp = NULLVP;
    vnode_t    oldTvp = NULLVP;
    vnode_t    newTvp = NULLVP;
    vnode_t    originalFvp = NULLVP;
    vnode_t    originalTvp = NULLVP;
    
    int (*origVnop)(struct vnop_rename_args *ap);
    
    origVnop = (int (*)(struct vnop_rename_args*))QvrGetOriginalVnodeOp( ap->a_fvp, QvrVopEnum_rename );
    assert( origVnop );
    
    const ApplicationData* appData = VNodeMap::getVnodeAppData( ap->a_fvp );
    if( !appData )
        appData =  QvrGetApplicationDataByContext( ap->a_context, ADT_OpenExisting );  // use ADT_OpenExisting, as this should be a case of path redirection
    
    if( !appData || RecursionEngine::IsRecursiveCall() || IsUserClient() )
        return origVnop( ap );
    
    struct vnop_rename_args apRedirected = { 0 };
    struct componentname    fcnpRedirected = { 0 };
    struct componentname    tcnpRedirected = { 0 };
    
    struct vnop_rename_args apOriginal = { 0 };
    struct componentname    fcnpOriginal = { 0 };
    struct componentname    tcnpOriginal = { 0 };
    
    struct vnop_rename_args apShadow = { 0 };
    struct componentname    fcnpShadow = { 0 };
    struct componentname    tcnpShadow = { 0 };
    
    char*      fromRedirectedFilePath = NULL;
    vm_size_t  fromRedirectedFilePathSize = 0;
    char*      fromRedirectedFileName = NULL;
    vm_size_t  fromRedirectedFileNameSize = 0;
    
    char*      toRedirectedFilePath = NULL;
    vm_size_t  toRedirectedFilePathSize = 0;
    char*      toRedirectedFileName = NULL;
    vm_size_t  toRedirectedFileNameSize = 0;
    
    char*      fromShadowFilePath = NULL;
    vm_size_t  fromShadowFilePathSize = 0;
    char*      fromShadowFileName = NULL;
    vm_size_t  fromShadowFileNameSize = 0;
    
    char*      toShadowFilePath = NULL;
    vm_size_t  toShadowFilePathSize = 0;
    char*      toShadowFileName = NULL;
    vm_size_t  toShadowFileNameSize = 0;

    
    VOPFUNC    redirectedRenameVnop = NULL;
    
    if( gRedirectionIsNullAndVoid )
    {
        RecursionEngine::EnterRecursiveCall( current_thread() );
        {
            error = origVnop( ap );
        }
        RecursionEngine::LeaveRecursiveCall();
        
        goto __exit;
    }
    
    if( ! appData->redirectIO ){
        
        //
        // in that case *a_fvp is a vnode for a redirected file,
        // get the original one
        //
        RecursionEngine::EnterRecursiveCall( (void*)&apRedirected.a_fcnp );
        {
            error = vnode_lookup( ap->a_fcnp->cn_pnbuf,
                                 0x0, //VNODE_LOOKUP_NOFOLLOW,
                                 &newFvp,
                                 ap->a_context );
        }
        RecursionEngine::LeaveRecursiveCall();
        
        if( error )
            goto __exit;
        
        if( ap->a_tvp ){
            
            //
            // in that case *a_fvp is a vnode for a redirected file,
            // get the original one
            //
            RecursionEngine::EnterRecursiveCall( (void*)&apRedirected.a_tcnp );
            {
                error = vnode_lookup( ap->a_tcnp->cn_pnbuf,
                                     0x0, //VNODE_LOOKUP_NOFOLLOW,
                                     &newTvp,
                                     ap->a_context );
            }
            RecursionEngine::LeaveRecursiveCall();
            
            if( error )
                goto __exit;
        }
        
        //
        // swap vnodes
        //
        
        oldFvp = ap->a_fvp;
        ap->a_fvp = newFvp;
        
        if( newTvp ){
            oldTvp = ap->a_tvp;
            ap->a_tvp = newTvp;
        }
        
        error = origVnop( ap );
        goto __exit;
        
    } // end if( ! appData->redirectIO )
    
    //assert( NULLVP == ap->a_tvp );
    
    //
    // in most cases to rename you need
    //   a_fcnp != NULL
    //   a_tcnp != NULL
    //   a_fdvp != NULLVP
    //   a_fvp  != NULLVP
    //   a_tdvp != NULLVP
    //   a_tvp  == NULLVP
    // i.e. you do not need a_tvp
    //
    
    //
    // because of the query attribute hook the original requests contains
    // shadow vnode but original path as there is no way for an application
    // to infer shadow name for shadow vnode
    //
    apOriginal.a_desc = ap->a_desc;
    fcnpOriginal = *ap->a_fcnp;
    tcnpOriginal = *ap->a_tcnp;
    apOriginal.a_fcnp = &fcnpOriginal; // keep it as it contains original path because of getattr hook
    apOriginal.a_tcnp = &tcnpOriginal; // keep it as it contains original path because of getattr hook
    apOriginal.a_fcnp->cn_hash = 0;
    apOriginal.a_tcnp->cn_hash = 0;
    apOriginal.a_fdvp = ap->a_fdvp; // from directory is the same
    apOriginal.a_tdvp = ap->a_tdvp; // to directory is the same
    apOriginal.a_context = ap->a_context;
    
    apShadow.a_desc = ap->a_desc;
    fcnpShadow = *ap->a_fcnp;
    tcnpShadow = *ap->a_tcnp;
    apShadow.a_fcnp = &fcnpShadow; // need to be fixed to shadow path
    apShadow.a_tcnp = &tcnpShadow; // need to be fixed to shadow path
    apShadow.a_fcnp->cn_hash = 0;
    apShadow.a_tcnp->cn_hash = 0;
    apShadow.a_fvp  = ap->a_fvp; // from vnode is the same as application sees shadow vnodes
    apShadow.a_tvp  = ap->a_tvp; // to vnode is the same as application sees shadow vnodes
    apShadow.a_fdvp = ap->a_fdvp; // from directory is the same
    apShadow.a_tdvp = ap->a_tdvp; // to directory is the same
    apShadow.a_context = gSuperUserContext;
    
    apRedirected.a_desc = ap->a_desc;
    fcnpRedirected = *ap->a_fcnp;
    tcnpRedirected = *ap->a_tcnp;
    apRedirected.a_fcnp = &fcnpRedirected; // need to be fiex to redirected path
    apRedirected.a_tcnp = &tcnpRedirected; // need to be fiex to redirected path
    apRedirected.a_fcnp->cn_hash = 0;
    apRedirected.a_tcnp->cn_hash = 0;
    apRedirected.a_context = gSuperUserContext;
    
    //
    // redirected paths
    //
    error = QvrConvertToShadowAndThenRedirectedPath( apRedirected.a_fcnp->cn_pnbuf,//apRedirected.a_fcnp->cn_nameptr,
                                        appData->redirectTo,
                                        &fromRedirectedFilePath,
                                        &fromRedirectedFilePathSize);
    if( error )
        goto __exit;
    
    error = QvrConvertToShadowAndThenRedirectedPath( apRedirected.a_fcnp->cn_pnbuf,//apRedirected.a_fcnp->cn_nameptr,
                                        "", // we need only the file name transformation
                                        &fromRedirectedFileName,
                                        &fromRedirectedFileNameSize);
    if( error )
        goto __exit;
    
    error = QvrConvertToShadowAndThenRedirectedPath( apRedirected.a_tcnp->cn_pnbuf,//apRedirected.a_tcnp->cn_nameptr,
                                        appData->redirectTo,
                                        &toRedirectedFilePath,
                                        &toRedirectedFilePathSize);
    if( error )
        goto __exit;
    
    error = QvrConvertToShadowAndThenRedirectedPath( apRedirected.a_tcnp->cn_pnbuf,//apRedirected.a_tcnp->cn_nameptr,
                                        "", // we need only the file name transformation
                                        &toRedirectedFileName,
                                        &toRedirectedFileNameSize);
    if( error )
        goto __exit;
    
    //
    // shadow paths
    //
    error = QvrConvertToShadowCopyPath( ap->a_fcnp->cn_pnbuf,
                                        &fromShadowFilePath,
                                        &fromShadowFilePathSize);
    if( error )
        goto __exit;
    
    error = QvrConvertToShadowCopyPath( ap->a_fcnp->cn_nameptr,
                                        &fromShadowFileName,
                                        &fromShadowFileNameSize);
    if( error )
        goto __exit;
    
    error = QvrConvertToShadowCopyPath( ap->a_tcnp->cn_pnbuf,
                                        &toShadowFilePath,
                                        &toShadowFilePathSize);
    if( error )
        goto __exit;
    
    error = QvrConvertToShadowCopyPath( ap->a_tcnp->cn_nameptr,
                                        &toShadowFileName,
                                        &toShadowFileNameSize);
    if( error )
        goto __exit;
    
    //
    // fix the redirected path
    //
    apRedirected.a_fcnp->cn_pnbuf = fromRedirectedFilePath;
    apRedirected.a_fcnp->cn_pnlen = (int)fromRedirectedFilePathSize - sizeof('\0');
    
    apRedirected.a_fcnp->cn_nameptr = fromRedirectedFileName;
    apRedirected.a_fcnp->cn_namelen = (int)fromRedirectedFileNameSize - sizeof('\0');
    
    apRedirected.a_tcnp->cn_pnbuf = toRedirectedFilePath;
    apRedirected.a_tcnp->cn_pnlen = (int)toRedirectedFilePathSize - sizeof('\0');
    
    apRedirected.a_tcnp->cn_nameptr = toRedirectedFileName;
    apRedirected.a_tcnp->cn_namelen = (int)toRedirectedFileNameSize - sizeof('\0');
    
    //
    // fix the shadow path
    //
    apShadow.a_fcnp->cn_pnbuf = fromShadowFilePath;
    apShadow.a_fcnp->cn_pnlen = (int)fromShadowFilePathSize - sizeof('\0');
    
    apShadow.a_fcnp->cn_nameptr = fromShadowFileName;
    apShadow.a_fcnp->cn_namelen = (int)fromShadowFileNameSize - sizeof('\0');
    
    apShadow.a_tcnp->cn_pnbuf = toShadowFilePath;
    apShadow.a_tcnp->cn_pnlen = (int)toShadowFilePathSize - sizeof('\0');
    
    apShadow.a_tcnp->cn_nameptr = toShadowFileName;
    apShadow.a_tcnp->cn_namelen = (int)toShadowFileNameSize - sizeof('\0');
    
    //
    // get redirected directory vnode
    //
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        error = vnode_lookup( appData->redirectTo,
                              0x0, //VNODE_LOOKUP_NOFOLLOW,
                              &apRedirected.a_fdvp,
                              apRedirected.a_context );
    }
    RecursionEngine::LeaveRecursiveCall();
    
    if( error )
        goto __exit;
    
    //
    // get a vnode for redirection directory
    //
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        error = vnode_lookup( appData->redirectTo,
                              0x0, //VNODE_LOOKUP_NOFOLLOW,
                              &apRedirected.a_tdvp,
                              apRedirected.a_context );
    }
    RecursionEngine::LeaveRecursiveCall();
    
    if( error )
        goto __exit;
    
    //
    // get redirected vnode for "from file"
    //
    RecursionEngine::EnterRecursiveCall( (void*)&apRedirected.a_fcnp );
    {
        error = vnode_lookup( apRedirected.a_fcnp->cn_pnbuf,
                              0x0, //VNODE_LOOKUP_NOFOLLOW,
                              &apRedirected.a_fvp,
                              apRedirected.a_context );
    }
    RecursionEngine::LeaveRecursiveCall();
    
    if( error )
        goto __exit;
    
    //
    // get original vnode for "from file", as you remember an application provided a shadow vnode but not original
    //
    RecursionEngine::EnterRecursiveCall( (void*)&apRedirected.a_fcnp );
    {
        error = vnode_lookup( apOriginal.a_fcnp->cn_pnbuf,
                              0x0, //VNODE_LOOKUP_NOFOLLOW,
                              &originalFvp,
                              apOriginal.a_context );
        
        apOriginal.a_fvp = originalFvp;
    }
    RecursionEngine::LeaveRecursiveCall();
    
    if( error )
        goto __exit;
    
    if( ap->a_tvp ){
        
        RecursionEngine::EnterRecursiveCall( current_thread() );
        {
            error = vnode_lookup( apRedirected.a_tcnp->cn_pnbuf,
                                  0x0, //VNODE_LOOKUP_NOFOLLOW,
                                  &apRedirected.a_tvp,
                                  apRedirected.a_context );
        }
        RecursionEngine::LeaveRecursiveCall();
        
        if( error )
            goto __exit;
        
        RecursionEngine::EnterRecursiveCall( current_thread() );
        {
            error = vnode_lookup( apOriginal.a_tcnp->cn_pnbuf,
                                  0x0, //VNODE_LOOKUP_NOFOLLOW,
                                  &originalTvp,
                                  apOriginal.a_context );
            
            apOriginal.a_tvp = originalTvp;
        }
        RecursionEngine::LeaveRecursiveCall();
        
        if( error )
            goto __exit;
        
    } else {
        
        assert( ! ap->a_tvp );
        
        //
        // if there is no tvp then check that the file does not exist,
        // if it exists then we need rename to existing file
        //
        vnode_t   redirectedTvp = NULLVP;
        errno_t   error2;
        
        RecursionEngine::EnterRecursiveCall( current_thread() );
        {
            error2 = vnode_lookup( apRedirected.a_tcnp->cn_pnbuf,
                                   0x0, //VNODE_LOOKUP_NOFOLLOW,
                                   &redirectedTvp,
                                   apRedirected.a_context );
        }
        RecursionEngine::LeaveRecursiveCall();
        
        if( !error2 ){
            
            assert( redirectedTvp );
            
            //
            // check that the original tvp does not exist
            //
            RecursionEngine::EnterRecursiveCall( current_thread() );
            {
                error2 = vnode_lookup( apOriginal.a_tcnp->cn_pnbuf,
                                       0x0, //VNODE_LOOKUP_NOFOLLOW,
                                       &originalTvp,
                                       apOriginal.a_context );
            }
            RecursionEngine::LeaveRecursiveCall();
            
            if( ! error2 ){
                
                //
                // original tvp also exist, let the file system decides what to do,
                // most likely the rename will fail
                //
                vnode_put( originalTvp );
                originalTvp = NULLVP;
                
            } else {
                
                assert( NULLVP == originalTvp );

                //
                // original tvp doesn't exist, we need to rename to the existing file
                //
                
                RecursionEngine::EnterRecursiveCall( current_thread() );
                {
                    //
                    // truncate the existing file
                    //
                    QvrVnodeSetsize( redirectedTvp, 0x0, 0x0, gSuperUserContext );
                }
                RecursionEngine::LeaveRecursiveCall();
                
                apRedirected.a_tvp = redirectedTvp;
                vnode_get( apRedirected.a_tvp );
            }
            
            vnode_put( redirectedTvp );
            redirectedTvp = NULLVP;
            
            assert( ! originalTvp && ! redirectedTvp );
        } // end  if( !error2 )
        
        assert( ! redirectedTvp );
    } // end for if( ap->a_tvp ) { ... } else { ... }
    
    redirectedRenameVnop = QvrGetVnop( apRedirected.a_fvp, &vnop_rename_desc );
    assert( redirectedRenameVnop );
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        error = redirectedRenameVnop( &apRedirected );
        if( !error ){
            
            //
            // shadow and original files are in the same directory so they are on the same file system as Mac OS X doesn't support union FS,
            // so use the same origVnop
            //
            error = origVnop( &apShadow );
            if( ! error && gSynchroniseWithOriginal )
                error = origVnop( &apOriginal );
        }
        
        if( ! error ){
            
            //
            // remove the association in case FS decides to create a new vnode for renamed file,
            // the association will be restored on next lookup
            //
            VNodeMap::removeShadowReverse( apShadow.a_fvp );
            
            //
            // clear the name cache
            //
            cache_purge( apShadow.a_fvp );
            cache_purge( originalFvp );
        }
    }
    RecursionEngine::LeaveRecursiveCall();
    
__exit:
    
    VFSData audit;
    VFSInitData( &audit, VFSDataType_Audit );
    audit.Data.Audit.op = VFSOpcode_Rename;
    audit.Data.Audit.path = ap->a_fcnp->cn_pnbuf;
    audit.Data.Audit.redirectedPath = ap->a_tcnp->cn_pnbuf;
    audit.Data.Audit.error = error;
    gVnodeGate->sendVFSDataToClient( &audit );
    
    if( fromRedirectedFilePath && toRedirectedFilePath ){
        
        VFSData audit;
        VFSInitData( &audit, VFSDataType_Audit );
        audit.Data.Audit.op = VFSOpcode_Rename;
        audit.Data.Audit.path = fromRedirectedFilePath;
        audit.Data.Audit.redirectedPath = toRedirectedFilePath;
        audit.Data.Audit.error = error;
        gVnodeGate->sendVFSDataToClient( &audit );
        
    }
    
    //
    // release redirected path vnodes
    //
    if( apRedirected.a_fdvp )
        vnode_put( apRedirected.a_fdvp );
    
    if( apRedirected.a_fvp )
        vnode_put( apRedirected.a_fvp );
    
    if( apRedirected.a_tdvp )
        vnode_put( apRedirected.a_tdvp );
    
    if( apRedirected.a_tvp )
        vnode_put( apRedirected.a_tvp );
    
    if( fromShadowFilePath )
        QvrFreeShadowPath( fromShadowFilePath, fromShadowFilePathSize );
    
    if( fromShadowFileName )
        QvrFreeShadowPath( fromShadowFileName, fromShadowFileNameSize );
    
    if( toShadowFilePath )
        QvrFreeShadowPath( toShadowFilePath, toShadowFilePathSize );
    
    if( toShadowFileName )
        QvrFreeShadowPath( toShadowFileName, toShadowFileNameSize );
    
    if( fromRedirectedFilePath )
        QvrFreeRedirectedPath( fromRedirectedFilePath, fromRedirectedFilePathSize );
    
    if( fromRedirectedFileName )
        QvrFreeRedirectedPath( fromRedirectedFileName, fromRedirectedFileNameSize );
    
    if( toRedirectedFilePath )
        QvrFreeRedirectedPath( toRedirectedFilePath, toRedirectedFilePathSize );
    
    if( toRedirectedFileName )
        QvrFreeRedirectedPath( toRedirectedFileName, toRedirectedFileNameSize );
    
    if( originalFvp )
        vnode_put( originalFvp );
    
    if( originalTvp )
        vnode_put( originalTvp );
    
    if( newFvp )
        vnode_put( newFvp );
    
    if( newTvp )
        vnode_put( newTvp );
    
    if( oldFvp )
        ap->a_fvp = oldFvp;
    
    if( oldTvp )
        ap->a_tvp = oldTvp;
    
    return error;
}

//--------------------------------------------------------------------

int
QvrVnopExchangeHookEx2(
                       __in struct vnop_exchange_args *ap
                       )
{
    int (*origVnop)(struct vnop_exchange_args *ap);
    
    origVnop = (int (*)(struct vnop_exchange_args*))QvrGetOriginalVnodeOp( ap->a_fvp, QvrVopEnum_exchange );
    assert( origVnop );
    
    const ApplicationData* appData = VNodeMap::getVnodeAppData( ap->a_fvp );
    if( !appData )
        appData =  QvrGetApplicationDataByContext( ap->a_context, ADT_OpenExisting );  // use ADT_OpenExisting, as this should be a case of path redirection
    
    if( !appData || (!appData->redirectIO) || RecursionEngine::IsRecursiveCall() || IsUserClient() )
        return origVnop( ap );
    
    
    //
    // TO DO , there is no implementation for path redirection
    //
    assert( appData->redirectIO );
    
    errno_t   error = 0x0;
    
    //
    // get the backing vnodes
    //
    vnode_t backingFvp = NULLVP;
    vnode_t backingTvp = NULLVP;
    
    backingFvp = VNodeMap::getVnodeIORef( ap->a_fvp );
    assert( backingFvp ); // because appData->redirectIO == true
    if( backingFvp ){
    
        backingTvp = VNodeMap::getVnodeIORef( ap->a_tvp );
        assert( backingTvp ); // because appData->redirectIO == true
        if( backingTvp ){
            
            int   fromPathLen = MAXPATHLEN;
            char* fromPath = (char*)IOMalloc( MAXPATHLEN );

            int   toPathLen = MAXPATHLEN;
            char* toPath = (char*)IOMalloc( MAXPATHLEN );
            
            if( !fromPath || !toPath )
                error = ENOMEM;
            
            if( ! error )
                error = vn_getpath( (vnode_t)backingFvp, fromPath, &fromPathLen );
            
            assert( ! error );
            
            if( ! error )
                error = vn_getpath( (vnode_t)backingTvp, toPath, &toPathLen );
            
            if( ! error ){
                
                QvrPreOperationCallback  inData;
                
                bzero( &inData, sizeof(inData) );
                
                inData.op = VFSOpcode_Exchange;
                
                inData.Parameters.Exchange.from = fromPath;
                inData.Parameters.Exchange.to   = toPath;
                
                QvrPreOperationCallbackAndWaitForReply( &inData );
                
            } // end if( ! error )
            
            if( fromPath )
                IOFree( fromPath, MAXPATHLEN );
            
            fromPath = NULL;
            
            if( toPath )
                IOFree( toPath, MAXPATHLEN );
            
            toPath = NULL;
            
            /*
            int (*backingVnop)(struct vnop_exchange_args *ap);
            
            backingVnop = (int (*)(struct vnop_exchange_args *ap)) QvrGetVnop( backingFvp, &vnop_exchange_desc );
            assert( backingVnop );
             assert( (void*)backingVnop == (void*)QvrGetVnop( backingTvp, &vnop_exchange_desc ));
             
             struct vnop_exchange_args backingAp = *ap;
             
             backingAp.a_fvp = backingFvp;
             backingAp.a_tvp = backingTvp;
             backingAp.a_context = gSuperUserContext;
             
             error = backingVnop( &backingAp );
             if( ! error ){
             
             //
             // force update now as some FSD might incorrectly exchange files that actually changes vnode
             //
             //QvrAssociateVnodeForRedirectedIO( ap->a_fvp, appData, true );
             //QvrAssociateVnodeForRedirectedIO( ap->a_tvp, appData, true );
             VNodeMap::removeVnodeIO( ap->a_fvp );
             VNodeMap::removeVnodeIO( ap->a_tvp );
             cache_purge( ap->a_fvp );
             cache_purge( ap->a_tvp );
             }
             */
            
        } // end if( backingTvp )
    } // end if( backingFvp )
    
__exit:
    
    if( ! error )
        error = origVnop( ap );
    
    if( backingFvp )
        vnode_put( backingFvp );
    
    if( backingTvp )
        vnode_put( backingTvp );
    
    return error;
}


//--------------------------------------------------------------------

int
QvrVnopGetattrHookEx2(
    __in struct vnop_getattr_args *ap
    )
{
    int (*origVnop)(struct vnop_getattr_args *ap);
    
    origVnop = (int (*)(struct vnop_getattr_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_getattr );
    assert( origVnop );
    
    vnode_t  realVnodeRef = VNodeMap::getVnodeShadowReverseRef( ap->a_vp );
    
    if( !realVnodeRef || RecursionEngine::IsRecursiveCall() || IsUserClient() ){
        
        if( realVnodeRef )
            vnode_put( realVnodeRef );
        
        return origVnop( ap );
    }
    
    errno_t   error = ENODEV;
    
    VOPFUNC  realVnodeVnop = QvrGetVnop( realVnodeRef, &vnop_getattr_desc );
    
    vnode_t  shadowVp = ap->a_vp;
    ap->a_vp = realVnodeRef;
    
    RecursionEngine::EnterRecursiveCall( current_thread() );
    {
        error = realVnodeVnop( ap );
        if( ! error ){
            
            //
            // adjust the size to an controlled file size
            //
            vnode_t  backingVnodeRef = VNodeMap::getVnodeIORef( shadowVp );
            if( backingVnodeRef ){
                
                struct vnode_attr	va;
                bool                queryAttributes = false;
                
                VATTR_INIT(&va);
                
                //
                // uint64_t	va_total_size;	/* size in bytes of all forks */
                // uint64_t	va_total_alloc;	/* disk space used by all forks */
                // uint64_t	va_data_size;	/* size in bytes of the fork managed by current vnode */
                // uint64_t	va_data_alloc;	/* disk space used by the fork managed by current vnode */
                //
                
                if( VATTR_IS_ACTIVE( ap->a_vap, va_total_size ) ){
                    VATTR_WANTED(&va, va_total_size);
                    queryAttributes = true;
                }
                
                if( VATTR_IS_ACTIVE( ap->a_vap, va_total_alloc ) ){
                    VATTR_WANTED(&va, va_total_alloc);
                    queryAttributes = true;
                }
                
                if( VATTR_IS_ACTIVE( ap->a_vap, va_data_size ) ){
                    VATTR_WANTED(&va, va_data_size);
                    queryAttributes = true;
                }
                
                if( VATTR_IS_ACTIVE( ap->a_vap, va_data_alloc ) ){
                    VATTR_WANTED(&va, va_data_alloc);
                    queryAttributes = true;
                }
                
                if( queryAttributes ){
                    
                    int			        attrError;
                    
                    attrError = vnode_getattr( backingVnodeRef, &va, gSuperUserContext );
                    
                    if( ! attrError ){
                        
                        if( VATTR_IS_ACTIVE( ap->a_vap, va_total_size ) ){
                            ap->a_vap->va_total_size = va.va_total_size;
                        }
                        
                        if( VATTR_IS_ACTIVE( ap->a_vap, va_total_alloc ) ){
                            ap->a_vap->va_total_alloc = va.va_total_alloc;
                        }
                        
                        if( VATTR_IS_ACTIVE( ap->a_vap, va_data_size ) ){
                            ap->a_vap->va_data_size = va.va_data_size;
                        }
                        
                        if( VATTR_IS_ACTIVE( ap->a_vap, va_data_alloc ) ){
                            ap->a_vap->va_data_alloc = va.va_data_alloc;
                        }
                    } // end if( ! attrError )
                } // end if( queryAttributes )
                
                vnode_put( backingVnodeRef );
                
            } // end if( backingVnodeRef )
            
        } // end if( ! error )
    }
    RecursionEngine::LeaveRecursiveCall();
    
    ap->a_vp = shadowVp;

    if( realVnodeRef )
        vnode_put( realVnodeRef );
    
    return error;
}

//--------------------------------------------------------------------

int
QvrFsdReclaimHookEx2(struct vnop_reclaim_args *ap)
/*
 struct vnop_reclaim_args {
 struct vnodeop_desc *a_desc;
 struct vnode *a_vp;
 vfs_context_t a_context;
 } *ap;
 */
/*
 called from the vnode_reclaim_internal() function
 vnode_reclaim_internal()->vgone()->vclean()->VNOP_RECLAIM()
 */
{
    //
    // check that v_data is not NULL, this is a sanity check to be sure we don't damage FSD structures,
    // some FSDs set v_data to NULL intentionally, for example see the msdosfs's msdosfs_check_link()
    // that calls vnode_clearfsnode() for a temporary vnode of VNON type
    //
    assert( !( VNON != vnode_vtype( ap->a_vp ) && NULL == vnode_fsnode( ap->a_vp ) ) );
    
    int (*origVnop)(struct vnop_reclaim_args *ap);
    
    origVnop = (int (*)(struct vnop_reclaim_args*))QvrGetOriginalVnodeOp( ap->a_vp, QvrVopEnum_reclaim );
    assert( origVnop );
    
    QvrUnHookVnodeVopAndParent( ap->a_vp );
    
    VNodeMap::removeVnodeAppData( ap->a_vp );
    VNodeMap::removeVnodeIO( ap->a_vp );
    VNodeMap::removeShadowReverse( ap->a_vp );
    
    return origVnop( ap );
}

//--------------------------------------------------------------------
