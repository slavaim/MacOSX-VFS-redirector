
#include <IOKit/IOLib.h>
#include <IOKit/IODataQueueShared.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSContainers.h>
#include <IOKit/assert.h>
#include <IOKit/IOCatalogue.h>
#include "VFSFilter0.h"
#include "VFSHooks.h"
#include "Kauth.h"
#include "VNode.h"
#include "VNodeHook.h"

//--------------------------------------------------------------------

com_VFSFilter0* com_VFSFilter0::Instance;

//--------------------------------------------------------------------

//
// the standard IOKit declarations
//
#undef super
#define super IOService

OSDefineMetaClassAndStructors(com_VFSFilter0, IOService)

//
// gSuperUserContext might contain an invalid thread and process pointer if derived from a process
// that terminates!!!!
//
vfs_context_t  gSuperUserContext;

//--------------------------------------------------------------------

bool
com_VFSFilter0::start(
    __in IOService *provider
    )
{
    
    //__asm__ volatile( "int $0x3" );
    
    VNodeMap::Init();
    
    if( kIOReturnSuccess != VFSHookInit() ){
        
        DBG_PRINT_ERROR( ( "VFSHookInit() failed\n" ) );
        goto __exit_on_error;
    }
    
    if( ! QvrVnodeHooksHashTable::CreateStaticTableWithSize( 8, true ) ){
        
        DBG_PRINT_ERROR( ( "QvrVnodeHooksHashTable::CreateStaticTableWithSize() failed\n" ) );
        goto __exit_on_error;
    }

    
    //
    // gSuperUserContext must have a valid thread and process pointer
    // TO DO redesign this! Indefinit holding a thread or task object is a bad behaviour.
    //
    thread_reference( current_thread() );
    task_reference( current_task() );
    
    gSuperUserContext = vfs_context_create(NULL); // vfs_context_kernel()
    
    //
    // create an object for the vnodes KAuth callback and register the callback,
    // the callback might be called immediatelly just after registration!
    //
    gVnodeGate = QvrIOKitKAuthVnodeGate::withCallbackRegistration( this );
    assert( NULL != gVnodeGate );
    if( NULL == gVnodeGate ){
        
        DBG_PRINT_ERROR( ( "QvrIOKitKAuthVnodeGate::withDefaultSettings() failed\n" ) );
        goto __exit_on_error;
    }
    
    Instance = this;
    
    //
    // register with IOKit to allow the class matching
    //
    registerService();

    return true;
    
__exit_on_error:
    
    //
    // all cleanup will be done in stop() and free()
    //
    this->release();
    return false;
}

//--------------------------------------------------------------------

IOReturn
com_VFSFilter0::newUserClient(
    __in task_t owningTask,
    __in void*,
    __in UInt32 type,
    __in IOUserClient **handler
    )
{
    return kIOReturnNoResources;
}

//--------------------------------------------------------------------

void
com_VFSFilter0::stop(
    __in IOService * provider
    )
{
    super::stop( provider );
}

//--------------------------------------------------------------------

bool com_VFSFilter0::init()
{
    if(! super::init() )
        return false;
    
    return true;
}

//--------------------------------------------------------------------

//
// actually this will not be called as the module should be unloadable in release build
//
void com_VFSFilter0::free()
{
    if( gVnodeGate ){
        
        gVnodeGate->release();
        gVnodeGate = NULL;
    }
    
    QvrVnodeHooksHashTable::DeleteStaticTable();
    
    VFSHookRelease();
    
    super::free();
}

//--------------------------------------------------------------------

IOReturn com_VFSFilter0::unregisterUserClient( __in VFSFilter0UserClient* client )
{
    bool   unregistered;
    VFSFilter0UserClient*  currentClient;
    
    currentClient = (VFSFilter0UserClient*)this->userClient;
    assert( currentClient == client );
    if( currentClient != client ){
        
        DBG_PRINT_ERROR(("currentClient != client\n"));
        return kIOReturnError;
    }
    
    this->pendingUnregistration = true;
    
    unregistered = OSCompareAndSwapPtr( (void*)currentClient, NULL, &this->userClient );
    assert( unregistered && NULL == this->userClient );
    if( !unregistered ){
        
        DBG_PRINT_ERROR(("!unregistered\n"));
        
        this->pendingUnregistration = false;
        return kIOReturnError;
    }
    
    do { // wait for any existing client invocations to return
        
        struct timespec ts = { 1, 0 }; // one second
        (void)msleep( &this->clientInvocations,      // wait channel
                      NULL,                          // mutex
                      PUSER,                         // priority
                      "com_VFSFilter0::unregisterUserClient()", // wait message
                      &ts );                         // sleep interval
        
    } while( this->clientInvocations != 0 );
    
    currentClient->release();
    this->pendingUnregistration = false;
    
    return unregistered? kIOReturnSuccess: kIOReturnError;
}

//--------------------------------------------------------------------

IOReturn com_VFSFilter0::registerUserClient( __in VFSFilter0UserClient* client )
{
    bool registered;
    
    if( this->pendingUnregistration ){
        
        DBG_PRINT_ERROR(("com_VFSFilter0 : pendingUnregistration\n"));
        return kIOReturnError;
    }
    
    registered = OSCompareAndSwapPtr( NULL, (void*)client, &this->userClient );
    assert( registered );
    if( !registered ){
        
        DBG_PRINT_ERROR(("com_VFSFilter0 : a client was not registered\n"));
        return kIOReturnError;
    }
    
    client->retain();
    
    return registered? kIOReturnSuccess: kIOReturnError;
}

//--------------------------------------------------------------------

VFSFilter0UserClient* com_VFSFilter0::getClient()
/*
 if non NULL is returned the putClient() must be called
 */
{
    VFSFilter0UserClient*  currentClient;
    
    //
    // if ther is no user client, then nobody call for logging
    //
    if( NULL == this->userClient || this->pendingUnregistration )
        return NULL;
    
    OSIncrementAtomic( &this->clientInvocations );
    
    currentClient = (VFSFilter0UserClient*)this->userClient;
    
    //
    // if the current client is NULL or can't be atomicaly exchanged
    // with the same value then the unregistration is in progress,
    // the call to OSCompareAndSwapPtr( NULL, NULL, &this->userClient )
    // checks the this->userClient for NULL atomically
    //
    if( !currentClient ||
       !OSCompareAndSwapPtr( currentClient, currentClient, &this->userClient ) ||
       OSCompareAndSwapPtr( NULL, NULL, &this->userClient ) ){
        
        //
        // the unregistration is in the progress and waiting for all
        // invocations to return
        //
        assert( this->pendingUnregistration );
        if( 0x1 == OSDecrementAtomic( &this->clientInvocations ) ){
            
            //
            // this was the last invocation
            //
            wakeup( &this->clientInvocations );
        }
        
        return NULL;
    }
    
    return currentClient;
}

void com_VFSFilter0::putClient()
{
    //
    // do not exchange or add any condition before OSDecrementAtomic as it must be always done!
    //
    if( 0x1 == OSDecrementAtomic( &this->clientInvocations ) && NULL == this->userClient ){
        
        //
        // this was the last invocation
        //
        wakeup( &this->clientInvocations );
    }
}

//--------------------------------------------------------------------


void com_VFSFilter0::sendVFSDataToClient(  __in VFSData* data )
{
    VFSFilter0UserClient*  currentClient = getClient();
    if( !currentClient )
        return;
    
    currentClient->sendVFSDataToClient( data );
    
    putClient();
}

//--------------------------------------------------------------------

proc_t  com_VFSFilter0::getUserClientProc()
{
    VFSFilter0UserClient*  currentClient = getClient();
    if( !currentClient )
        return NULL;
    
    proc_t   proc = currentClient->getProc();
    
    putClient();
    
    return proc;
}

//--------------------------------------------------------------------

bool com_VFSFilter0::IsUserClient( __in_opt vfs_context_t context )
{
    proc_t  proc;
    
    if( context )
        proc = vfs_context_proc( context );
    else
        proc = current_proc();
    
    if( proc )
        return proc == getInstance()->getUserClientProc();
    
    return false;
}

bool IsUserClient(){ return com_VFSFilter0::IsUserClient(); }

//--------------------------------------------------------------------


