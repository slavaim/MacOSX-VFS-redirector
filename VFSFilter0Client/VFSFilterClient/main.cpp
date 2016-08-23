//
//  main.cpp
//  VFSFilterClient
//
//  Created by slava on 6/06/2015.
//

#include <iostream>
#include <IOKit/IOKitLib.h>
#include <IOKit/IODataQueueShared.h>
#include <IOKit/IODataQueueClient.h>

#include <mach/mach.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/acl.h>

#include "../../VFSFilter0/VFSFilter0/VFSFilter0UserClientInterface.h"

int cp(const char *to, const char *from);

const char*
OpcodeToString(
    VFSOpcode op
    )
{
    switch( op ){
        case VFSOpcode_Unknown:
            return "Unknown";
        case VFSOpcode_Lookup:
            return "Lookup";
        case VFSOpcode_Create:
            return "Create";
        case VFSOpcode_Read:
            return "Read";
        case VFSOpcode_Write:
            return "Write";
        case VFSOpcode_Rename:
            return "Rename";
        case VFSOpcode_Exchange:
            return "Exchange";
    }

    return "Wrong Opcode Value";
}

void
VFSFilter0NotificationHandler(void* ctx)
{
    io_connect_t        connection = *(io_connect_t*)ctx;
    kern_return_t       kr;
    VFSFilter0UserClientData  vdata;
    UInt32              dataSize;
    IODataQueueMemory  *queueMappedMemory;
    vm_size_t           queueMappedMemorySize;
    mach_vm_address_t   address = 0;
    mach_vm_size_t      size = 0;
    unsigned int        msgType = 1; // family-defined port type (arbitrary)
    mach_port_t         recvPort;
    
    // allocate a Mach port to receive notifications from the IODataQueue
    if (!(recvPort = IODataQueueAllocateNotificationPort())) {
        fprintf(stderr, "failed to allocate notification port\n");
        return;
    }
    
    // this will call registerNotificationPort() inside our user client class
    kr = IOConnectSetNotificationPort(connection, msgType, recvPort, 0);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "failed to register notification port (%d)\n", kr);
        mach_port_destroy(mach_task_self(), recvPort);
        return;
    }
    
    // this will call clientMemoryForType() inside our user client class
    kr = IOConnectMapMemory(connection, kIODefaultMemoryType,
                            mach_task_self(), &address, &size, kIOMapAnywhere);
    if (kr != kIOReturnSuccess) {
        fprintf(stderr, "failed to map memory (%d)\n", kr);
        mach_port_destroy(mach_task_self(), recvPort);
        return;
    }
    
    queueMappedMemory = (IODataQueueMemory *)address;
    queueMappedMemorySize = size;
    
    printf("reading the driver output ...\n");
    
    while (IODataQueueWaitForAvailableData(queueMappedMemory, recvPort) == kIOReturnSuccess) {
        
        while (IODataQueueDataAvailable(queueMappedMemory)) {
            
            dataSize = sizeof(vdata);
            kr = IODataQueueDequeue(queueMappedMemory, &vdata, &dataSize);
            if (kr != kIOReturnSuccess) {
                
                fprintf(stderr, "*** error in receiving data (%d)\n", kr);
                continue;
            }
            
            if( VFSDataType_Audit == vdata.Header.Type ){
                
                printf("%s : \"%s\" -> \"%s\" \n",
                       OpcodeToString(vdata.Data.Audit.opcode),
                       vdata.Data.Audit.path,
                       vdata.Data.Audit.redirectedPath);
                
            } else if( VFSDataType_PreOperationCallback == vdata.Header.Type ){
                
                //
                // reply
                //
                VFSClientReply  reply = {0};
                
                reply.id = vdata.Data.PreOperationCallback.id;
                
                switch( vdata.Data.PreOperationCallback.op ){
                        
                    case VFSOpcode_Lookup:
                    {
                        //
                        // copy to the protected storage
                        //
                        //printf("copy( to = %s, from = %s ) \n", vdata.Data.PreOperationCallback.Parameters.Lookup.redirectedPath, vdata.Data.PreOperationCallback.Parameters.Lookup.path );
                        cp( vdata.Data.PreOperationCallback.Parameters.Lookup.redirectedPath, vdata.Data.PreOperationCallback.Parameters.Lookup.path );
                        
                        break;
                    }
                        
                    case VFSOpcode_Exchange:
                    {
                        printf("exchangedata( %s, %s )\n", vdata.Data.PreOperationCallback.Parameters.Exchange.from, vdata.Data.PreOperationCallback.Parameters.Exchange.to);
                        
                        int error = exchangedata( vdata.Data.PreOperationCallback.Parameters.Exchange.from,
                                                  vdata.Data.PreOperationCallback.Parameters.Exchange.to,
                                                  0 );
                        
                        printf("exchangedata() returned %d\n", error);
                        
                        break;
                    }
                        
                    case VFSOpcode_Filter:
                    {
                        //printf("filter( %s )\n", vdata.Data.PreOperationCallback.Parameters.Filter.path);
                        reply.Data.Filter.isControlledFile = 1;
                        break;

                    }
                        
                    default:
                        fprintf(stderr, "**** oooops in PreOperationCallback\n");
                        break;
                        
                } // end switch
                
                kern_return_t status = IOConnectCallStructMethod(connection,
                                                                 kt_kVnodeWatcherUserClientReply,
                                                                 &reply,
                                                                 sizeof(reply),
                                                                 NULL,
                                                                 0x0);
                if (status != KERN_SUCCESS) {
                    
                    fprintf(stderr, "*** IOConnectCallStructMethod returned an error (%d)\n", status);
                }

            }
        } // end while (IODataQueueDataAvailable(queueMappedMemory))
    } // end while (IODataQueueWaitForAvailableData
    
exit:
    
    kr = IOConnectUnmapMemory(connection, kIODefaultMemoryType,
                              mach_task_self(), address);
    if (kr != kIOReturnSuccess)
        fprintf(stderr, "failed to unmap memory (%d)\n", kr);
    
    mach_port_destroy(mach_task_self(), recvPort);
    
    return;
}


int main(int argc, const char * argv[])
{
    kern_return_t   kr;
    int             ret;
    int             opt;
    
    setbuf(stdout, NULL);
    
    //
    // load the driver
    // TO DO
    
    io_service_t service = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                       IOServiceMatching("com_VFSFilter0"));
    
    if (!service){
        fprintf(stderr, "IOServiceGetMatchingService failed to retrieve matching objects\n" );
        return  -1;
    }
    
    io_connect_t connection;
    
    kern_return_t status = IOServiceOpen(service,
                                         mach_task_self(),
                                         0,
                                         &connection);
    
    IOObjectRelease(service);
    
    if (status != kIOReturnSuccess){
        fprintf(stderr, "IOServiceOpen failed to open com_VFSFilter0 driver\n" );
        return  -1;
    }
    
    kr = IOConnectCallScalarMethod(connection, kt_kVnodeWatcherUserClientOpen, NULL, 0, NULL, NULL);
    if (kr != KERN_SUCCESS) {
        IOServiceClose(connection);
        
        fprintf(stderr, "com_VFSFilter0 service is busy\n");
        return  -1;
    }
    
    pthread_t       dataQueueThread;
    
    ret = pthread_create(&dataQueueThread, (pthread_attr_t *)0,
                         (void *(*)(void *))VFSFilter0NotificationHandler, (void *)&connection);
    if (ret)
        perror("pthread_create");
    else
        pthread_join(dataQueueThread, (void **)&kr);
    
    (void)IOServiceClose(connection);
    
    return 0;
}


int cp(const char *to, const char *from)
{
    int fd_to, fd_from;
    char buf[4096];
    ssize_t nread;
    int saved_errno;
    
    fd_from = open(from, O_RDONLY);
    if (fd_from < 0)
        return -1;
    
    //////////////////
    /*
    fd_to = open(to, O_RDONLY);
    if (fd_to > 0){
        close(fd_from);
        close(fd_to);
        return 0;
    }
     */
    ///////////////
    
    printf("copying data from %s to %s\n", from, to);
    
    fd_to = open(to, O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0666);
    if (fd_to < 0)
        goto out_error;
    
    while (nread = read(fd_from, buf, sizeof buf), nread > 0)
    {
        char *out_ptr = buf;
        ssize_t nwritten;
        
        do {
            nwritten = write(fd_to, out_ptr, nread);
            
            if (nwritten >= 0)
            {
                nread -= nwritten;
                out_ptr += nwritten;
            }
            else if (errno != EINTR)
            {
                goto out_error;
            }
        } while (nread > 0);
    }
    
    if (nread == 0)
    {
        if (close(fd_to) < 0)
        {
            fd_to = -1;
            goto out_error;
        }
        close(fd_from);
        
        /* Success! */
        return 0;
    }
    
out_error:
    saved_errno = errno;
    
    close(fd_from);
    if (fd_to >= 0)
        close(fd_to);
    
    errno = saved_errno;
    return -1;
}
