/*

	Copyright (C) 2007 Gregory Smith
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

#include "Update.h"
#include "preferences.h"
#include "csalerts.h"
#include "../Misc/SprintathonBuildVersion.h"
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#ifdef HAVE_CURL
#ifdef __WIN32__
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <curl/curl.h>
#endif

namespace {
const char* const api="https://api.github.com/repos/visageeee/alephone-sprintathon/";
const char* const releases="https://github.com/visageeee/alephone-sprintathon/releases";
std::string buildKey() { return std::string(SPRINTATHON_BUILD_COMMIT)+":"+SPRINTATHON_BUILD_TAG; }
std::string encode(const std::string& value) {
    const char* hex="0123456789ABCDEF";std::string out;
    for(unsigned char c:value) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')out+=char(c);
        else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
    }
    return out;
}
bool validTag(const std::string& tag) {
    if(tag.empty() || tag.size()>128)return false;
    for(unsigned char c:tag)if(c<33 || c>126)return false;
    return true;
}
boost::property_tree::ptree parse(const std::string& text) {
    std::istringstream stream(text);boost::property_tree::ptree result;
    boost::property_tree::read_json(stream,result);return result;
}
#ifdef HAVE_CURL
struct Download {std::string body;std::atomic<bool>* cancel;};
size_t receive(char* data,size_t size,size_t count,void* user) {
    auto& download=*static_cast<Download*>(user);
    const size_t limit=2*1024*1024;
    if(size && count>(limit-download.body.size())/size)return 0;
    const size_t bytes=size*count;
    try {download.body.append(data,bytes);}catch(...){return 0;}
    return bytes;
}
int progress(void* user,curl_off_t,curl_off_t,curl_off_t,curl_off_t) {
    return static_cast<Download*>(user)->cancel->load()?1:0;
}
long get(const std::string& url,std::string& body,std::atomic<bool>& cancel) {
    if(cancel.load())return 0;
    CURL* curl=curl_easy_init();if(!curl)return 0;
    Download download{{},&cancel};
    curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
    curl_easy_setopt(curl,CURLOPT_USERAGENT,"Sprintathon-update-check");
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYPEER,1L);
    curl_easy_setopt(curl,CURLOPT_SSL_VERIFYHOST,2L);
    curl_easy_setopt(curl,CURLOPT_NOSIGNAL,1L);
    curl_easy_setopt(curl,CURLOPT_CONNECTTIMEOUT,3L);
    curl_easy_setopt(curl,CURLOPT_TIMEOUT,10L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,receive);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&download);
    curl_easy_setopt(curl,CURLOPT_NOPROGRESS,0L);
    curl_easy_setopt(curl,CURLOPT_XFERINFOFUNCTION,progress);
    curl_easy_setopt(curl,CURLOPT_XFERINFODATA,&download);
    const CURLcode result=curl_easy_perform(curl);
    long code=0;if(result==CURLE_OK)curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&code);
    curl_easy_cleanup(curl);body.swap(download.body);return code;
}
#endif
}

Update::Update() {
    const auto& p=*network_preferences;
    if(p.sprintathon_update_build==buildKey() && p.sprintathon_update_status>=UpdateCheckFailed &&
       p.sprintathon_update_status<=NoPublishedRelease &&
       (p.sprintathon_update_status!=UpdateAvailable || validTag(p.sprintathon_update_tag))) {
        m_tag=p.sprintathon_update_tag;
        m_status=static_cast<Status>(p.sprintathon_update_status);
    }
    m_seen=GetStatus();
}
void Update::StartUpdateCheck() {
    if(GetStatus()==CheckingForUpdate)return;
    if(m_thread){SDL_WaitThread(m_thread,nullptr);m_thread=nullptr;}
    m_cancel=false;m_tag.clear();m_status=CheckingForUpdate;m_pending=true;
    network_preferences->sprintathon_update_last_check=static_cast<uint32>(std::time(nullptr));
    network_preferences->sprintathon_update_build=buildKey();
    network_preferences->sprintathon_update_status=NotChecked;
    network_preferences->sprintathon_update_tag.clear();
    write_preferences();
    // Unstamped source builds must not guess their version or recommend downgrades.
    if(!SPRINTATHON_BUILD_COMMIT[0]){m_status=CannotCompare;return;}
    m_thread=SDL_CreateThread(update_thread,"Sprintathon update check",this);
    if(!m_thread)m_status=UpdateCheckFailed;
}
bool Update::Poll() {
    const auto now=std::time(nullptr);
    const auto last=network_preferences->sprintathon_update_last_check;
    if(network_preferences->check_for_updates && GetStatus()!=CheckingForUpdate &&
       (network_preferences->sprintathon_update_build!=buildKey() || !last || now<last || now-last>=86400))
        StartUpdateCheck();
    const Status status=GetStatus();
    if(m_pending && status!=CheckingForUpdate) {
        if(m_thread){SDL_WaitThread(m_thread,nullptr);m_thread=nullptr;}
        network_preferences->sprintathon_update_status=status;
        network_preferences->sprintathon_update_tag=m_tag;
        write_preferences();m_pending=false;
    }
    const bool changed=status!=m_seen;m_seen=status;return changed;
}
void Update::Shutdown() {
    m_cancel=true;
    if(m_thread){SDL_WaitThread(m_thread,nullptr);m_thread=nullptr;}
}
int Update::update_thread(void* user) {
    auto* self=static_cast<Update*>(user);
    try {return self->Thread();}catch(...){self->m_status=UpdateCheckFailed;return 1;}
}
int Update::Thread() {
#ifdef HAVE_CURL
    std::string body;
    long code=get(std::string(api)+"releases/latest",body,m_cancel);
    if(code==404){m_status=NoPublishedRelease;return 0;}
    if(code!=200){m_status=UpdateCheckFailed;return 1;}
    const auto release=parse(body);
    if(release.get<bool>("draft") || release.get<bool>("prerelease"))throw std::runtime_error("Unpublished release");
    const std::string tag=release.get<std::string>("tag_name");
    if(!validTag(tag))throw std::runtime_error("Invalid release tag");
    if(tag==SPRINTATHON_BUILD_TAG){m_status=NoUpdateAvailable;return 0;}
    // Compare ancestry, not tag spelling or release dates. An older release
    // published after this build must never be advertised as an upgrade.
    code=get(std::string(api)+"compare/"+encode(SPRINTATHON_BUILD_COMMIT)+"..."+encode(tag)+"?per_page=1",body,m_cancel);
    if(code==404){m_status=CannotCompare;return 0;}
    if(code!=200){m_status=UpdateCheckFailed;return 1;}
    const std::string comparison=parse(body).get<std::string>("status");
    if(comparison=="ahead"){m_tag=tag;m_status=UpdateAvailable;}
    else if(comparison=="behind" || comparison=="identical")m_status=NoUpdateAvailable;
    else if(comparison=="diverged")m_status=CannotCompare;
    else throw std::runtime_error("Invalid comparison");
#else
    m_status=UpdateCheckFailed;
#endif
    return 0;
}
const char* Update::MenuLabel() const {
    switch(GetStatus()) {
    case CheckingForUpdate:return "Checking for Sprintathon updates...";
    case UpdateAvailable:return "New Sprintathon version - View release";
    case NoUpdateAvailable:return "Up to date - Check again";
    case UpdateCheckFailed:return "Update check failed - Retry";
    case CannotCompare:return "Cannot compare this build - View releases";
    case NoPublishedRelease:return "No published release - Check again";
    default:return "Check for updates";
    }
}
void Update::MenuAction() {
    const Status status=GetStatus();
    if(status==UpdateAvailable)launch_url_in_browser((std::string(releases)+"/tag/"+encode(m_tag)).c_str());
    else if(status==CannotCompare)launch_url_in_browser(releases);
    else StartUpdateCheck();
}
