//
//  ApplicationsData.h
//  VFSFilter0
//
//  Created by slava on 6/19/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__ApplicationsData__
#define __VFSFilter0__ApplicationsData__

#include "Common.h"
#include "RecursionEngine.h"

#include <libkern/c++/OSObject.h>
#include <IOKit/assert.h>
#include <libkern/c++/OSObject.h>
#include <libkern/c++/OSArray.h>

//--------------------------------------------------------------------

typedef enum ApplicationDataType{
    ADT_CreateNew = 0,
    ADT_OpenExisting,
    
    ADT_TypesCount
} ADT;

//--------------------------------------------------------------------

class ApplicationData{
    
public:
    const char*   redirectTo;
    const char*   applicationShortName;
    bool          redirectIO;
};

//--------------------------------------------------------------------

const ApplicationData*
QvrGetApplicationDataByName(
    __in const OSSymbol* name,
    __in ADT type
    );

const ApplicationData*
QvrGetApplicationDataByContext(
    __in vfs_context_t  context,
    __in ADT type
    );

//--------------------------------------------------------------------

const OSSymbol*
QvrGetProcessNameByPid(
    __in pid_t     pid
    );

const OSSymbol*
QvrGetProcessNameByContext(
    __in vfs_context_t  context
    );

//--------------------------------------------------------------------

/*
class ApplicationsSet {
    
protected:
    
    OSDictionary*   byName;
    OSArray*        byPid;
    
public:
    
    
};
 */


#endif /* defined(__VFSFilter0__ApplicationsData__) */
