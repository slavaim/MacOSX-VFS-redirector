//
//  VFSFilter0UserClient.cpp
//  VFSFilter0
//
//  Created by slava on 6/6/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#include <IOKit/IODataQueueShared.h>
#include "VFSFilter0UserClient.h"
#include "VNode.h"
#include "WaitingList.h"

//--------------------------------------------------------------------

#undef super
#define super IOUserClient
OSDefineMetaClassAndStructors(VFSFilter0UserClient, IOUserClient)

//--------------------------------------------------------------------

static const IOExternalMethod sMethods[kt_kVnodeWatcherUserClientNMethods] =
{
    {
        NULL,
        (IOMethod)&VFSFilter0UserClient::open,
        kIOUCScalarIScalarO,
        0,
        0
    },
    {
        NULL,
        (IOMethod)&VFSFilter0UserClient::close,
        kIOUCScalarIScalarO,
        0,
        0
    },
    { // kt_kVnodeWatcherUserClientReply
        NULL,
        (IOMethod)&VFSFilter0UserClient::reply,
        kIOUCStructIStructO,
        kIOUCVariableStructureSize,
        kIOUCVariableStructureSize
    },
};

//--------------------------------------------------------------------

DataMap    gPreOperationDataMap;

//--------------------------------------------------------------------

enum { kt_kMaximumEventsToHold = 512 };

//--------------------------------------------------------------------

bool
VFSFilter0UserClient::start(IOService *provider)
{
    fProvider = OSDynamicCast(com_VFSFilter0, provider);
    assert( fProvider );
    if (!fProvider)
        return false;
    
    if (!super::start(provider))
        return false;
    
    fDataQueue = IODataQueue::withCapacity( (sizeof(VFSFilter0UserClientData)) * kt_kMaximumEventsToHold +
                                            DATA_QUEUE_ENTRY_HEADER_SIZE);
    
    if (!fDataQueue)
        return false;
    
    fSharedMemory = fDataQueue->getMemoryDescriptor();
    if (!fSharedMemory) {
        fDataQueue->release();
        fDataQueue = NULL;
        return false;
    }
    
    fProvider->registerUserClient( this );
    
    return true;
}

//--------------------------------------------------------------------

void
VFSFilter0UserClient::stop(IOService *provider)
{
    
    if (fDataQueue) {
        UInt8 message = kt_kStopListeningToMessages;
        fDataQueue->enqueue(&message, sizeof(message));
    }
    
    if (fSharedMemory) {
        fSharedMemory->release();
        fSharedMemory = NULL;
    }
    
    if (fDataQueue) {
        fDataQueue->release();
        fDataQueue = NULL;
    }
    
    super::stop(provider);
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::open(void)
{
    if (isInactive())
        return kIOReturnNotAttached;
    
    if (!fProvider->open(this))
        return kIOReturnExclusiveAccess; // only one user client allowed
    
    return startLogging();
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::clientClose(void)
{
    close();
    terminate(0);
    
    fClient = NULL;
    fProvider = NULL;
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::close(void)
{
    if (!fProvider)
        return kIOReturnNotAttached;
    
    //
    // release all vnodes to avoid stalling on system shutdown when the system
    // waits for vnode iocount drops to zero on unmount
    //
    VNodeMap::releaseAllVnodeIO();
    VNodeMap::releaseAllShadowReverse();
    
    fProvider->unregisterUserClient( this );
    
    if (fProvider->isOpen(this))
        fProvider->close(this);
    
    return kIOReturnSuccess;
}


//--------------------------------------------------------------------

void
VFSFilter0UserClient::preOperationCallbackAndWaitForReply(
    __in QvrPreOperationCallback* inData
)
{
    assert( preemption_enabled() );
    
    VFSData    data;
    void*      id = &data;
    bool       insertedInPreOperationDataMap = false;
    
    switch( inData->op ){
            
        case VFSOpcode_Lookup:
        {
            char*  pathToLookup       = inData->Parameters.Lookup.pathToLookup;
            char*  redirectedFilePath = inData->Parameters.Lookup.redirectedFilePath;
            char*  shadowFilePath     = inData->Parameters.Lookup.shadowFilePath;
            bool   calledFromCreate   = inData->Parameters.Lookup.calledFromCreate;
            
            //
            // prepare a notification for a client
            //
            
            VFSInitData( &data, VFSDataType_PreOperationCallback );
            
            data.Data.PreOperationCallback.op = VFSOpcode_Lookup;
            data.Data.PreOperationCallback.id = (int64_t)id;
            data.Data.PreOperationCallback.Parameters.Lookup.path = pathToLookup;
            data.Data.PreOperationCallback.Parameters.Lookup.redirectedPath = redirectedFilePath;
            data.Data.PreOperationCallback.Parameters.Lookup.shadowFilePath = shadowFilePath;
            data.Data.PreOperationCallback.Parameters.Lookup.calledFromCreate = calledFromCreate;
            
            break;
        }
        /*
        case VFSOpcode_Rename:
        {
            //
            // prepare a notification for a client
            //
            
            VFSInitData( &data, VFSDataType_PreOperationCallback );
            
            data.Data.PreOperationCallback.op = VFSOpcode_Rename;
            data.Data.PreOperationCallback.id = (int64_t)id;
            data.Data.PreOperationCallback.Parameters.Rename.from = inData->Parameters.Rename.from;
            data.Data.PreOperationCallback.Parameters.Rename.to   = inData->Parameters.Rename.to;
            
            break;
        }
        */
        case VFSOpcode_Exchange:
        {
            //
            // prepare a notification for a client
            //
            
            VFSInitData( &data, VFSDataType_PreOperationCallback );
            
            data.Data.PreOperationCallback.op = VFSOpcode_Exchange;
            data.Data.PreOperationCallback.id = (int64_t)id;
            data.Data.PreOperationCallback.Parameters.Exchange.from = inData->Parameters.Exchange.from;
            data.Data.PreOperationCallback.Parameters.Exchange.to   = inData->Parameters.Exchange.to;
            
            break;
        }
            
        case VFSOpcode_Filter:
        {
            //
            // prepare a notification for a client
            //
            
            VFSInitData( &data, VFSDataType_PreOperationCallback );
            
            data.Data.PreOperationCallback.op = VFSOpcode_Filter;
            data.Data.PreOperationCallback.id = (int64_t)id;
            data.Data.PreOperationCallback.Parameters.Filter.op   = inData->Parameters.Filter.in.op;
            data.Data.PreOperationCallback.Parameters.Filter.path = inData->Parameters.Filter.in.path;
            
            insertedInPreOperationDataMap = gPreOperationDataMap.addDataByKey( id, inData );
            assert( insertedInPreOperationDataMap );
            if( ! insertedInPreOperationDataMap )
                inData->Parameters.Filter.out.isControlledFile = false;
            
            break;
        }
            
        default:
            assert( !"An unknown inData->op was provided to VFSFilter0UserClient::preOperationCallbackAndWaitForReply" );
            break;
    }
    
    //
    // enter in the waiting list
    //
    if( gFileOpenWaitingList.enter( id ) ){
        
        //
        // notify a user client
        //
        this->sendVFSDataToClient( &data );
        
        if( data.Status.WasEnqueued ){
            
            //
            // wait for a client response
            //
            gFileOpenWaitingList.wait( id );
            
        } else {
            
            //
            // just remove the waiting entry by signalling it
            //
            gFileOpenWaitingList.signal( id );
        }
    } // end if( gFileOpenWaitingList.enter
    
    if( insertedInPreOperationDataMap ){
        
        gPreOperationDataMap.removeKey( id );
        assert( NULL == gPreOperationDataMap.getDataByKey( id ) );
        insertedInPreOperationDataMap = false;
    }
}

void
QvrPreOperationCallbackAndWaitForReply(
    __in QvrPreOperationCallback* data
    )

{
    VFSFilter0UserClient*  client = com_VFSFilter0::getInstance()->getClient();
    if( ! client ){
     
#if	MACH_ASSERT
        if( VFSOpcode_Filter == data->op )
            data->Parameters.Filter.out.noClient = true;
#endif // DBG
        
        return;
    }
    
    client->preOperationCallbackAndWaitForReply( data );
    
    com_VFSFilter0::getInstance()->putClient();
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::reply(
                            __in  void *vInBuffer, //VFSClientReply
                            __out void *vOutBuffer,
                            __in  void *vInSize,
                            __in  void *vOutSizeP,
                            void *, void *)
{
    IOReturn         RC = kIOReturnSuccess;
    VFSClientReply*  reply = (VFSClientReply*)vInBuffer;
    vm_size_t        inSize = (vm_size_t)vInSize;
    
    //
    // there is no output data
    //
    *(UInt32*)vOutSizeP = 0x0;
    
    if( inSize < sizeof( *reply ) )
        return kIOReturnBadArgument;
    
    //
    // it is safe to use a value returned by getDataByKey as it will not be deleted untill a corresponding event
    // is set to a signal state
    //
    QvrPreOperationCallback* inData = (QvrPreOperationCallback*)gPreOperationDataMap.getDataByKey( (void*)reply->id );
    if( inData ){
        
        //
        // we are waiting for some decision made by the daemon
        //
        switch( inData->op )
        {
            case VFSOpcode_Filter:
                inData->Parameters.Filter.out.isControlledFile = ( 0x0 != reply->Data.Filter.isControlledFile );
#if	MACH_ASSERT
                inData->Parameters.Filter.out.replyWasReceived = true;
#endif // MACH_ASSERT
                break;
                
            default:
                break;
        } // end switch
    }
    
    //
    // invalidate the pointer before signalling a waiting thread that will deallocate the memory
    //
    inData = NULL;
    
    //
    // wakeup a waiting thread
    //
    gFileOpenWaitingList.signal( (void*)reply->id );
    
    return RC;
}

//--------------------------------------------------------------------

bool
VFSFilter0UserClient::terminate(IOOptionBits options)
{
    //
    // if somebody does a kextunload while a client is attached
    //
    if (fProvider && fProvider->isOpen(this))
        fProvider->close(this);
    
    (void)stopLogging();
    
    return super::terminate(options);
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::startLogging(void)
{
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::stopLogging(void)
{
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

bool
VFSFilter0UserClient::initWithTask(task_t owningTask, void *securityID, UInt32 type)
{
    if (!super::initWithTask(owningTask, securityID , type))
        return false;
    
    if (!owningTask)
        return false;
    
    queueLock = IOLockAlloc();
    assert( queueLock );
    if( ! queueLock )
        return false;
    
    fClient = owningTask;
    fClientProc = current_proc();
    fProvider = NULL;
    fDataQueue = NULL;
    fSharedMemory = NULL;
    
    return true;
}

//--------------------------------------------------------------------

void
VFSFilter0UserClient::free()
{
    if( queueLock )
        IOLockFree( queueLock );
    
    super::free();
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::registerNotificationPort(mach_port_t port, UInt32 type, UInt32 ref)
{
    if ((!fDataQueue) || (port == MACH_PORT_NULL))
        return kIOReturnError;
    
    fDataQueue->setNotificationPort(port);
    
    return kIOReturnSuccess;
}

//--------------------------------------------------------------------

IOReturn
VFSFilter0UserClient::clientMemoryForType(UInt32 type, IOOptionBits *options,
                               IOMemoryDescriptor **memory)
{
    *memory = NULL;
    *options = 0;
    
    if (type == kIODefaultMemoryType) {
        if (!fSharedMemory)
            return kIOReturnNoMemory;
        fSharedMemory->retain(); // client will decrement this reference
        *memory = fSharedMemory;
        return kIOReturnSuccess;
    }
    
    // unknown memory type
    return kIOReturnNoMemory;
}

//--------------------------------------------------------------------

IOExternalMethod *
VFSFilter0UserClient::getTargetAndMethodForIndex(IOService **target, UInt32 index)
{
    if (index >= (UInt32)kt_kVnodeWatcherUserClientNMethods)
        return NULL;
    
    switch (index) {
        case kt_kVnodeWatcherUserClientOpen:
        case kt_kVnodeWatcherUserClientClose:
        case kt_kVnodeWatcherUserClientReply:
            *target = this;
            break;
            
        default:
            *target =  fProvider;
            break;
    }
    
    return (IOExternalMethod *)&sMethods[index];
}

//--------------------------------------------------------------------

void VFSFilter0UserClient::sendVFSDataToClient( __in VFSData* kernelData  )
{
    UInt32 size;
    VFSFilter0UserClientData data;
    
    bzero( &data, sizeof(data));
    
    assert( kernelData->Header.Type < VFSDataType_Count );
    
    data.Header = kernelData->Header;
    
    switch( kernelData->Header.Type ){
        case VFSDataType_Audit:
        {
            bool emptyPath = false;
            bool emptyRedirectedPath = false;
            
            data.Data.Audit.opcode = kernelData->Data.Audit.op;
            data.Data.Audit.error  = kernelData->Data.Audit.error;
            
            size = sizeof( data ) - sizeof( data.Data.Audit.redirectedPath );
            
            if( kernelData->Data.Audit.path ){
                
                size_t name_len = strlen( kernelData->Data.Audit.path ) + sizeof( '\0' );
                
                if( name_len <= sizeof( data.Data.Audit.path ) )
                    memcpy( data.Data.Audit.path, kernelData->Data.Audit.path, name_len );
                else
                    emptyPath = true;
                
            } else if( !kernelData->Data.Audit.path && kernelData->Data.Audit.vn ){
                
                int name_len = sizeof(data.Data.Audit.path);
                
                errno_t error = vn_getpath( (vnode_t)kernelData->Data.Audit.vn, data.Data.Audit.path, &name_len );
                if( error )
                    emptyPath = true;
                
            } else {
                
                emptyPath = true;
            }
            
            if( emptyPath ){
                
                data.Data.Audit.path[0] = '\0';
            }
            
            if( kernelData->Data.Audit.redirectedPath ){
                
                size_t name_len = strlen( kernelData->Data.Audit.redirectedPath ) + sizeof( '\0' );
                
                if( name_len <= sizeof( data.Data.Audit.redirectedPath ) ){
                    
                    memcpy( data.Data.Audit.redirectedPath, kernelData->Data.Audit.redirectedPath, name_len );
                    size += name_len;
                    
                } else {
                    emptyRedirectedPath = true;
                }
                
            } else {
                
                emptyRedirectedPath = true;
            }
            
            if( emptyRedirectedPath ){
                data.Data.Audit.redirectedPath[0] = '\0';
                size += sizeof( '\0' );
            }
            break;
        }
            
        case VFSDataType_PreOperationCallback:
        {
            data.Data.PreOperationCallback.id = kernelData->Data.PreOperationCallback.id;
            data.Data.PreOperationCallback.op = kernelData->Data.PreOperationCallback.op;
            
            switch( kernelData->Data.PreOperationCallback.op ){
                    
                case VFSOpcode_Lookup:
                {
                    
                    bool emptyPath = false;
                    
                    data.Data.PreOperationCallback.Parameters.Lookup.calledFromCreate = kernelData->Data.PreOperationCallback.Parameters.Lookup.calledFromCreate;
                    
                    size = sizeof( data ) - sizeof( data.Data.PreOperationCallback.Parameters.Lookup.path );
                    
                    if( kernelData->Data.PreOperationCallback.Parameters.Lookup.redirectedPath ){
                        
                        size_t name_len = strlen( kernelData->Data.PreOperationCallback.Parameters.Lookup.redirectedPath ) + sizeof( '\0' );
                        
                        if( name_len <= sizeof( data.Data.PreOperationCallback.Parameters.Lookup.redirectedPath ) )
                            memcpy( data.Data.PreOperationCallback.Parameters.Lookup.redirectedPath, kernelData->Data.PreOperationCallback.Parameters.Lookup.redirectedPath, name_len );
                        else
                            emptyPath = true;
                        
                    } else {
                        
                        emptyPath = true;
                    }
                    
                    if( emptyPath ){
                        
                        data.Data.PreOperationCallback.Parameters.Lookup.redirectedPath[0] = '\0';
                    }
                    
                    emptyPath= false;
                    
                    if( kernelData->Data.PreOperationCallback.Parameters.Lookup.path ){
                        
                        size_t name_len = strlen( kernelData->Data.PreOperationCallback.Parameters.Lookup.path ) + sizeof( '\0' );
                        
                        if( name_len <= sizeof( data.Data.PreOperationCallback.Parameters.Lookup.path ) )
                            memcpy( data.Data.PreOperationCallback.Parameters.Lookup.path, kernelData->Data.PreOperationCallback.Parameters.Lookup.path, name_len );
                        else
                            emptyPath = true;
                        
                    } else if( kernelData->Data.PreOperationCallback.Parameters.Lookup.vn ){
                        
                        int name_len = sizeof(data.Data.PreOperationCallback.Parameters.Lookup.path );
                        
                        errno_t error = vn_getpath( (vnode_t)kernelData->Data.PreOperationCallback.Parameters.Lookup.vn, data.Data.PreOperationCallback.Parameters.Lookup.path, &name_len );
                        if( error )
                            emptyPath = true;
                        
                    } else {
                        
                        emptyPath = true;
                    }
                    
                    if( emptyPath ){
                        
                        data.Data.PreOperationCallback.Parameters.Lookup.path[0] = '\0';
                    }
                    
                    emptyPath = false;
                    
                    if( kernelData->Data.PreOperationCallback.Parameters.Lookup.shadowFilePath ){
                        
                        size_t name_len = strlen( kernelData->Data.PreOperationCallback.Parameters.Lookup.shadowFilePath ) + sizeof( '\0' );
                        
                        if( name_len <= sizeof( data.Data.PreOperationCallback.Parameters.Lookup.shadowFilePath ) )
                            memcpy( data.Data.PreOperationCallback.Parameters.Lookup.shadowFilePath, kernelData->Data.PreOperationCallback.Parameters.Lookup.shadowFilePath, name_len );
                        else
                            emptyPath = true;
                        
                    } else {
                        
                        emptyPath = true;
                    }
                    
                    if( emptyPath ){
                        
                        data.Data.PreOperationCallback.Parameters.Lookup.shadowFilePath[0] = '\0';
                    }
                    
                    
                    size += strlen( data.Data.PreOperationCallback.Parameters.Lookup.shadowFilePath ) + sizeof( '\0' );
                    break;
                }
                    
                case VFSOpcode_Exchange:
                {
                    
                    size = sizeof( data ) - sizeof( data.Data.PreOperationCallback.Parameters.Exchange.to );
                    
                    size_t name_len = strlen( kernelData->Data.PreOperationCallback.Parameters.Exchange.from ) + sizeof( '\0' );
                    
                    if( name_len <= sizeof( data.Data.PreOperationCallback.Parameters.Exchange.from ) )
                        memcpy( data.Data.PreOperationCallback.Parameters.Exchange.from, kernelData->Data.PreOperationCallback.Parameters.Exchange.from, name_len );
                    else
                        data.Data.PreOperationCallback.Parameters.Exchange.from[0] = '\0';

                    name_len = strlen( kernelData->Data.PreOperationCallback.Parameters.Exchange.to ) + sizeof( '\0' );
                    
                    if( name_len <= sizeof( data.Data.PreOperationCallback.Parameters.Exchange.to ) )
                        memcpy( data.Data.PreOperationCallback.Parameters.Exchange.to, kernelData->Data.PreOperationCallback.Parameters.Exchange.to, name_len );
                    else
                        data.Data.PreOperationCallback.Parameters.Exchange.to[0] = '\0';
                    
                    size += strlen( data.Data.PreOperationCallback.Parameters.Exchange.to ) + sizeof( '\0' );
                    
                    break;
                }
                    
                case VFSOpcode_Filter:
                {
                    data.Data.PreOperationCallback.Parameters.Filter.op = kernelData->Data.PreOperationCallback.Parameters.Filter.op;
                    
                    size = sizeof( data ) - sizeof( data.Data.PreOperationCallback.Parameters.Filter.path );
                    
                    size_t name_len = strlen( kernelData->Data.PreOperationCallback.Parameters.Filter.path ) + sizeof( '\0' );
                    
                    if( name_len <= sizeof( data.Data.PreOperationCallback.Parameters.Filter.path ) )
                        memcpy( data.Data.PreOperationCallback.Parameters.Filter.path, kernelData->Data.PreOperationCallback.Parameters.Filter.path, name_len );
                    else
                        data.Data.PreOperationCallback.Parameters.Filter.path[0] = '\0';
                    
                    size += strlen( data.Data.PreOperationCallback.Parameters.Filter.path ) + sizeof( '\0' );
                    
                    break;
                }
                    
                default:
                {
                    assert( !"An unknown kernelData->Data.PreOperationCallback.op was provided to VFSFilter0UserClient::sendVFSDataToClient" );
                    break;
                }
            }
            
            break;
        }
            
        default:
            size = 0;
            break;
    } // end switch
    

    if( size ){
        
        IOLockLock( this->queueLock );
        {// start of the lock
            fDataQueue->enqueue( &data, size );
        }// end of the lock
        IOLockUnlock( this->queueLock );
        
        kernelData->Status.WasEnqueued = true;
        
    } // end if( size )

}

//--------------------------------------------------------------------
