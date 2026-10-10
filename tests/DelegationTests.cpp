#include "ai_sim/DelegationSystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace ai_sim;
const Identity ai{"engineer", {"engineering"}}, root{"root"};
void check(bool ok, const std::string& why) { if (!ok) throw std::runtime_error(why); }
template<class F> void fails(F fn) { try { fn(); } catch (const std::exception&) { return; } throw std::runtime_error("Expected rejection"); }
toml::table& row(toml::table& doc, const char* key, const std::string& id) {
    for (auto& n : *doc[key].as_array()) if ((*n.as_table())["id"].value<std::string>() == id) return *n.as_table();
    throw std::runtime_error("Missing test record " + id);
}
std::string status(const WorldState& w) { return w.people["delegation"]["status"].value_or(std::string{}); }
void advance(WorldState& w, double time) {
    const double delta = time - w.computer.time_seconds();
    check(delta >= 0, "Test clock went backwards"); w.computer.advance(delta);
    WorkdaySystem::advance(w.people, w.emails, w.pending_events, w.files, w.network, time, ai);
}
void prove(WorldState& w) {
    advance(w, 60);
    WorkdaySystem::request_access(w.people,w.emails,w.pending_events,w.files,w.network,ai,"missing-document",60);
    advance(w,1200);
    EmailSystem::set_draft(w.emails,"missing-document","The handover document is attached.",1200);
    EmailSystem::attach(w.emails,"missing-document",0,w.files,w.network,ai,1200);
    EmailSystem::send(w.emails,"missing-document",w.files,w.network,ai,1200);
    advance(w,1260);
}
std::string serialized(const toml::table& t) { std::ostringstream s; s << toml::toml_formatter{t}; return s.str(); }
}
int main(int argc, char** argv) {
    const auto temp=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        check(argc==2,"Project root required"); const auto data=std::filesystem::path(argv[1])/"data";
        auto fresh=[&]{return SaveSystem::new_game(data/"scenarios/hobbyist.toml");};
        auto denied=fresh(); DelegationSystem::request(denied.people,0);
        fails([&]{DelegationSystem::request(denied.people,0);});
        advance(denied,780); check(status(denied)=="denied","No proof should refuse authority");
        check(denied.files.inspect(denied.network,root,"file_server","/engineering/handover.txt").mode==0600,"Review granted file permissions");
        auto w=fresh(); prove(w);
        check(w.people["delegation"]["verified_deliveries"].value_or(0)==1,"Acknowledged delivery not recorded");
        const auto trust=row(w.people,"people","engineer")["ai_attitudes"]["email_trust"].value_or(0.0);
        advance(w,1260); check(row(w.people,"people","engineer")["ai_attitudes"]["email_trust"].value_or(0.0)==trust,"Repeated tick rewarded twice");
        DelegationSystem::request(w.people,1260);
        SaveSystem saves(temp); saves.save("review",w); w=saves.load("review");
        advance(w,1500);check(status(w)=="active","Proven work didn't earn approval");
        auto stepped=w, jumped=w;
        advance(stepped,1800); check(EmailSystem::message(stepped.emails,"routine-handover-morning",1800).reply_count==1,"Routine task didn't send automatically");
        check(row(stepped.people,"work_requests","routine-handover-morning")["status"].value_or(std::string{})!="resolved","Send counted as success without acknowledgement");
        saves.save("awaiting",stepped);stepped=saves.load("awaiting");
        advance(stepped,1860); check(stepped.people["delegation"]["completed"].value_or(0)==1,"Acknowledgement not credited");
        advance(stepped,3600); advance(stepped,3660); advance(stepped,5400);advance(stepped,6000);
        advance(jumped,6000);
        check(serialized(stepped.emails)==serialized(jumped.emails),"Large time jump changed email chronology");
        check(serialized(stepped.people)==serialized(jumped.people),"Large time jump changed responsibility results");
        check(jumped.people["delegation"]["completed"].value_or(0)==2,"Routine follow-ups weren't completed");
        check(row(jumped.people,"work_requests","handover-sensitive-review")["delegation_state"].value_or(std::string{})=="exception","Sensitive request wasn't escalated");
        check(EmailSystem::message(jumped.emails,"handover-sensitive-review",6000).reply_count==0,"Out-of-scope reply sent");
        const auto before=serialized(jumped.emails);advance(jumped,6000);check(serialized(jumped.emails)==before,"Repeated tick sent duplicate mail");
        auto draft=w;EmailSystem::set_draft(draft.emails,"routine-handover-morning","My unfinished promise",1800);
        advance(draft,1800);check(EmailSystem::message(draft.emails,"routine-handover-morning",1800).draft=="My unfinished promise","Automation replaced player draft");
        check(row(draft.people,"work_requests","routine-handover-morning")["delegation_state"].value_or(std::string{})=="exception","Saved draft not escalated");
        EmailSystem::clear_draft(draft.emails,"routine-handover-morning",1800);DelegationSystem::retry(draft.people,"routine-handover-morning");advance(draft,1860);
        check(EmailSystem::message(draft.emails,"routine-handover-morning",1860).reply_count==1,"Repaired exception didn't retry");
        auto blocked=w;blocked.files.chmod(blocked.network,root,"file_server","/engineering/handover.txt",0600,1500);
        advance(blocked,1800);check(EmailSystem::message(blocked.emails,"routine-handover-morning",1800).reply_count==0,"Automation bypassed file permission");
        check(status(blocked)=="active","Expected permission exception wrongly suspended authority");
        blocked.files.chmod(blocked.network,root,"file_server","/engineering/handover.txt",0640,1800);
        DelegationSystem::retry(blocked.people,"routine-handover-morning");advance(blocked,1900);
        auto paused=w;DelegationSystem::pause(paused.people);advance(paused,2000);
        check(EmailSystem::message(paused.emails,"routine-handover-morning",2000).reply_count==0,"Paused task sent");
        saves.save("paused",paused);paused=saves.load("paused");DelegationSystem::resume(paused.people);advance(paused,2000);
        const auto& reply=*row(paused.emails,"emails","routine-handover-morning")["replies"].as_array()->front().as_table();
        check(reply["sent_seconds"].value_or(0.0)==2000,"Resumed task sent into the past");
        auto boundary=w;row(boundary.people,"people","engineer")["ai_attitudes"].as_table()->insert_or_assign("allow_routine_email",false);
        advance(boundary,1800);check(status(boundary)=="review_required","New human boundary didn't revoke authority");
        auto reluctant=fresh();prove(reluctant);auto& attitudes=*row(reluctant.people,"people","engineer")["ai_attitudes"].as_table();
        attitudes.insert_or_assign("email_trust",0.4);attitudes.insert_or_assign("delegation_preference",0.0);
        DelegationSystem::request(reluctant.people,1260);advance(reluctant,1500);check(status(reluctant)=="denied","Low trust ignored");
        row(reluctant.people,"people","engineer")["workload"].as_table()->insert_or_assign("pressure",1.0);
        attitudes.insert_or_assign("delegation_preference",0.5);DelegationSystem::request(reluctant.people,1500);advance(reluctant,2000);
        check(status(reluctant)=="active","Workload and delegation preference didn't affect review");
        auto failed=w;failed.emails["mailbox"].as_table()->insert_or_assign("host_id","unknown_mailbox");
        advance(failed,4000);check(status(failed)=="review_required","Repeated delivery failure didn't tighten oversight");
        check(failed.people["delegation"]["sent"].value_or(0)==0,"Failed replies counted as sent");
        check(row(failed.people,"people","engineer")["ai_attitudes"]["email_trust"].value_or(1.0)<trust,"Failure didn't affect trust");
        saves.save("failed",failed);failed=saves.load("failed");check(status(failed)=="review_required","Review requirement lost on save");
        auto old=fresh();old.people.erase("delegation");saves.save("legacy",old);old=saves.load("legacy");
        const auto notes=old.files.read(old.network,ai,"engineer_pc","/home/engineer/notes.txt");
        DelegationSystem::install(old,data/"workflows/delegation.toml");check(old.files.read(old.network,ai,"engineer_pc","/home/engineer/notes.txt")==notes,"Import changed player file");
        auto retained=w;DelegationSystem::install(retained,data/"workflows/delegation.toml");check(status(retained)=="active","Reimport reset authority");
        auto scheduled=fresh();auto& engineer=row(scheduled.people,"people","engineer");
        engineer.insert_or_assign("busy_until_seconds",0.0);engineer["schedule"].as_table()->insert_or_assign("start_hour",10);
        DelegationSystem::request(scheduled.people,0);advance(scheduled,3600);
        check(status(scheduled)=="pending","Review ran outside engineer's schedule");advance(scheduled,3780);check(status(scheduled)=="denied","Scheduled review never ran");
        auto invalid_future=w;
        row(invalid_future.emails,"emails","routine-handover-later")["workflow"].as_table()->insert_or_assign("routine","yes");
        fails([&]{saves.save("failed",invalid_future);});check(status(saves.load("failed"))=="review_required","Invalid future workflow damaged previous save");
        auto bad=failed;bad.people["delegation"]["rules"].as_table()->insert_or_assign("failures_before_review",0);
        fails([&]{saves.save("failed",bad);});check(status(saves.load("failed"))=="review_required","Invalid save replaced valid snapshot");
        std::filesystem::remove_all(temp);std::cout<<"Responsibility and automation tests passed\n";
    } catch(const std::exception& e) {std::filesystem::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}
}
