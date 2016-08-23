//
//  WaitingList.h
//  VFSFilter0
//
//  Created by slava on 24/07/2015.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__WaitingList__
#define __VFSFilter0__WaitingList__

#include "Common.h"
#include "RecursionEngine.h"

//--------------------------------------------------------------------

class WaitingList{

public:
    
    WaitingList()
    {
        lock = IOLockAlloc();
        assert( lock );
    }
    
    ~WaitingList()
    {
        while( ! list.isEmpty() )
        {
            void* key;
            
            if( list.removeFirstEntryAndReturnItsData( &key ) )
                thread_wakeup(key);
        }
        
        IOLockFree(lock);
    }
    
    int enter( __in void* key )
    {
        return list.addDataByKey( key, this );
    }
    
    void wait( __in void* key )
    {
        assert( preemption_enabled() );
        
        bool wait = false;
        
        IOLockLock( lock );
        {// start of the lock
            
            if( list.getDataByKey(key) )
                wait = (THREAD_WAITING == assert_wait( key, THREAD_UNINT ));
            
        }// end of the lock
        IOLockUnlock( lock );
        
        if( wait )
            thread_block( THREAD_CONTINUE_NULL );
    }
    
    void signal( __in void* key )
    {
        IOLockLock( lock );
        {// start of the lock
            
            if( list.getDataByKey(key) )
            {
                thread_wakeup(key);
                list.removeKey(key);
            }
            
        }// end of the lock
        IOLockUnlock( lock );
    }
    
private:
    DataMap    list;
    IOLock*    lock;
};

//--------------------------------------------------------------------

extern WaitingList gFileOpenWaitingList;

//--------------------------------------------------------------------

#endif /* defined(__VFSFilter0__WaitingList__) */
