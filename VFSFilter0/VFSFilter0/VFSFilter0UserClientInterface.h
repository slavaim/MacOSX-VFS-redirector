//
//  VFSFilter0UserClientInterface.h
//  VFSFilter0
//
//  Created by slava on 6/6/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef VFSFilter0_VFSFilter0UserClientInterface_h
#define VFSFilter0_VFSFilter0UserClientInterface_h

#ifdef __cplusplus
extern "C" {
#endif
    
#include <sys/param.h>
#include <sys/kauth.h>
#include <sys/vnode.h>
    
    //--------------------------------------------------------------------

    #define  VFS_DATA_TYPE_VER   0x1
    
    //--------------------------------------------------------------------

    typedef enum {
        VFSOpcode_Unknown = 0,
        VFSOpcode_Lookup,
        VFSOpcode_Create,
        VFSOpcode_Read,
        VFSOpcode_Write,
        VFSOpcode_Rename,
        VFSOpcode_Exchange,
        VFSOpcode_Open,
        VFSOpcode_Filter // a fake opcode used for filtering
    } VFSOpcode;
    
    //--------------------------------------------------------------------

    typedef enum {
        VFSDataType_Unknown = 0, // an invalid value
        VFSDataType_Audit,
        VFSDataType_PreOperationCallback,
        
        VFSDataType_Count // a number of types, must be the last member of the enum
    } VFSDataType;
    
    //--------------------------------------------------------------------

    typedef struct _VFSDataHeader{
        int32_t        Version; // VFS_DATA_TYPE_VER
        VFSDataType    Type;
    } VFSDataHeader;
    
    //--------------------------------------------------------------------

    typedef struct _VFSFilter0UserClientAudit{
        VFSOpcode     opcode;
        errno_t       error;
        char          path[MAXPATHLEN];
        
        //
        // this one must always be the last as being
        // cut to save space
        //
        char          redirectedPath[MAXPATHLEN];
    } VFSFilter0UserClientAudit;
    
    //--------------------------------------------------------------------

    typedef struct _VFSFilter0PreOperationCallback{
        VFSOpcode   op;
        int64_t     id;
        
        union {
            
            struct {
                UInt16      calledFromCreate; // a bool value {0,1}
                char        redirectedPath[MAXPATHLEN];
                char        path[MAXPATHLEN];
                
                //
                // this one must always be the last as being
                // cut to save space
                //
                char        shadowFilePath[MAXPATHLEN];
            } Lookup;
            
            struct {
                char        from[MAXPATHLEN];
                
                //
                // this one must always be the last as being
                // cut to save space
                //
                char        to[MAXPATHLEN];
            } Exchange;
            
            struct {
                //
                // an actual operation that requested filtering
                //
                VFSOpcode   op;
                
                //
                // a path to a file, the file might not exist as in case of VFSOpcode_Create,
                // this one must always be the last as being cut to save space
                //
                char        path[MAXPATHLEN];
            } Filter;
            
        } Parameters;
        
    } VFSFilter0PreOperationCallback;
    
    //--------------------------------------------------------------------

    typedef struct _VFSFilter0UserClientData{
        
        VFSDataHeader    Header;
        
        union{
            VFSFilter0UserClientAudit        Audit;
            VFSFilter0PreOperationCallback   PreOperationCallback;
        } Data;
        
    } VFSFilter0UserClientData;
    
    //--------------------------------------------------------------------

    typedef struct _VFSClientReply{
        
        int64_t    id;
        
        union{
            struct{
                int8_t  isControlledFile;
            } Filter;
        } Data;
        
    } VFSClientReply;
    
    //--------------------------------------------------------------------

    enum {
        kt_kVnodeWatcherUserClientOpen,
        kt_kVnodeWatcherUserClientClose,
        kt_kVnodeWatcherUserClientReply,
        
        kt_kVnodeWatcherUserClientNMethods,// the number of methods available to a client
        kt_kStopListeningToMessages = 0xff,
    };
    
    //--------------------------------------------------------------------

#ifdef __cplusplus
}
#endif

#endif
