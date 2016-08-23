//
//  RecursionEngine.h
//  VFSFilter0
//
//  Created by slava on 6/13/15.
//  Copyright (c) 2015 Slava Imameev. All rights reserved.
//

#ifndef __VFSFilter0__RecursionEngine__
#define __VFSFilter0__RecursionEngine__

#include "Common.h"

class DataMap{
    
private:
    
    class Entry{
    public:
        LIST_ENTRY   listEntry;
        void*        key;
        void*        data;
    };
    
private:
    
    LIST_ENTRY  head;
    IOLock*     lock;
    
private:
    
    Entry* findEntryByKey( __in void* key, __in bool acquireLock )
    {
        Entry* foundEntry = NULL;
        
        if( acquireLock )
            IOLockLock( lock );
        { // start ofthe lock
            for( LIST_ENTRY* le = head.Flink; le != &head; le = le->Flink )
            {
                Entry* entry = CONTAINING_RECORD( le, Entry, listEntry );
                if( entry->key == key )
                {
                    foundEntry = entry;
                    break;
                }
            } // end for
        } // end of the lock
        if( acquireLock )
            IOLockUnlock( lock );
        
        return foundEntry;
    }
    
public:
    
    DataMap()
    {
        InitializeListHead( &head );
        lock = IOLockAlloc();
        assert( lock );
        // TO DO , in kernel we can't through an exception if allocation failed
        // redesign with init() function
    };
    
    virtual ~DataMap()
    {
        assert( IsListEmpty( &head ) );
        IOLockFree( lock );
    }
    
    //
    // both key and data can't be NULL,
    // the function checks for duplicate entries
    // and updates the data field
    //
    virtual bool addDataByKey( __in void* key, __in void* data )
    {
        assert( data );
        assert( key );
        
        bool    freeEntry = false;
        Entry*  entry = reinterpret_cast<Entry*>( IOMalloc( sizeof(*entry) ) );
        assert( entry );
        if( ! entry )
            return false;
        
        bzero( entry, sizeof(*entry) );
        
        entry->key = key;
        entry->data = data;
        
        IOLockLock( lock );
        {
            Entry* existingEntry = findEntryByKey( key, false );
            if( existingEntry )
                existingEntry->data = data;
            else
                InsertHeadList( &head, &entry->listEntry );
            
            freeEntry = ( NULL != existingEntry );
        }
        IOLockUnlock( lock );
        
        if( freeEntry )
            IOFree( entry, sizeof( *entry ) );
        
        return true;
    };
    
    virtual void removeKey( __in void* key )
    {
        Entry* entryToRemove = NULL;
        
        IOLockLock( lock );
        {
            entryToRemove = findEntryByKey( key, false );
            if( entryToRemove )
                RemoveEntryList( &entryToRemove->listEntry );
        }
        IOLockUnlock( lock );
        
        if( entryToRemove )
            IOFree( entryToRemove, sizeof(*entryToRemove) );
    };
    
    
    virtual void* removeFirstEntryAndReturnItsData( __out_opt void** key = NULL)
    {
        Entry* entryToRemove = NULL;
        
        IOLockLock( lock );
        {
            if( !IsListEmpty( &head ) ){
                
                entryToRemove = CONTAINING_RECORD( head.Flink, Entry, listEntry );
                RemoveEntryList( &entryToRemove->listEntry );
            }
        }
        IOLockUnlock( lock );
        
        void* data = NULL;
        
        if( entryToRemove ){
         
            data = entryToRemove->data;
            if( key )
                *key = entryToRemove->key;
            
            IOFree( entryToRemove, sizeof(*entryToRemove) );
        }
        
        return data;
    };
    
    //
    // returns NULL if a key is not found, the validite of the returned pointer is a caller's responsibility
    //
    virtual void* getDataByKey( __in void* key )
    {
        void* data = NULL;
        
        IOLockLock( lock );
        {
            Entry* entry = findEntryByKey( key, false );
            if( entry )
                data = entry->data;
        }
        IOLockUnlock( lock );
        
        return data;
    }
    
    virtual bool isEmpty()
    {
        return IsListEmpty( &head );
    }
    
};

class RecursionEngine: public DataMap{
    
public:
    static RecursionEngine   Instance;
   
public:
    
    static void* CookieForRecursiveCall() { return RecursionEngine::Instance.getDataByKey( current_thread() ); }
    
    static bool IsRecursiveCall( __in void* cookie ) { return NULL != RecursionEngine::Instance.getDataByKey( current_thread() ); }
    static void EnterRecursiveCall( __in void* cookie ) {  RecursionEngine::Instance.addDataByKey( current_thread(), cookie ); }
    static void LeaveRecursiveCall( __in void* cookie ) {  RecursionEngine::Instance.removeKey( current_thread() ); }

    static bool IsRecursiveCall() { return IsRecursiveCall( current_thread() ); }
    static void EnterRecursiveCall() { EnterRecursiveCall( current_thread() ); }
    static void LeaveRecursiveCall() {  LeaveRecursiveCall( current_thread() ); }

};

#endif /* defined(__VFSFilter0__RecursionEngine__) */
