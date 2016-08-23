//
//  Kauth.h
//  VFSFilter0
//
//  Created by slava on 3/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__Kauth__
#define __VFSFilter0__Kauth__

#include <IOKit/IOService.h>
#include <IOKit/assert.h>

#ifdef __cplusplus
extern "C" {
#endif
    
#include <sys/types.h>
#include <sys/kauth.h>
    
#ifdef __cplusplus
}
#endif


#include "Common.h"
#include "QvrMacPolicy.h"

//--------------------------------------------------------------------

class com_VFSFilter0;

class QvrIOKitKAuthVnodeGate : public OSObject
{
    OSDeclareDefaultStructors( QvrIOKitKAuthVnodeGate )
    
private:
    
    //
    // the callback is called when a vnode is being created or have been created depending on the type of a file open,
    // also the callback is called when the vnode is being accessed
    //
    static int VnodeAuthorizeCallback( kauth_cred_t    credential, // reference to the actor's credentials
                                       void           *idata,      // cookie supplied when listener is registered
                                       kauth_action_t  action,     // requested action
                                       uintptr_t       arg0,       // the VFS context
                                       uintptr_t       arg1,       // the vnode in question
                                       uintptr_t       arg2,       // parent vnode, or NULL
                                       uintptr_t       arg3);      // pointer to an errno value
    
    static int	MacVnodeCheckLookup( kauth_cred_t cred,
                                     struct vnode *dvp,
                                     struct label *dlabel,
                                     struct componentname *cnp
                                    );
    
    //
    // KAUTH_SCOPE_VNODE listener, used for the acess permissions check
    //
    kauth_listener_t                 VnodeListener;
    
    //
    // a driver's class
    //
    com_VFSFilter0*           provider;
    
    QvrMacPolicy*                    macPolicy;
    
    struct mac_policy_ops            mpoServiceProtection;
    struct mac_policy_conf           mpcServiceProtection;
    
protected:
    
    virtual bool init();
    virtual void free();
    
public:
    
    virtual IOReturn  RegisterVnodeScopeCallback(void);
    
    static QvrIOKitKAuthVnodeGate*  withCallbackRegistration( __in com_VFSFilter0* provider );
    
    virtual void sendVFSDataToClient( __in struct _VFSData* data );
    
};

//--------------------------------------------------------------------

extern QvrIOKitKAuthVnodeGate*     gVnodeGate;

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__Kauth__) */
