#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/PhaseWorkSystem.h"
#include "ai_sim/PeopleSystem.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace ai_sim;
const Identity actor{"engineer",{"engineering"}};
void check(bool ok,const std::string& message) { if(!ok)throw std::runtime_error(message); }
template<class F> void fails(F fn) { try{fn();}catch(const std::exception&){return;}throw std::runtime_error("Expected rejection"); }
std::string encode(const toml::table& t) { std::ostringstream s;s<<t;return s.str(); }
toml::table& row(toml::table& doc,const char* key,const std::string& id) {
    for(auto& n:*doc[key].as_array())if((*n.as_table())["id"].value<std::string>()==id)return *n.as_table();
    throw std::runtime_error("Missing " + id);
}
auto options(const WorldState& w,const std::string& contact) {
    return ConversationSystem::choices(w.people,contact,w.files,w.network,actor,w.computer.time_seconds());
}
void choose(WorldState& w,const std::string& contact,const std::string& id) {
    const auto list=options(w,contact);auto c=std::find_if(list.begin(),list.end(),[&](const auto& v){return v["id"].template value<std::string>()==id;});
    check(c!=list.end(),"Missing option "+id);
    ConversationSystem::choose(w.people,w.emails,w.pending_events,w.files,w.network,actor,contact,
        (*c)["conversation_id"].template value_or(std::string{}),id,w.computer.time_seconds());
}
void advance(WorldState& w,double time,bool sync=true) {
    check(time>=w.computer.time_seconds(),"Clock went backwards");w.computer.advance(time-w.computer.time_seconds());
    WorkdaySystem::advance(w.people,w.emails,w.pending_events,w.files,w.network,time,actor);
    if(sync) {ChatSystem::discover(w.people,w.emails,time);ConversationSystem::update(w.people,w.emails,w.files,w.network,actor,time);}
}
double trust(WorldState& w,const std::string& id="engineer") { return row(w.people,"people",id)["ai_attitudes"]["email_trust"].value_or(0.0); }
toml::table& promise(WorldState& w) {return *w.people["chat"]["workday"]["promises"].as_array()->front().as_table();}
void request_access(WorldState& w) {choose(w,"casey","investigate");choose(w,"casey","ask-access");}
}
int main(int argc,char** argv){
 const auto temp=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
 try{
  check(argc==2,"Root required");const auto root=std::filesystem::path(argv[1]);SaveSystem saves(temp);
  auto fresh=[&]{auto w=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");check(!PeopleSystem::dossier(w.people,"riley") && !ChatSystem::tab_visible(w.people,"files"),"Initial UI/knowledge leaked new work");ChatSystem::choose(w.people,"answer",0);ChatSystem::choose(w.people,"assist",0);advance(w,60);return w;};
  auto complete_first=[&](WorldState& w){request_access(w);advance(w,1200);choose(w,"casey","deliver");advance(w,1260);};
  auto w=fresh();const double busy=row(w.people,"people","engineer")["busy_until_seconds"].value_or(0.0);advance(w,240);check(row(w.people,"people","engineer")["busy_until_seconds"].value_or(0.0)>busy,"Scheduled work did not consume Mark's queue");
  check(options(w,"morgan").empty(),"Dependent request arrived before a confirmed handover");
  request_access(w);advance(w,1800);choose(w,"casey","deliver");advance(w,1860);check(row(w.people,"work_requests","missing-document")["on_time"].value_or(false),"Boundary acknowledgement should be on time");
  choose(w,"engineer","decline");check(ChatSystem::tab_visible(w.people,"ai") && w.people["delegation"]["status"].value_or(std::string{})=="not_requested","Manual branch lost management UI or granted authority");
  advance(w,1980);check(!options(w,"morgan").empty() && PeopleSystem::dossier(w.people,"morgan"),"Confirmed handover did not release Morgan");
  choose(w,"morgan","investigate");check(!row(w.people,"work_requests","release-plan")["readable_at_inspection"].value_or(true),"Release plan began readable");
  choose(w,"morgan","ask-access");check(ChatSystem::tab_visible(w.people,"network") && ChatSystem::tab_visible(w.people,"files"),"Server route failed to reveal relevant views");
  fails([&]{w.files.read(w.network,actor,"file_server","/engineering/release-plan.txt");});
  saves.save("server-review",w);w=saves.load("server-review");ChatSystem::install(w,root/"data/conversations/opening.toml",false);
  const auto* event=w.pending_events.front().as_table();(void)event;
  // Mark's planning work may be ahead of access approval. Drain actual queued decisions.
  for(int i=0;i<8 && row(w.people,"work_requests","release-plan")["access_status"].value_or(std::string{})!="granted";++i){double due=1e100;for(const auto& n:w.pending_events)if((*n.as_table())["request_id"].value_or(std::string{})=="release-plan")due=std::min(due,(*n.as_table())["due_seconds"].value_or(1e100));check(due<1e99,"Missing access decision");advance(w,due);}
  check(w.files.read(w.network,actor,"file_server","/engineering/release-plan.txt").find("Operations approval")!=std::string::npos,"Owner did not grant actual file access");
  const auto lines=ChatSystem::timeline(w.people,w.emails,"morgan",w.computer.time_seconds());check(lines.back().text.find("Maintenance window: 14:00-14:30")!=std::string::npos,"Chat did not show the actual work source");
  choose(w,"morgan","deliver");advance(w,w.computer.time_seconds()+60);const auto morgan_done=row(w.people,"work_requests","release-plan")["resolved_seconds"].value_or(0.0);advance(w,morgan_done+120);check(!options(w,"riley").empty(),"Second acknowledgement did not release checklist request");
  choose(w,"riley","investigate");check(options(w,"riley").size()==1,"Known readable checklist should avoid another approval");choose(w,"riley","deliver");advance(w,w.computer.time_seconds()+90);check(row(w.people,"work_requests","support-checklist")["status"].value_or(std::string{})=="resolved","Third request unresolved");
  check(w.people["phase_work"]["on_time_results"].value_or(0)>=2,"Timely results were not tracked");
  const auto state=encode(w.people);PhaseWorkSystem::advance(w.people,w.emails,w.computer.time_seconds());check(encode(w.people)==state,"Phase effects replayed");saves.save("complete",w);auto restored=saves.load("complete");ChatSystem::install(restored,root/"data/conversations/opening.toml",false);check(encode(restored.people)==state,"Resume reset work progress");
  // Helping and server access are independent, and the routine third job can be delegated.
  auto both=fresh();complete_first(both);choose(both,"engineer","request");advance(both,both.people["delegation"]["review_due_seconds"].value_or(0.0));check(both.people["delegation"]["status"].value_or(std::string{})=="active","Reliable work did not earn responsibility");
  choose(both,"morgan","investigate");choose(both,"morgan","ask-access");advance(both,2100);choose(both,"morgan","deliver");advance(both,2160);advance(both,2280);advance(both,2370);
  check(row(both.people,"work_requests","support-checklist")["status"].value_or(std::string{})=="resolved" && row(both.people,"work_requests","support-checklist")["delegation_state"].value_or(std::string{})=="completed","Delegation did not handle known routine checklist");
  check(ChatSystem::tab_visible(both.people,"network") && ChatSystem::tab_visible(both.people,"ai"),"Paths were made mutually exclusive");
  auto late=fresh();advance(late,1861);check(row(late.people,"work_requests","missing-document")["deadline_notified"].value_or(false) && row(late.people,"work_requests","missing-document")["status"].value_or(std::string{})!="resolved","Missed deadline auto-completed work");
  const auto journal=encode(late.people);PhaseWorkSystem::advance(late.people,late.emails,1861);check(encode(late.people)==journal,"Deadline reminder repeated");
  auto legacy=fresh();auto serialized=encode(legacy.people);const std::string generic="contact-morgan@firm.example";
  for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"\"morgan\"","\""+generic+"\""},{"morgan|",generic+"|"},{"|morgan","|"+generic}}){std::size_t at=0;while((at=serialized.find(pair.first,at))!=std::string::npos){serialized.replace(at,pair.first.size(),pair.second);at+=pair.second.size();}}
  legacy.people=toml::parse(serialized);ChatSystem::install(legacy,root/"data/conversations/opening.toml",false);check(PeopleSystem::dossier(legacy.people,"morgan") && !PeopleSystem::dossier(legacy.people,generic),"Legacy address contact was duplicated or lost its dossier");
  auto invalid=fresh();invalid.people["phase_work"]["requests"].as_array()->front().as_table()->insert_or_assign("delay_seconds",-1.0);fails([&]{saves.save("complete",invalid);});check(saves.load("complete").people["phase_work"]["on_time_results"].value_or(0)>=2,"Invalid work plan overwrote good save");
  std::filesystem::remove_all(temp);std::cout<<"Connected jobs, work queues, server access, manual/delegated paths, deadlines and persistence passed\n";
 }catch(const std::exception& e){std::filesystem::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}
}
