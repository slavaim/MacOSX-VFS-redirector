#ifndef __VFSFilter0__VFSFilter0__
#define __VFSFilter0__VFSFilter0__

#include <IOKit/IOService.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IODataQueue.h>

#include "Common.h"
#include "VFSFilter0UserClient.h"
#include "RecursionEngine.h"

//--------------------------------------------------------------------

class VFSFilter0UserClient;

//--------------------------------------------------------------------

//
// the I/O Kit driver class
//
class com_VFSFilter0 : public IOService
{
    OSDeclareDefaultStructors(com_VFSFilter0)
    
public:
    virtual bool start(IOService *provider);
    virtual void stop( IOService * provider );
    
    virtual IOReturn newUserClient( __in task_t owningTask,
                                    __in void*,
                                    __in UInt32 type,
                                    __in IOUserClient **handler );
    
    IOReturn registerUserClient( __in VFSFilter0UserClient* client );
    IOReturn unregisterUserClient( __in VFSFilter0UserClient* client );
    static bool IsUserClient( __in_opt vfs_context_t context = NULL );
    
    virtual void sendVFSDataToClient( __in struct _VFSData* data );
    
    static com_VFSFilter0*  getInstance(){ return com_VFSFilter0::Instance; };
    
    VFSFilter0UserClient* getClient();
    void putClient();
    
protected:
    
    virtual bool init();
    virtual void free();
    
private:
    
    proc_t         getUserClientProc();
    
private:
    
    //
    // the object is retained
    //
    volatile VFSFilter0UserClient* userClient;
    
    bool             pendingUnregistration;
    UInt32           clientInvocations;
    
    static com_VFSFilter0* Instance;
    
};

//--------------------------------------------------------------------

bool IsUserClient();

//--------------------------------------------------------------------

#endif//__VFSFilter0__VFSFilter0__

