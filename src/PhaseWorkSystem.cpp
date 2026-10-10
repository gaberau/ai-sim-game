#include "ai_sim/PhaseWorkSystem.h"
#include "ai_sim/WorkRequestState.h"
#include "ai_sim/ChatSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <functional>
namespace ai_sim {
namespace {
std::string text(const toml::table& t,const char* k){return t[k].value_or(std::string{});}
void require(bool v,const char* why){if(!v)throw std::runtime_error(std::string{"Phase work: "}+why);}
const toml::table* record(const toml::table& p,const char* list,const std::string& id){if(auto* a=p[list].as_array())for(const auto& n:*a)if(text(*n.as_table(),"id")==id)return n.as_table();return nullptr;}
const toml::table* state(const toml::table& p){return p["phase_work"].as_table();}
double base(const toml::table& p){return std::max(p["phase_work"]["installed_seconds"].value_or(0.0),p["chat"]["connected_seconds"].value_or(0.0));}
void note(toml::table& p,const std::string& contact,const std::string& body,double time){p["chat"]["transcript"].as_array()->push_back(toml::table{{"contact_id",contact},{"speaker",contact},{"speaker_name",PeopleSystem::name(p,contact)},{"text",body},{"seconds",time}});}
}
void PhaseWorkSystem::install(WorldState& world,const std::filesystem::path& path){
    if(!std::filesystem::exists(path))return;
    auto w=world;const auto source=DefinitionLoader::load(path);require(source["schema_version"].value_or(0)==1,"Invalid definition");
    // Earlier saves learned Morgan from an address before a character profile existed.
    const std::string old_id="contact-morgan@firm.example";
    if(record(w.people,"people",old_id) && !record(w.people,"people","morgan")){
        std::function<void(toml::node&)> migrate=[&](toml::node& node){
            if(auto* value=node.as_string()){
                auto str=value->get();if(str==old_id)str="morgan";
                else if(str.starts_with(old_id+"|"))str="morgan"+str.substr(old_id.size());
                else if(str.ends_with("|"+old_id))str=str.substr(0,str.size()-old_id.size())+"morgan";
                *value=str;
            }else if(auto* table=node.as_table()){
                for(auto& [key,v]:*table){(void)key;migrate(v);}
                if(auto* v=table->get(old_id)){table->insert_or_assign("morgan",*v);table->erase(old_id);}
            }else if(auto* list=node.as_array())for(auto& v:*list)migrate(v);
        };
        migrate(w.people);migrate(w.pending_events);
    }
    for(const auto& name:{"morgan.toml","riley.toml"})PeopleSystem::install(w.people,path.parent_path().parent_path()/"people"/name);
    if(!state(w.people)){
        auto config=source;config.insert("installed_seconds",w.computer.time_seconds());config.insert("on_time_results",0);config.insert("late_results",0);
        for(auto key:{"requests","commitments"})for(auto& n:*config[key].as_array())n.as_table()->insert("processed",false);
        w.people.insert("phase_work",std::move(config));
    }
    if(auto* definitions=source["requests"].as_array())for(const auto& n:*definitions)if(!record(*w.people["phase_work"].as_table(),"requests",text(*n.as_table(),"id"))) {
        auto r=*n.as_table();r.insert("processed",false);w.people["phase_work"]["requests"].as_array()->push_back(std::move(r));
    }
    if(auto* refs=source["additional_documents"].as_array())for(const auto& ref:*refs) {
        const auto extra=DefinitionLoader::load(path.parent_path()/ref.value_or(std::string{}));
        for(const auto& n:*extra["entries"].as_array()) {const auto& f=*n.as_table();FileEntry e;e.host_id=text(f,"host_id");e.path=text(f,"path");e.owner=text(f,"owner");e.group=text(f,"group");e.mode=FileSystem::parse_mode(text(f,"mode"));e.content=text(f,"content");e.category="work";w.files.import_file(w.network,e,w.computer.time_seconds());}
    }
    const auto document=DefinitionLoader::load(path.parent_path()/source["document"].value_or(std::string{}));
    require(document["entries"].is_array(),"Missing source document");
    if(auto* entries=document["entries"].as_array())for(const auto& n:*entries){const auto& f=*n.as_table();FileEntry e;e.host_id=text(f,"host_id");e.path=text(f,"path");e.owner=text(f,"owner");e.group=text(f,"group");e.mode=FileSystem::parse_mode(text(f,"mode"));e.content=text(f,"content");e.category="work";w.files.import_file(w.network,e,w.computer.time_seconds());}
    // Add a deadline only to the stock first request; do not reset existing timers.
    const double within=w.people["phase_work"]["first_deadline_seconds"].value_or(1800.0);
    if(auto* messages=w.emails["emails"].as_array())for(auto& n:*messages)if(text(*n.as_table(),"id")=="missing-document")if(auto* workflow=(*n.as_table())["workflow"].as_table())if(!workflow->contains("deadline_within_seconds"))workflow->insert("deadline_within_seconds",within);
    if(auto* tasks=w.people["work_requests"].as_array())for(auto& n:*tasks)if(text(*n.as_table(),"id")=="missing-document" && !n.as_table()->contains("deadline_seconds"))n.as_table()->insert("deadline_seconds",std::max(w.computer.time_seconds(),(*n.as_table())["arrival_seconds"].value_or(0.0))+within);
    validate(w.people);
    auto future=w.emails;toml::array templates;
    for(const auto& n:*w.people["phase_work"]["requests"].as_array()){auto m=*(*n.as_table())["message"].as_table();m.insert_or_assign("arrival_seconds",0.0);templates.push_back(std::move(m));}
    EmailSystem::import_messages(future,toml::table{{"schema_version",1},{"emails",templates}});EmailSystem::validate(future);WorkdaySystem::validate_mail_workflows(w.people,future);
    world=std::move(w);
}
std::optional<double> PhaseWorkSystem::next_due(
    const toml::table& people, double limit, bool include_commitments) {
    std::optional<double> due;
    const auto* plan = state(people);
    if (!plan || !ChatSystem::connected(people)) return due;
    const auto add = [&](double time) {
        if (time <= limit && (!due || time < *due)) due = time;
    };
    if (include_commitments) {
        for (const auto& node : *(*plan)["commitments"].as_array()) {
            const auto& commitment = *node.as_table();
            if (!commitment["processed"].value_or(false))
                add(base(people) + commitment["after_seconds"].value_or(0.0));
        }
    }
    for (const auto& node : *(*plan)["requests"].as_array()) {
        const auto& request = *node.as_table();
        if (request["processed"].value_or(false)) continue;
        const auto* prerequisite = record(people, "work_requests", text(request, "after_request"));
        if (prerequisite && text(*prerequisite, "status") == "resolved") {
            add(std::max(base(people), (*prerequisite)["resolved_seconds"].value_or(base(people)))
                + request["delay_seconds"].value_or(0.0));
        }
    }
    if (const auto* tasks = people["work_requests"].as_array()) {
        for (const auto& node : *tasks) {
            const auto request = WorkRequestState::read(*node.as_table());
            if (const auto next = request.next_deadline_event(limit)) add(*next);
        }
    }
    return due;
}
void PhaseWorkSystem::advance(toml::table& p,toml::table& emails,double time){
    if(!state(p) || !ChatSystem::connected(p))return;
    auto& s=*p["phase_work"].as_table();
    for(auto& n:*s["commitments"].as_array()){auto& c=*n.as_table();const double start=base(p)+c["after_seconds"].value_or(0.0);if(c["processed"].value_or(false) || start>time)continue;
        auto* owner=const_cast<toml::table*>(record(p,"people",text(c,"person")));require(owner,"Missing commitment owner");auto service=*owner;service.insert_or_assign("response_seconds",c["service_seconds"].value_or(60.0));const double finish=PeopleSystem::queue_response(service,start);owner->insert_or_assign("busy_until_seconds",service["busy_until_seconds"].value_or(finish));c.insert_or_assign("processed",true);c.insert("finish_seconds",finish);
    }
    for(auto& n:*s["requests"].as_array()){auto& r=*n.as_table();if(r["processed"].value_or(false))continue;const auto* prerequisite=record(p,"work_requests",text(r,"after_request"));if(!prerequisite || text(*prerequisite,"status")!="resolved")continue;
        const double arrival=std::max(base(p),(*prerequisite)["resolved_seconds"].value_or(base(p)))+r["delay_seconds"].value_or(0.0);if(arrival>time)continue;
        auto message=*r["message"].as_table();message.insert_or_assign("arrival_seconds",arrival);EmailSystem::import_messages(emails,toml::table{{"schema_version",1},{"emails",toml::array{message}}});r.insert_or_assign("processed",true);r.insert("released_seconds",arrival);
    }
    if (auto* tasks = p["work_requests"].as_array()) {
        for (auto& node : *tasks) {
            auto& task = *node.as_table();
            const auto request = WorkRequestState::read(task);
            if (!request.deadline) continue;
            if (request.resolved && !request.deadline_assessed) {
                const bool on_time = request.resolved_at.value_or(time) <= *request.deadline;
                task.insert("deadline_assessed", true);
                task.insert("on_time", on_time);
                const auto key = on_time ? "on_time_results" : "late_results";
                s.insert_or_assign(key, s[key].value_or(0) + 1);
            } else if (!request.resolved && *request.deadline <= time
                       && !request.deadline_notified) {
                task.insert("deadline_notified", true);
                note(p, text(task, "colleague_id"),
                    s["deadline_message"].value_or(std::string{}), *request.deadline);
            }
        }
    }
    PeopleSystem::observe_chat(p);validate(p);
}
void PhaseWorkSystem::validate(const toml::table& p){
    const auto* s=state(p);if(!s)return;require((*s)["schema_version"].value_or(0)==1 && (*s)["requests"].is_array() && (*s)["commitments"].is_array(),"Invalid saved work plan");
    for(auto k:{"installed_seconds","first_deadline_seconds"}){auto v=(*s)[k].value<double>();require(v && std::isfinite(*v) && *v>=0,"Invalid work clock");}
    for(auto k:{"on_time_results","late_results"})require((*s)[k].value_or(-1)>=0,"Invalid work counter");
    require(!text(*s,"deadline_message").empty(),"Missing deadline dialogue");
    std::unordered_set<std::string> ids;
    for(const auto& n:*(*s)["requests"].as_array()){require(n.is_table(),"Invalid request template");const auto& r=*n.as_table();auto delay=r["delay_seconds"].value<double>();require(!text(r,"id").empty() && ids.insert(text(r,"id")).second && !text(r,"after_request").empty() && delay && std::isfinite(*delay) && *delay>0 && r["processed"].is_boolean() && r["message"].is_table(),"Invalid dependent request");const auto& m=*r["message"].as_table();require(text(m,"id")==text(r,"id") && m["workflow"].is_table() && !text(m,"body").empty(),"Invalid request message");for(auto k:{"engineer_id","owner_id","colleague_id"})require(record(p,"people",text(*m["workflow"].as_table(),k)),"Unknown request person");auto d=m["workflow"]["deadline_within_seconds"].value<double>();require(d && std::isfinite(*d) && *d>0,"Invalid deadline");}
    for(const auto& n:*(*s)["requests"].as_array()){
        std::unordered_set<std::string> visited;const toml::table* r=n.as_table();
        while(r){require(visited.insert(text(*r,"id")).second,"Cyclic work dependency");const auto prerequisite=text(*r,"after_request");if(prerequisite=="missing-document")break;require(ids.contains(prerequisite),"Unknown prerequisite request");r=record(*s,"requests",prerequisite);}
    }
    ids.clear();for(const auto& n:*(*s)["commitments"].as_array()){require(n.is_table(),"Invalid commitment");const auto& c=*n.as_table();require(ids.insert(text(c,"id")).second && !text(c,"label").empty() && record(p,"people",text(c,"person")) && c["processed"].is_boolean(),"Invalid commitment owner");for(auto k:{"after_seconds","service_seconds"}){auto v=c[k].value<double>();require(v && std::isfinite(*v) && *v>=0,"Invalid commitment duration");}require(c["service_seconds"].value_or(0.0)>0,"Empty commitment");}
}
}
