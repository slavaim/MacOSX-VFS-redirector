/*
 *
 *  Created by Slava
 *  Copyright 2015 Slava Imameev. All rights reserved.
 *
 */

/*
 the file contains an implementation for MAC plugin module ( Mandatory Access Control (MAC) framework ),
 MAC is a Mac OS X clone clone for TrustedBSD. For more information see
 http://www.trustedbsd.org
 */

#ifndef _QVRMACPOLICY_H
#define _QVRMACPOLICY_H

#include <IOKit/IOService.h>
#include <IOKit/assert.h>
#include <sys/types.h>
#include <sys/kauth.h>
extern "C" {
    #include <security/mac_policy.h>
}
#include "Common.h"

class QvrMacPolicy : public OSObject
{
    
    OSDeclareDefaultStructors( QvrMacPolicy )
    
private:
    
    bool                    registered;
    
    struct mac_policy_conf* mpc;
    mac_policy_handle_t     handlep;
    void*                   xd;
    
protected:
    
    virtual void free();
    virtual bool initWithPolicyConf( __in struct mac_policy_conf *mpc, __in void* xd );
    
public:
    
    // a caller must guarantee the validity of provided pointer until the object is destroyed
    static QvrMacPolicy*  createPolicyObject( __in struct mac_policy_conf *mpc, __in void* xd );
    
    kern_return_t registerMacPolicy();
    kern_return_t unRegisterMacPolicy();
};

/*
 * Flags to control which MAC subsystems are enforced
 * on a per-process/thread/credential basis.
 The defenitions were borrowed from the XNU kernel sources, look at /security/mac.h file.
 */
#define MAC_SYSTEM_ENFORCE	0x0001	/* system management */
#define MAC_PROC_ENFORCE	0x0002	/* process management */
#define MAC_MACH_ENFORCE	0x0004	/* mach interfaces */
#define MAC_VM_ENFORCE		0x0008	/* VM interfaces */
#define MAC_FILE_ENFORCE	0x0010	/* file operations */
#define MAC_SOCKET_ENFORCE	0x0020	/* socket operations */
#define MAC_PIPE_ENFORCE	0x0040	/* pipes */
#define MAC_VNODE_ENFORCE	0x0080	/* vnode operations */
#define MAC_NET_ENFORCE		0x0100	/* network management */
#define MAC_MBUF_ENFORCE	0x0200	/* network traffic */
#define MAC_POSIXSEM_ENFORCE	0x0400	/* posix semaphores */
#define MAC_POSIXSHM_ENFORCE	0x0800	/* posix shared memory */
#define MAC_SYSVMSG_ENFORCE	0x1000	/* SysV message queues */
#define MAC_SYSVSEM_ENFORCE	0x2000	/* SysV semaphores */
#define MAC_SYSVSHM_ENFORCE	0x4000	/* SysV shared memory */
#define MAC_ALL_ENFORCE		0x7fff	/* enforce everything */

#endif // _QVRMACPOLICY_H