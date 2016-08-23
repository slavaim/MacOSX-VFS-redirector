//
//  VFSFilter0UserClient.h
//  VFSFilter0
//
//  Created by slava on 6/6/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__VFSFilter0UserClient__
#define __VFSFilter0__VFSFilter0UserClient__

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IODataQueue.h>
#include <sys/vm.h>
#include "Common.h"
#include "VFSFilter0.h"
#include "VFSFilter0UserClientInterface.h"

//--------------------------------------------------------------------

class com_VFSFilter0;

typedef struct _VFSAudit{
    vnode_t     vn;
    char*       path;
    char*       redirectedPath;
    VFSOpcode   op;
    errno_t     error;
} VFSAudit;

typedef struct _VFSPreOperationCallback{
    VFSOpcode   op;
    int64_t     id;
    
    union{
        
        struct {
            //
            // either path or vnode must be provided
            //
            char*       path;
            vnode_t     vn;
            
            char*       redirectedPath;
            
            char*       shadowFilePath; // optional
            
            bool        calledFromCreate;
        } Lookup;
        
        struct {
            
            char*      from;
            char*      to;
            
        } Rename;
        
        struct {
            
            char*      from;
            char*      to;
            
        } Exchange;
        
        struct {
            
            VFSOpcode   op;
            char*       path;
            
        } Filter;
        
    } Parameters;
    
} VFSPreOperationCallback;

typedef struct _VFSData{
    
    VFSDataHeader    Header;
    
    struct{
        bool WasEnqueued;
    } Status;
    
    union{
        VFSAudit                  Audit;
        VFSPreOperationCallback   PreOperationCallback;
    } Data;
    
} VFSData;

//--------------------------------------------------------------------

__inline
void
VFSInitData(
    __in VFSData* Data,
    __in VFSDataType Type
    )
{
    bzero( Data, sizeof(*Data) );
    
    Data->Header.Version = VFS_DATA_TYPE_VER;
    Data->Header.Type = Type;
}

//--------------------------------------------------------------------

typedef struct _QvrPreOperationCallbackData{
    
    VFSOpcode   op;
    
    union {
        
        struct {
            __in     char*  pathToLookup;
            __in_opt char*  shadowFilePath;
            __in     char*  redirectedFilePath;
            __in     bool   calledFromCreate;
        } Lookup;
        
        struct {
            __in     char*  from;
            __in     char*  to;
        } Exchange;
        
        struct {
            struct {
                __in     VFSOpcode op;
                __in     char*     path;
            } in;
            
            struct {
                __out    bool      isControlledFile;
#if	MACH_ASSERT
                __out    bool      replyWasReceived;
                __out    bool      noClient;
#endif // DBG
            } out;
            
        } Filter;
        
    } Parameters;
    
} QvrPreOperationCallback;

void
QvrPreOperationCallbackAndWaitForReply( __in QvrPreOperationCallback* data );

//--------------------------------------------------------------------

// the user client class
class VFSFilter0UserClient : public IOUserClient
{
    OSDeclareDefaultStructors(VFSFilter0UserClient)
    
private:
    task_t                           fClient;
    proc_t                           fClientProc;
    com_VFSFilter0*           fProvider;
    IODataQueue*                     fDataQueue;
    IOMemoryDescriptor*              fSharedMemory;
    kauth_listener_t                 fListener;
    IOLock*                          queueLock;
    
public:
    virtual bool     start(IOService *provider);
    virtual void     stop(IOService *provider);
    virtual IOReturn open(void);
    virtual IOReturn clientClose(void);
    virtual IOReturn close(void);
    virtual bool     terminate(IOOptionBits options);
    virtual IOReturn startLogging(void);
    virtual IOReturn stopLogging(void);
    virtual void     free();
    
    virtual bool     initWithTask( task_t owningTask, void *securityID, UInt32 type);
    
    virtual IOReturn registerNotificationPort( mach_port_t port, UInt32 type, UInt32 refCon);
    
    virtual IOReturn clientMemoryForType(UInt32 type, IOOptionBits *options,
                                         IOMemoryDescriptor **memory);
    
    virtual IOExternalMethod *getTargetAndMethodForIndex(IOService **target,
                                                         UInt32 index);
    
    virtual void sendVFSDataToClient( __in VFSData* data );
    
    virtual void preOperationCallbackAndWaitForReply( __in QvrPreOperationCallback* inData );
    
    virtual IOReturn reply( __in  void *vInBuffer, //VFSClientReply
                            __out void *vOutBuffer,
                            __in  void *vInSize,
                            __in  void *vOutSizeP,
                           void *, void *);
    
    virtual proc_t   getProc(){ return fClientProc; }
};

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__VFSFilter0UserClient__) */
