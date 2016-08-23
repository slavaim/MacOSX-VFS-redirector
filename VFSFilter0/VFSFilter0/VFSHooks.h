//
//  VFSHooks.h
//  VFSFilter0
//
//  Created by slava on 3/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__VFSHooks__
#define __VFSFilter0__VFSHooks__

#include <IOKit/IOService.h>
#include <IOKit/assert.h>

#ifdef __cplusplus
extern "C" {
#endif
    
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/vnode.h>
    
#ifdef __cplusplus
}
#endif

#include "Common.h"

//--------------------------------------------------------------------

typedef enum _QvrVnodeType{
    QvrVnodeOthers = 0,
    QvrVnodeWatched,
    QvrVnodeRedirected,
    
    //
    // always the last
    //
    QvrVnodeMaximum
} QvrVnodeType;

//--------------------------------------------------------------------

IOReturn
VFSHookInit();

void
VFSHookRelease();

bool
QvrHookVnodeVop(
    __inout vnode_t      vnode,
    __in    QvrVnodeType type
    );

int
QvrVnopLookupHookEx2(
                     __inout struct vnop_lookup_args *ap
                     );

int
QvrVnopCreateHookEx2(
                     __inout struct vnop_create_args *ap
                     );

int
QvrVnopCloseHookEx2(
                   __inout struct vnop_close_args *ap
                   );

int
QvrVnopReadHookEx2(
                   __in struct vnop_read_args *ap
                   );
int
QvrVnopOpenHookEx2(
                   __in struct vnop_open_args *ap
                   );
int
QvrVnopPageinHookEx2(
                     __in struct vnop_pagein_args *ap
                     );

int
QvrVnopWriteHookEx2(
                    __in struct vnop_write_args *ap
                    );

int
QvrVnopPageoutHookEx2(
                      __in struct vnop_pageout_args *ap
                      );

int
QvrVnopRenameHookEx2(
                     __in struct vnop_rename_args *ap
                     );

int
QvrVnopExchangeHookEx2(
                       __in struct vnop_exchange_args *ap
                       );

int
QvrFsdReclaimHookEx2(struct vnop_reclaim_args *ap);

int
QvrVnopInactiveHookEx2(struct vnop_inactive_args *ap);

int
QvrVnopGetattrHookEx2(
                      __in struct vnop_getattr_args *ap
                      );

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__VFSHooks__) */
