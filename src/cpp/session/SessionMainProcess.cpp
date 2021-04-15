/*
 * SessionMainProcess.cpp
 *
 * Copyright (C) 2021 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include "SessionMainProcess.hpp" 
#include "modules/SessionAuthoring.hpp"

#include <core/BoostThread.hpp>
#include <core/system/Process.hpp>

#include <session/SessionModuleContext.hpp>

#include <r/RExec.hpp>

namespace rstudio {
namespace session {
namespace main_process {
namespace {

// fork state
boost::thread::id s_mainThreadId;
bool s_wasForked = false;

boost::thread_specific_ptr<bool> t_isMainThread;

} // anonymous namespace

bool wasForked() 
{
   return s_wasForked;
}

// fork handlers (only applicable to Unix platforms)
#ifndef _WIN32

void prepareFork()
{
   // only detect forks from the main thread (since we are going to be
   // calling into non-threadsafe code). this is ok because fork
   // detection is meant to handle forks that don't exec (and thus
   // continue running R code). only the main thread will ever do this
   if (boost::this_thread::get_id() != s_mainThreadId)
      return;

}

void atForkParent()
{
   if (boost::this_thread::get_id() != s_mainThreadId)
      return;
}

void atForkChild()
{
   s_wasForked = true;
}

void setupForkHandlers()
{
   int rc = ::pthread_atfork(prepareFork, atForkParent, atForkChild);
   if (rc != 0)
      LOG_ERROR(systemError(errno, ERROR_LOCATION));
}
#else
void setupForkHandlers()
{

}
#endif

// TODO: asyncRpc - this is just a sanity check on the thread local implementation
// remove it since getting the thread id is a system call 
bool sysIsMainThread()
{
   return boost::this_thread::get_id() == s_mainThreadId;
}

bool isMainThread()
{
   bool res = t_isMainThread.get();
   if (res != sysIsMainThread())
      LOG_ERROR_MESSAGE("isMainThreadCheck - wrong result!");
   return res;
}

void initThreadId()
{
   s_mainThreadId = boost::this_thread::get_id();
   t_isMainThread.reset(new bool{true});

   // Give the R exec library a function it can call to validate the main thread for better diagnostics when
   // R functions are run on the wrong thread
   rstudio::r::exec::initMainThread(isMainThread);
}

bool haveActiveChildren()
{
   return module_context::processSupervisor().hasActiveChildren() ||
          modules::authoring::hasRunningChildren();
}

} // namespace main_process
} // namespace session
} // namespace rstudio


