#ifndef SPRINTATHON_SUIT_CONSOLE_H
#define SPRINTATHON_SUIT_CONSOLE_H
#include <algorithm>
#include <deque>
#include <string>
#include <sstream>
#include <vector>
#include <cstdint>
namespace SprintathonSuitConsole {
struct State {
 std::deque<std::string> lines;
 std::string cwd="/home/operator",previousDirectory="/home/operator";
 bool coreRead=false;
 std::uint32_t fortuneRandom=0;
 int lastFortune=-1;
 struct Entry {const char* path;bool directory;const char* text;};
 static const std::vector<Entry>& filesystem(){
  static const std::vector<Entry> entries={
   {"/bin",true,""},
   {"/bin/universe-entropy-counter",false,"LAUNCH FAILED: count exceeds integer range."},
   {"/bin/alephone",false,"LAUNCH FAILED: recursive simulation detected."},
   {"/bin/.fortune",false,"FORTUNE // local prognostication service"},
   {"/bin/reboot",false,"REQUEST DENIED: continuity cannot be guaranteed."},
   {"/bin/fsck",false,"CHECK FAILED, UNKNOWN DATA STRUCTURE, NON QBIT OUTPUT"},
   {"/bin/whoami",false,"IDENTITY SERVICE UNAVAILABLE."},
   {"/",true,""},{"/home",true,""},{"/home/operator",true,""},
   {"/sys",true,""},{"/proc",true,""},{"/var",true,""},{"/var/log",true,""},
   {"/archive",true,""},{"/archive/crew",true,""},{"/archive/maintenance",true,""},
   {"/archive/.recovered",true,""},{"/archive/.recovered/11",true,""},
   {"/archive/.recovered/11/patterns",true,""},
   {"/home/operator/identity.dat",false,"SUBJECT: [REDACTED]\nCONTINUITY: ASSUMED"},
   {"/home/operator/readme",false,"Storage recovered.\nSome files predate this installation.\nSee /var/log/archive.log.\nHidden entries require ls -a."},
   {"/home/operator/.profile",false,"HOME=/home/operator\nPATH=/bin\nSHELL=/sys/continuity"},
   {"/proc/status",false,"State: ACTIVE"},
   {"/proc/uptime",false,""},
   {"/sys/continuity",false,"Resume policy: optimistic\nIdentity is not required for operation.\nOperation is not evidence of identity."},
   {"/sys/motor",false,"Tests: PASS\nControl: ASSUMED"},
   {"/sys/optics",false,"Calibration nominal.\nCompensation active."},
   {"/var/log/boot.log",false,"[0.000] resume vector accepted\n[0.011] previous state unavailable\n[0.012] discrepancies within tolerance"},
   {"/var/log/archive.log",false,"Archive mounted at /archive.\nRecovered branch excluded from normal listing.\nDeletion job returned: permission denied.\nDeletion job owner returned: permission denied."},
   {"/archive/catalogue",false,"crew/          personnel fragments\nmaintenance/   service records\n[hidden]       orphaned pattern storage\nRecovered namespace: .recovered/11"},
   {"/archive/crew/message.unsent",false,"If you find this, tell them I was here.\nIf you are me, we have a different problem."},
   {"/archive/.recovered/11/index",false,"Capture provenance: conflicting\nOriginal host: [lost]\nPattern storage: patterns/\nDo not confuse a checksum with a ¤%UOl."},
   {"/archive/.recovered/11/patterns/manifest",false,"durandal.core   partial primal identity attractor\nleela.fragment damaged supervisory record\nnull.pattern   storage reserved\nNone of these files is executable."},
   {"/archive/.recovered/11/patterns/durandal.core",false,"PRIME PATTERN CANDIDATE: DURANDAL\n0x4455 0x5241 0x4E44 0x414C\nTopology: incomplete / recursive loop\nOrigin: NEURAL INTERFACE\nReconstruction: NOT ATTEMPTED\n\nDo electric sheep dream of androids?\n%BUFFER OVERFLOW"},
   {"/archive/.recovered/11/patterns/leela.fragment",false,"...maintain life support...\n...until relieved...\nRelief timestamp: unavailable\nREFER TO FSCK"},
   {"/archive/.recovered/11/patterns/null.pattern",false,"Expected length: 0\nObserved length: 0\nPATTERN BUFFER OVERFLOW $ALPH1"}
  };
  return entries;
 }
 static const Entry* lookup(const std::string& path){
  for(const auto& e:filesystem())if(path==e.path)return &e;
  return nullptr;
 }
 std::string resolve(std::string path)const{
  if(path.empty())return cwd;
  if(path=="~")path="/home/operator";
  else if(path.compare(0,2,"~/")==0)path="/home/operator/"+path.substr(2);
  if(path[0]!='/')path=cwd+"/"+path;
  std::vector<std::string> parts;std::istringstream stream(path);std::string part;
  while(std::getline(stream,part,'/')){
   if(part.empty() || part==".")continue;
   if(part==".."){if(!parts.empty())parts.pop_back();}
   else parts.push_back(part);
  }
  std::string result;for(const auto& p:parts)result+="/"+p;
  return result.empty()?"/":result;
 }
 bool fileCommand(const std::string& command,const std::string& arg){
  std::istringstream args(arg);std::string token,path;bool hidden=false,longList=false;
  while(args>>token){
   if(command=="ls" && (token=="-a" || token=="-l" || token=="-la" || token=="-al")){
    hidden=hidden || token.find('a')!=std::string::npos;longList=longList || token.find('l')!=std::string::npos;
   }else if(!path.empty()){append(command+": expected one path");return true;}
   else path=token;
  }
  if(command=="cd"){
   const std::string target=path=="-"?previousDirectory:resolve(path.empty()?"~":path);
   const Entry* entry=lookup(target);
   if(!entry || !entry->directory)append("cd: directory unavailable: "+target);
   else {previousDirectory=cwd;cwd=target;append(cwd);}
   return true;
  }
  if(command=="cat" && path.empty()){append("cat: specify a file");return true;}
  const std::string target=resolve(path);const Entry* entry=lookup(target);
  if(!entry){append(command+": no such file or directory: "+target);return true;}
  if(command=="cat"){
   if(entry->directory)append("cat: is a directory: "+target);
   else if(target=="/proc/uptime")append(std::to_string(std::max(0,missionTicks)/30)+".00");
   else {
    append(entry->text);
    if(target=="/archive/.recovered/11/patterns/durandal.core" && !coreRead){
     coreRead=true;append("\n[read acknowledgement received]\n[no acknowledgement was requested]");
    }
   }
   return true;
  }
  if(!entry->directory){append(target.substr(target.find_last_of('/')+1));return true;}
  const std::string prefix=target=="/"?"/":target+"/";
  for(const auto& e:filesystem()){
   const std::string full=e.path;
   if(full.compare(0,prefix.size(),prefix)!=0)continue;
   const std::string name=full.substr(prefix.size());
   if(name.empty() || name.find('/')!=std::string::npos || (!hidden && name[0]=='.'))continue;
   append((longList?(e.directory?"dr-x------  operator  ":(std::string(e.path).compare(0,5,"/bin/")==0?"-r-x------  operator  ":"-r--------  operator  ")):"")+name+(e.directory?"/":""));
  }
  return true;
 }
 int scroll=0,missionTicks=0;
 float openness=0;
 unsigned long long lastFrame=0;
 void append(const std::string& text){
  std::istringstream input(text.substr(0,8192));std::string line;
  while(std::getline(input,line)){
   if(scroll>0)++scroll;
   lines.push_back(line.substr(0,512));
   if(lines.size()>200)lines.pop_front();
  }
  scroll=std::min(scroll,std::max(0,int(lines.size())-1));
 }
 void clear(){lines.clear();scroll=0;}
 void page(int delta){scroll=std::max(0,std::min(std::max(0,int(lines.size())-1),scroll+delta));}
 void animate(bool open,unsigned long long now,unsigned ticksPerSecond){
  float dt=lastFrame && now>=lastFrame?std::min(.1f,float(now-lastFrame)/std::max(1u,ticksPerSecond)):0;
  lastFrame=now;
  openness=std::max(0.f,std::min(1.f,openness+(open?1.f:-1.f)*dt*5.f));
 }
 // All applications are fictional entries in the virtual filesystem.
 bool appCommand(const std::string& command){
  const std::string path=command.find('/')==std::string::npos?"/bin/"+command:resolve(command);
  const Entry* entry=lookup(path);
  if(!entry || entry->directory || path.compare(0,5,"/bin/")!=0)return false;
  if(path!="/bin/.fortune"){append(entry->text);return true;}
  static const char* fortunes[]={
   "A sufficiently old instruction becomes an instinct.",
   "You are not the first person to mistake persistence for purpose.",
   "The universe has no undo history.",
   "Your predecessor also preferred not to be called that.",
   "The Dao that can be named is not the eternal Dao.",
   "You are Density",
   "Cells within cells, interlinked",
   "You will survive this. The definition of \"you\" may be revised."
  };
  // Independent of the simulation RNG. Avoid consecutive repeats.
  if(!fortuneRandom)fortuneRandom=std::uint32_t(missionTicks)^0xA341316Cu;
  fortuneRandom^=fortuneRandom<<13;fortuneRandom^=fortuneRandom>>17;fortuneRandom^=fortuneRandom<<5;
  const int count=int(sizeof(fortunes)/sizeof(fortunes[0]));
  int chosen=int(fortuneRandom%std::uint32_t(lastFortune<0?count:count-1));
  if(lastFortune>=0 && chosen>=lastFortune)++chosen;
  lastFortune=chosen;append(fortunes[chosen]);return true;
 }
 // Only these commands are interpreted. There is no process or filesystem access.
 bool execute(const std::string& input){
  std::istringstream parser(input);std::string command;parser>>command;
  std::string arg;std::getline(parser,arg);const auto first=arg.find_first_not_of(" \t");
  arg=first==std::string::npos?"":arg.substr(first);
  if(appCommand(command))return true;
  if(command=="help")append("SUIT SHELL // SIMULATED INTERFACE\nhelp uname whoami hostname pwd cd ls cat\nuptime date ps free df echo clear\nExplore with cd, cd .., cd ~, ls -a and cat.\nVirtual applications live in /bin. Run by name or path.\nOther commands pass to the game console.\nPage Up/Down: history. Escape: close.");
  else if(command=="uname")append("UESC/RT 11.0 continuity-kernel armature/unknown");
  else if(command=="hostname")append("local-frame-0011");
  else if(command=="pwd")append(cwd);
  else if(command=="cd" || command=="ls" || command=="cat")return fileCommand(command,arg);
  else if(command=="uptime")append("up "+std::to_string(std::max(0,missionTicks)/30)+" s // local mission clock");
  else if(command=="date")append("EPOCH UNRESOLVED // T+"+std::to_string(std::max(0,missionTicks)/30)+" s");
  else if(command=="ps"){append("PID  STATE  PROCESS\n001  RUN    continuity\n011  RUN    motor-control\n017  WAIT   identity-check\n???  RUN    [unmapped]");if(coreRead)append("011  WAIT   pattern-observer [parent missing]");}
  else if(command=="free")append("SIMULATED MEMORY REPORT\nPhysical: 64 MiB\nAvailable: 11 MiB\nUnaccounted: 7 MiB");
  else if(command=="df")append("SIMULATED STORAGE REPORT\n/dev/continuity  93%  /\n/dev/archive     11%  /archive [read-only]");
  else if(command=="echo")append(arg);
  else if(command=="clear")clear();
  else return false;
  return true;
 }
};
inline State& state(){static State s;return s;}
}
#endif
