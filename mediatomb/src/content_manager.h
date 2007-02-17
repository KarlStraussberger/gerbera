/*MT*
    
    MediaTomb - http://www.mediatomb.cc/
    
    content_manager.h - this file is part of MediaTomb.
    
    Copyright (C) 2005 Gena Batyan <bgeradz@mediatomb.cc>,
                       Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>
    
    Copyright (C) 2006-2007 Gena Batyan <bgeradz@mediatomb.cc>,
                            Sergey 'Jin' Bostandzhyan <jin@mediatomb.cc>,
                            Leonhard Wimmer <leo@mediatomb.cc>
    
    MediaTomb is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License version 2
    as published by the Free Software Foundation.
    
    MediaTomb is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    version 2 along with MediaTomb; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
    
    $Id$
*/

/// \file content_manager.h
#ifndef __CONTENT_MANAGER_H__
#define __CONTENT_MANAGER_H__

#include "common.h"
#include "cds_objects.h"
#include "storage.h"
#include "dictionary.h"
#include "sync.h"
#include "autoscan.h"
#include "timer.h"

#ifdef HAVE_JS
#include "scripting/scripting.h"
#endif

class ContentManager;

typedef enum task_type_t
{
    Invalid,
    AddFile,
    RemoveObject,
    LoadAccounting,
    RescanDirectory
};

class CMTask : public zmm::Object
{
protected:
    zmm::String description;
    task_type_t taskType;
    unsigned int parentTaskID;
    unsigned int taskID;
    bool valid;
    bool cancellable;
    
public:
    CMTask();
    virtual void run(zmm::Ref<ContentManager> cm) = 0;
    inline void setDescription(zmm::String description) { this->description = description; };
    inline zmm::String getDescription() { return description; };
    inline task_type_t getType() { return taskType; };
    inline unsigned int getID() { return taskID; };
    inline unsigned int getParentID() { return parentTaskID; };
    inline void setID(unsigned int taskID) { this->taskID = taskID; };
    inline void setParentID(unsigned int parentTaskID = 0) { this->parentTaskID = parentTaskID; };
    inline bool isValid() { return valid; };
    inline bool isCancellable() { return cancellable; };
    inline void invalidate() { valid = false; };
};

class CMAddFileTask : public CMTask
{
protected:
    zmm::String path;
    bool recursive;
    bool hidden;
public:
    CMAddFileTask(zmm::String path, bool recursive=false, bool hidden=false);
    zmm::String getPath();
    virtual void run(zmm::Ref<ContentManager> cm);
};

class CMRemoveObjectTask : public CMTask
{
protected:
    int objectID;
    bool all;
public:
    CMRemoveObjectTask(int objectID, bool all);
    virtual void run(zmm::Ref<ContentManager> cm);
};

class CMLoadAccountingTask : public CMTask
{
public:
    CMLoadAccountingTask();
    virtual void run(zmm::Ref<ContentManager> cm);
};

class CMRescanDirectoryTask : public CMTask
{
protected: 
    int objectID;
    int scanID;
    scan_mode_t scanMode;
public:
    CMRescanDirectoryTask(int objectID, int scanID, scan_mode_t scanMode);
    virtual void run(zmm::Ref<ContentManager> cm);
};

class CMAccounting : public zmm::Object
{
public:
    CMAccounting();
public:
    int totalFiles;
};

/*
class DirCacheEntry : public zmm::Object
{
public:
    DirCacheEntry();
public:
    int end;
    int id;
};

class DirCache : public zmm::Object
{
protected:
    zmm::Ref<zmm::StringBuffer> buffer;
    int size; // number of entries
    int capacity; // capacity of entries
    zmm::Ref<zmm::Array<DirCacheEntry> > entries;
public:
    DirCache();
    void push(zmm::String name);
    void pop();
    void setPath(zmm::String path);
    void clear();
    zmm::String getPath();
    int createContainers();
};
*/

class ContentManager : public TimerSubscriberSingleton<ContentManager>
{
public:
    ContentManager();
    virtual void init();
    virtual ~ContentManager();
    void shutdown();
    
    virtual void timerNotify(int id);
    
    bool isBusy() { return working; }
    
    zmm::Ref<CMAccounting> getAccounting();

    /// \brief Returns the task that is currently being executed.
    zmm::Ref<CMTask> getCurrentTask();

    /// \brief Returns the list of all enqueued tasks, including the current or nil if no tasks are present.
    zmm::Ref<zmm::Array<CMTask> > getTasklist();

    /// \brief Find a task identified by the task ID and invalidate it.
    void invalidateTask(unsigned int taskID);

    
    /* the functions below return true if the task has been enqueued */
    
    /* sync/async methods */
    void loadAccounting(bool async=true);
    void addFile(zmm::String path, bool recursive=true, bool async=true, bool hidden=false, bool lowPriority=false);
    int ensurePathExistence(zmm::String path);
    void removeObject(int objectID, bool async=true, bool all=false);
//    void rescanDirectory(int objectID, scan_level_t scanLevel = BasicScan);   
    
    /// \brief Updates an object in the database using the given parameters.
    /// \param objectID ID of the object to update
    /// \param parameters key value pairs of fields to be updated
    void updateObject(int objectID, zmm::Ref<Dictionary> parameters);

    zmm::Ref<CdsObject> createObjectFromFile(zmm::String path, bool magic=true);

    /// \brief Adds a virtual item.
    /// \param obj item to add
    ///
    /// This function makes sure that the file is first added to
    /// PC-Directory, however without the scripting execution.
    /// It then adds the user defined virtual item to the database.
    void addVirtualItem(zmm::Ref<CdsObject> obj);

    /// \brief Adds an object to the database.
    /// \param obj object to add
    ///
    /// parentID of the object must be set before this method.
    /// The ID of the object provided is ignored and generated by this method    
    void addObject(zmm::Ref<CdsObject> obj);

    /// \brief Adds a virtual container chain specified by path.
    /// \param container path separated by '/'. Slashes in container
    /// titles must be escaped.
    /// \param lastClass upnp:class of the last container in the chain
    /// \return ID of the last container in the chain.
    int addContainerChain(zmm::String chain, zmm::String lastClass);
    
    /// \brief Adds a virtual container specified by parentID and title
    /// \param parentID the id of the parent.
    /// \param title the title of the container.
    /// \param upnpClass the upnp class of the container.
    void addContainer(int parentID, zmm::String title, zmm::String upnpClass);
    
    /// \brief Updates an object in the database.
    /// \param obj the object to update
    void updateObject(zmm::Ref<CdsObject> obj);

    /// \brief Updates an object in the database using the given parameters.
    /// \param objectID ID of the object to update
    ///
    /// Note: no actions should be performed on the object given as the parameter.
    /// Only the returned object should be processed. This method does not save
    /// the returned object in the database. To do so updateObject must be called
    zmm::Ref<CdsObject> convertObject(zmm::Ref<CdsObject> obj, int objectType);

    /// \brief Gets an AutocsanDirectrory from the watch list.
    zmm::Ref<AutoscanDirectory> getAutoscanDirectory(int scanID, scan_mode_t scanMode);

    /// \brief Get an AutoscanDirectory given by location on disk from the watch list.
    zmm::Ref<AutoscanDirectory> getAutoscanDirectory(zmm::String location);
    /// \brief Removes an AutoscanDirectrory (found by scanID) from the watch list.
    void removeAutoscanDirectory(int scanID, scan_mode_t scanMode);

    /// \brief Removes an AutoscanDirectrory (found by location) from the watch list.
    void removeAutoscanDirectory(zmm::String location);
    
    /// \brief Removes an AutoscanDirectory (by objectID) from the watch list.
    void removeAutoscanDirectory(int objectID);
 
    /// \brief Update autoscan parameters for an existing autoscan directory 
    /// or add a new autoscan directory
    void setAutoscanDirectory(zmm::Ref<AutoscanDirectory> dir);


    /// \brief instructs ContentManager to reload scripting environment
#ifdef HAVE_JS
    void reloadScripting();
#endif
    
protected:
#ifdef HAVE_JS
    void initScripting();
    void destroyScripting();
#endif
    //pthread_mutex_t last_modified_mutex;
    
    int ignore_unknown_extensions;
    zmm::Ref<Dictionary> extension_mimetype_map;
    zmm::Ref<Dictionary> mimetype_upnpclass_map;
    zmm::Ref<AutoscanList> autoscan_timed;
    
    /* don't use these, use the above methods */
    void _loadAccounting();

    void addFileInternal(zmm::String path, bool recursive=true, bool async=true, bool hidden=false, bool lowPriority=false, 
                         unsigned int parentTaskID = 0);
    void _addFile(zmm::String path, bool recursive=false, bool hidden=false, zmm::Ref<CMTask> task=nil);
    //void _addFile2(zmm::String path, bool recursive=0);
    void _removeObject(int objectID, bool all);
    
    
    void rescanDirectory(int objectID, int scanID, scan_mode_t scanMode, zmm::String descPath = nil); 
    void _rescanDirectory(int containerID, int scanID, scan_mode_t scanMode, scan_level_t scanLevel, zmm::Ref<CMTask> task=nil);
    /* for recursive addition */
    void addRecursive(zmm::String path, bool hidden=false, zmm::Ref<CMTask> task=nil);
    //void addRecursive2(zmm::Ref<DirCache> dirCache, zmm::String filename, bool recursive);
    
    zmm::String extension2mimetype(zmm::String extension);
    zmm::String mimetype2upnpclass(zmm::String mimeType);

    void invalidateAddTask(zmm::Ref<CMTask> t, zmm::String path);
    
#ifdef HAVE_JS  
    zmm::Ref<Scripting> scripting;
#endif
    
    void setLastModifiedTime(time_t lm);
    
    inline void signal() { cond->signal(); }
    static void *staticThreadProc(void *arg);
    void threadProc();
    
    void addTask(zmm::Ref<CMTask> task, bool lowPriority = false);
    
    zmm::Ref<CMAccounting> acct;
    
    pthread_t taskThread;
    zmm::Ref<Cond> cond;
    
    bool working;
    
    bool shutdownFlag;
    
    zmm::Ref<zmm::ObjectQueue<CMTask> > taskQueue1; // priority 1
    zmm::Ref<zmm::ObjectQueue<CMTask> > taskQueue2; // priority 2
    zmm::Ref<CMTask> currentTask;

    unsigned int taskID;
    
    friend void CMAddFileTask::run(zmm::Ref<ContentManager> cm);
    friend void CMRemoveObjectTask::run(zmm::Ref<ContentManager> cm);
    friend void CMRescanDirectoryTask::run(zmm::Ref<ContentManager> cm);
    friend void CMLoadAccountingTask::run(zmm::Ref<ContentManager> cm);
};

#endif // __CONTENT_MANAGER_H__

