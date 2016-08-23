//
//  ApplicationsData.cpp
//  VFSFilter0
//
//  Created by slava on 6/19/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#include <libkern/c++/OSSymbol.h>
#include "ApplicationsData.h"

//--------------------------------------------------------------------

#define  WordDirectory  "/work1/my_word" 

const static ApplicationData   gApplicationsData[2][3] =
{
    //
    // CreateNew
    //
    {
        {
            .applicationShortName = "Microsoft Word",
            .redirectTo = WordDirectory,
            .redirectIO = true
        },
        
        {
            .applicationShortName = "Preview",
            .redirectTo = "/work1/my_preview",
            .redirectIO = true
        },
        
        {
            .applicationShortName = "AdobeReader",
            .redirectTo = "/work1/my_adobe",
            .redirectIO = true
        },
    },
    
    //
    // OpenExisting
    //
    {
        {
            .applicationShortName = "Microsoft Word",
            .redirectTo = WordDirectory,
            .redirectIO = false
        },
        
        {
            .applicationShortName = "Preview",
            .redirectTo = "/work1/my_preview",
            .redirectIO = false
        },
        
        {
            .applicationShortName = "AdobeReader",
            .redirectTo = "/work1/my_adobe",
            .redirectIO = false
        },
    }
};

//--------------------------------------------------------------------

const OSSymbol*
QvrGetProcessNameByPid(
    __in pid_t     pid
    )
/*
 a caller must release the returned object
 */
{
    char p_comm[MAXCOMLEN + 1];
    
    //bzero( p_comm, sizeof(p_comm) );
    proc_name( pid, p_comm, sizeof( p_comm ) );
    
    const OSSymbol*  name = OSSymbol::withCString( p_comm );
    
    return name;
}

const OSSymbol*
QvrGetProcessNameByContext(
    __in vfs_context_t  context
    )
/*
 a caller must release the returned object
 */
{
    return QvrGetProcessNameByPid( vfs_context_pid( context ) );
}

//--------------------------------------------------------------------

const ApplicationData*
QvrGetApplicationDataByName(
    __in const OSSymbol* name,
    __in ADT type
    )
{
    for( int i = 0; i < __countof( gApplicationsData[type] ); ++i ){
        
        if( name->isEqualTo( gApplicationsData[type][ i ].applicationShortName ) )
            return &gApplicationsData[type][ i ];
    }
    
    return NULL;
}

const ApplicationData*
QvrGetApplicationDataByContext(
    __in vfs_context_t  context,
    __in ADT type
    )
{
    const OSSymbol*  name = QvrGetProcessNameByContext( context );
    assert( name );
    if( ! name )
        return NULL;
    
    const ApplicationData* data = QvrGetApplicationDataByName( name, type );
    
    name->release();
    
    return data;
}

//--------------------------------------------------------------------
