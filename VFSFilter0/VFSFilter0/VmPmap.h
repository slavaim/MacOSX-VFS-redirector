//
//  VmPmap.h
//  VFSFilter0
//
//  Created by slava on 4/06/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__VmPmap__
#define __VFSFilter0__VmPmap__

#include "Common.h"

//--------------------------------------------------------------------

#ifdef __cplusplus
extern "C" {
#endif
    
#include <mach/mach_types.h>
#include <mach/vm_param.h>
    
    //--------------------------------------------------------------------

#define	INTEL_PGBYTES		I386_PGBYTES
#define INTEL_PGSHIFT		I386_PGSHIFT
#define INTEL_OFFMASK	(I386_PGBYTES - 1)
#define PG_FRAME        0x000FFFFFFFFFF000ULL
    
    //--------------------------------------------------------------------

    typedef uint64_t  pmap_paddr_t;
    typedef void* pmap_t;
    
    //--------------------------------------------------------------------

    extern
    void
    bcopy_phys(
               addr64_t src64,
               addr64_t dst64,
               vm_size_t bytes);
    
    extern
    ppnum_t
    pmap_find_phys(pmap_t pmap, addr64_t va);
    
    extern pmap_t	kernel_pmap;
    
    //--------------------------------------------------------------------

    
#ifdef __cplusplus
}
#endif

//--------------------------------------------------------------------

addr64_t
QvrVirtToPhys(
              __in vm_offset_t addr
              );


unsigned int
QvrWriteWiredSrcToWiredDst(
                           __in vm_offset_t  src,
                           __in vm_offset_t  dst,
                           __in vm_size_t    len
                           );

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__VmPmap__) */
