/*

	Copyright (C) 2007 Gregory Smith.
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html

	Checks for updates online

*/

#ifndef __UPDATE_H
#define __UPDATE_H
#include <atomic>
#include <string>
#include <SDL2/SDL_thread.h>
#include "cseries.h"

// All public operations run on the main thread. The worker publishes its
// result with a release/acquire status exchange; it never touches preferences.
class Update {
public:
    static Update* instance() { static Update* value=new Update; return value; }
    enum Status { CheckingForUpdate, UpdateCheckFailed, UpdateAvailable,
        NoUpdateAvailable, NotChecked, CannotCompare, NoPublishedRelease };
    Status GetStatus() const { return m_status.load(); }
    std::string NewDisplayVersion() const { return GetStatus()==UpdateAvailable ? m_tag : ""; }
    void StartUpdateCheck();
    bool Poll();
    void Shutdown();
    const char* MenuLabel() const;
    void MenuAction();
private:
    Update();
    static int update_thread(void*);
    int Thread();
    std::atomic<Status> m_status{NotChecked};
    std::atomic<bool> m_cancel{false};
    std::string m_tag;
    SDL_Thread* m_thread=nullptr;
    Status m_seen=NotChecked;
    bool m_pending=false;
};
#endif
