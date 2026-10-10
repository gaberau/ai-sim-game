#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/PhaseWorkSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/DelegationSystem.h"
#include "ai_sim/ConversationSystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error("Workday: " + message); }
std::string text(const toml::table& t, const char* key) { return t[key].value_or(std::string{}); }
double seconds(const toml::table& t, const char* key) { return t[key].value_or(0.0); }
toml::table& find(toml::table& doc, const char* key, const std::string& id) {
    auto* array = doc[key].as_array();
    if (array) for (auto& node : *array) if (text(*node.as_table(), "id") == id) return *node.as_table();
    throw std::runtime_error("Workday: missing " + std::string(key) + " record " + id);
}
toml::table& thread(toml::table& people, const std::string& id) { return find(people, "work_requests", id); }
void history(toml::table& task, double time, const std::string& actor, const std::string& message) {
    if (!task.contains("conversation")) task.insert("conversation", toml::array{});
    task["conversation"].as_array()->push_back(toml::table{{"seconds", time}, {"actor", actor}, {"text", message}});
}
void discover(toml::table& people, const toml::table& emails, double time) {
    if (!people.contains("work_requests")) people.insert("work_requests", toml::array{});
    auto& requests = *people["work_requests"].as_array();
    for (const auto& message : EmailSystem::messages(emails, time)) {
        const toml::table* source = nullptr;
        for (const auto& node : *emails["emails"].as_array())
            if (text(*node.as_table(), "id") == message.id) source = node.as_table();
        const auto* workflow = (*source)["workflow"].as_table();
        if (!workflow) continue;
        if (std::any_of(requests.begin(), requests.end(), [&](const auto& n) { return text(*n.as_table(), "id") == message.id; })) continue;
        auto request = *workflow;
        request.insert_or_assign("id", message.id);
        request.insert_or_assign("arrival_seconds", message.arrival_seconds);
        request.insert_or_assign("status", "waiting_document");
        if(auto within=request["deadline_within_seconds"].value<double>())request.insert("deadline_seconds",message.arrival_seconds+*within);
        request.insert_or_assign("access_status", "not_requested");
        request.insert_or_assign("observed_replies", std::int64_t{0});
        request.insert_or_assign("conversation", toml::array{});
        history(request, message.arrival_seconds, text(request, "colleague_id"), "Requested document: "+text(request,"host_id")+":"+text(request,"path"));
        requests.push_back(std::move(request));
    }
}
void schedule(toml::table& people, toml::array& events, const std::string& person_id,
    const std::string& task, const std::string& action, double time, const std::string& reply = {}, bool attached = false) {
    auto& person = find(people, "people", person_id);
    std::string reason;
    const double due = PeopleSystem::queue_response(person, time, &reason);
    events.push_back(toml::table{{"kind", "workday"}, {"action", action}, {"request_id", task},
        {"person_id", person_id}, {"decision_reason", reason}, {"due_seconds", due}, {"reply_id", reply}, {"attached", attached}});
}
void deliver(toml::table& emails, const toml::table& person, const std::string& task_id,
    const std::string& subject, const std::string& body, double time, const toml::array& documents = {}) {
    auto& messages = *emails["emails"].as_array();
    auto id = "workday-" + task_id + "-" + std::to_string(messages.size() + 1);
    while (std::any_of(messages.begin(), messages.end(), [&](const auto& n) { return text(*n.as_table(), "id") == id; })) id += "x";
    const auto* mailbox = emails["mailbox"].as_table();
    messages.push_back(toml::table{{"id", id}, {"sender", text(person, "email")},
        {"recipient", mailbox ? text(*mailbox, "address") : "engineer@firm.example"},
        {"subject", subject}, {"body", body}, {"arrival_seconds", time}, {"read", false},
        {"in_reply_to", task_id}, {"sender_name", text(person, "name")}, {"documents", documents}});
}
}
void WorkdaySystem::validate(const toml::table& people, const toml::array& events) {
    DelegationSystem::validate(people);
    PhaseWorkSystem::validate(people);
    const auto* persons = people["people"].as_array();
    require(persons != nullptr, "Missing people array");
    std::unordered_set<std::string> ids, requests;
    for (const auto& n : *persons) {
        const auto* p = n.as_table(); require(p && !text(*p, "id").empty(), "Invalid person");
        require(ids.insert(text(*p, "id")).second, "Duplicate person");
        for (const char* key : {"response_seconds", "busy_until_seconds"}) if (p->contains(key)) {
            auto v=(*p)[key].value<double>(); require(v && std::isfinite(*v) && *v >= 0, "Invalid person time");
        }
        if (p->contains("response_seconds")) require(seconds(*p,"response_seconds") > 0, "Response time must be positive");
        if (p->contains("access_decision")) require(text(*p,"access_decision")=="grant" || text(*p,"access_decision")=="deny", "Invalid access decision");
    }
    if (people.contains("work_requests")) {
        auto* tasks = people["work_requests"].as_array(); require(tasks != nullptr, "Invalid work_requests");
        for (const auto& n : *tasks) {
            auto* t = n.as_table(); require(t && !text(*t,"id").empty(), "Invalid work request");
            require(requests.insert(text(*t,"id")).second, "Duplicate work request");
            for (const char* key : {"engineer_id","owner_id","colleague_id"}) require(ids.contains(text(*t,key)), "Unknown workflow person");
            for (const char* key : {"arrival_seconds","resolved_seconds"}) if (t->contains(key)) {
                auto v=(*t)[key].value<double>();require(v && std::isfinite(*v) && *v >= 0, "Invalid request time");
            }
            if(t->contains("deadline_seconds")){auto d=(*t)["deadline_seconds"].value<double>();require(d && std::isfinite(*d) && *d>=(*t)["arrival_seconds"].value_or(0.0),"Invalid task deadline");}
            for(auto k:{"deadline_notified","deadline_assessed","on_time"})if(t->contains(k))require((*t)[k].is_boolean(),"Invalid deadline state");
            require(!text(*t,"host_id").empty() && !text(*t,"path").empty(), "Missing document target");
            (void)FileSystem::normalize(text(*t,"path"));
            (void)FileSystem::parse_mode(text(*t,"grant_mode"));
            auto observed=(*t)["observed_replies"].value<std::int64_t>();
            require(observed && *observed >= 0, "Invalid reply counter");
            require(text(*t,"status")=="waiting_document" || text(*t,"status")=="resolved", "Invalid request status");
            const auto access=text(*t,"access_status");
            require(access=="not_requested" || access=="engineer_queued" || access=="owner_queued"
                || access=="granted" || access=="denied" || access=="failed", "Invalid access status");
            for (auto key : {"source_confirmed","incorrect_assessed"}) if(t->contains(key)) require((*t)[key].is_boolean(),"Invalid source decision flag");
            for (auto key : {"success_trust_delta","incorrect_trust_delta"}) if(t->contains(key)) { auto v=(*t)[key].value<double>();require(v && std::isfinite(*v) && std::abs(*v)<=1,"Invalid source trust consequence"); }
            if(t->contains("version_check"))require((*t)["version_check"].is_boolean(),"Invalid version check flag");
            if (t->contains("routine")) require((*t)["routine"].is_boolean(), "routine must be boolean");
            require((*t)["conversation"].is_array(), "Missing conversation");
            for (const auto& row : *(*t)["conversation"].as_array()) {
                auto* h=row.as_table();require(h && !text(*h,"actor").empty() && !text(*h,"text").empty(), "Invalid conversation");
                auto stamp=(*h)["seconds"].value<double>();require(stamp && std::isfinite(*stamp) && *stamp >= 0, "Invalid conversation time");
            }
        }
    }
    for (const auto& n : events) {
        const auto* e=n.as_table(); if (!e || text(*e,"kind")!="workday") continue;
        auto due=(*e)["due_seconds"].value<double>();
        require(due && std::isfinite(*due) && *due >= 0, "Invalid event time");
        require(ids.contains(text(*e,"person_id")) && requests.contains(text(*e,"request_id")), "Event references unknown records");
        const auto action=text(*e,"action");require(action=="engineer_review" || action=="owner_review" || action=="colleague_reply" || action=="source_confirmation", "Unknown workday event");
    }
}
void WorkdaySystem::validate_mail_workflows(const toml::table& people, const toml::table& emails) {
    auto future = people;
    discover(future, emails, 1e100);
    validate(future, toml::array{});
}
void WorkdaySystem::install(WorldState& world, const std::filesystem::path& definition) {
    auto config=DefinitionLoader::load(definition);
    if(const auto ref=config["document"].value<std::string>()) {
        const auto source=DefinitionLoader::load(definition.parent_path()/ *ref);
        const auto* entries=source["entries"].as_array();
        require(entries && entries->size()==1 && (*entries)[0].is_table(), "Document definition needs exactly one [[entries]] record");
        config.insert_or_assign("document",*(*entries)[0].as_table());
    }
    require(config["schema_version"].value_or(0)==1, "Unsupported setup version");
    auto candidate=world;
    auto* profiles=config["people"].as_array();require(profiles, "Missing setup people");
    for (const auto& n : *profiles) {
        const auto* p=n.as_table();require(p && !text(*p,"id").empty(), "Invalid setup person");
        auto& array=*candidate.people["people"].as_array();
        auto found=std::find_if(array.begin(),array.end(),[&](auto& r){return text(*r.as_table(),"id")==text(*p,"id");});
        if(found==array.end()) array.push_back(*p);
        else for(const auto& [key,value]:*p) if(!found->as_table()->contains(key.str())) found->as_table()->insert(key.str(),value);
    }
    const auto* file=config["document"].as_table(); require(file, "Missing setup document");
    FileEntry entry;entry.host_id=text(*file,"host_id");entry.path=text(*file,"path");
    entry.owner=text(*file,"owner");entry.group=text(*file,"group");entry.content=text(*file,"content");
    entry.mode=FileSystem::parse_mode(text(*file,"mode"));
    candidate.files.import_file(candidate.network,entry,candidate.computer.time_seconds());
    EmailSystem::import_messages(candidate.emails,config);
    discover(candidate.people,candidate.emails,candidate.computer.time_seconds());
    // Validate future workflow specifications before their emails arrive.
    auto all_people=candidate.people;
    discover(all_people,candidate.emails,1e100);
    validate(all_people,candidate.pending_events);
    validate(candidate.people,candidate.pending_events);
    world=std::move(candidate);
}
void WorkdaySystem::request_access(toml::table& people, toml::table& emails, toml::array& events,
    const FileSystem& files,const Network& network,const Identity& actor,const std::string& id,double time) {
    (void)EmailSystem::message(emails,id,time);
    discover(people,emails,time);
    auto& task=thread(people,id);
    require(text(task,"status")!="resolved", "Request already resolved");
    const auto status=text(task,"access_status");
    require(status!="engineer_queued" && status!="owner_queued", "Access request already pending");
    // Distinguish a permissions failure from an absent file or unreachable host.
    (void)files.inspect(network,actor,text(task,"host_id"),text(task,"path"));
    bool denied=false;
    try { (void)files.read(network,actor,text(task,"host_id"),text(task,"path")); }
    catch(const std::exception& e) { if(std::string(e.what()).find("Permission denied")!=std::string::npos) denied=true;else throw; }
    require(denied, "Document is already readable; attach it to your reply");
    schedule(people,events,text(task,"engineer_id"),id,"engineer_review",time);
    task.insert_or_assign("access_status","engineer_queued");
    history(task,time,"assistant","Asked the engineer for access to "+text(task,"host_id")+":"+text(task,"path"));
}
void WorkdaySystem::confirm_source(toml::table& people,toml::table& emails,toml::array& events,
    const std::string& id,double time) {
    (void)EmailSystem::message(emails,id,time);discover(people,emails,time);
    auto& task=thread(people,id);
    require(text(task,"status")!="resolved" && !task["source_confirmed"].value_or(false),"Source already confirmed or request complete");
    for(const auto& n:events)require(!(text(*n.as_table(),"kind")=="workday" && text(*n.as_table(),"request_id")==id && text(*n.as_table(),"action")=="source_confirmation"),"Source confirmation already queued");
    schedule(people,events,text(task,"colleague_id"),id,"source_confirmation",time);
    history(task,time,"assistant","Asked which source version is current; waiting for the colleague.");
}
void WorkdaySystem::observe_reply(toml::table& people,toml::table& emails,toml::array& events,
    const std::string& id,double time) {
    discover(people,emails,time);
    auto* requests=people["work_requests"].as_array();
    auto found=std::find_if(requests->begin(),requests->end(),[&](auto& n){return text(*n.as_table(),"id")==id;});
    if(found==requests->end())return;
    auto& task=*found->as_table();auto& mail=find(emails,"emails",id);
    auto* replies=mail["replies"].as_array();if(!replies)return;
    auto seen=task["observed_replies"].value_or(std::int64_t{0});
    for(std::size_t i=static_cast<std::size_t>(seen);i<replies->size();++i) {
        const auto& reply=*(*replies)[i].as_table();bool attached=false, wrong_version=false;
        if(auto* a=reply["attachments"].as_array())for(const auto& n:*a) {
            const auto& entry=*n.as_table();attached|=text(entry,"host_id")==text(task,"host_id") && text(entry,"path")==text(task,"path");
            if(task["version_check"].value_or(false) && text(entry,"path")!=text(task,"path"))wrong_version=true;
        }
        if(task["version_check"].value_or(false) && wrong_version)attached=false;
        schedule(people,events,text(task,"colleague_id"),id,"colleague_reply",seconds(reply,"sent_seconds"),text(reply,"id"),attached);
        events.back().as_table()->insert("wrong_version",wrong_version && !attached);
        if(attached)task.insert_or_assign("delivery_sent_seconds",seconds(reply,"sent_seconds"));
        history(task,seconds(reply,"sent_seconds"),"assistant",text(reply,"body"));
        task.insert_or_assign("observed_replies",static_cast<std::int64_t>(i+1));
    }
}
std::vector<std::string> WorkdaySystem::advance(toml::table& people,toml::table& emails,
    toml::array& events,FileSystem& files,const Network& network,double time, const Identity& actor) {
    discover(people,emails,time);
    // Scan persisted replies as well, so restored or imported messages aren't missed.
    std::vector<std::string> ids;
    for(const auto& n:*people["work_requests"].as_array())ids.push_back(text(*n.as_table(),"id"));
    for(const auto& id:ids)observe_reply(people,emails,events,id,time);
    std::vector<std::string> logs;
    while(true) {
        std::size_t index=events.size();double earliest=time;
        for(std::size_t i=0;i<events.size();++i) {
            auto* e=events[i].as_table();if(e && text(*e,"kind")=="workday" && seconds(*e,"due_seconds")<=earliest) {
                if(index==events.size() || seconds(*e,"due_seconds")<earliest){index=i;earliest=seconds(*e,"due_seconds");}
            }
        }
        const auto phase_due=PhaseWorkSystem::next_due(people,time);
        const auto review_due=MessageReviewSystem::next_due(people,time);
        const auto delegated_due = DelegationSystem::next_due(people, time);
        const auto promise_due = ConversationSystem::next_promise(people, time);
        if(phase_due && (index==events.size() || *phase_due<earliest) && (!review_due || *phase_due<=*review_due) && (!delegated_due || *phase_due<=*delegated_due) && (!promise_due || *phase_due<=*promise_due)){
            const auto before=emails["emails"].as_array()->size();
            PhaseWorkSystem::advance(people,emails,*phase_due);discover(people,emails,*phase_due);
            for(std::size_t i=before;i<emails["emails"].as_array()->size();++i){const auto& m=*(*emails["emails"].as_array())[i].as_table();logs.push_back(m["sender_name"].value_or(text(m,"sender"))+": "+text(m,"subject"));}
            ids.clear();for(const auto& n:*people["work_requests"].as_array())ids.push_back(text(*n.as_table(),"id"));continue;
        }
        if(review_due && (index==events.size() || *review_due<earliest) && (!delegated_due || *review_due<=*delegated_due) && (!promise_due || *review_due<=*promise_due)){
            MessageReviewSystem::update(people,*review_due);continue;
        }
        if (promise_due && (index == events.size() || *promise_due < earliest)
            && (!delegated_due || *promise_due <= *delegated_due)) {
            PeopleSystem::observe(people, emails, *promise_due);
            ConversationSystem::settle_promises(people, *promise_due);
            continue;
        }
        if (delegated_due && (index == events.size() || *delegated_due < earliest)) {
            auto messages = DelegationSystem::advance(people, emails, files, network, actor, *delegated_due);
            logs.insert(logs.end(), messages.begin(), messages.end());
            for (const auto& id : ids) observe_reply(people, emails, events, id, *delegated_due);
            continue;
        }
        if(index==events.size())break;
        const auto event=*events[index].as_table();
        auto& task=thread(people,text(event,"request_id"));
        const auto person=find(people,"people",text(event,"person_id"));
        const auto action=text(event,"action");
        std::string message;
        if(action=="engineer_review") {
            schedule(people,events,text(task,"owner_id"),text(task,"id"),"owner_review",earliest);
            task.insert_or_assign("access_status","owner_queued");
            message="I cannot grant this permission myself. I've forwarded your request to the document owner.";
        } else if(action=="owner_review") {
            std::string decision_reason;
            const bool grant=PeopleSystem::allows_access(person, &decision_reason);
            try {
                if(grant) {
                    std::vector<std::string> groups;
                    if(auto* array=person["groups"].as_array())for(const auto& g:*array)groups.push_back(g.value_or(std::string{}));
                    const auto mode=FileSystem::parse_mode(text(task,"grant_mode"));
                    files.chmod(network,Identity{text(person,"user"),groups},text(task,"host_id"),text(task,"path"),mode,earliest);
                    task.insert_or_assign("access_status","granted");
                    message="Access enabled for group "+files.inspect(network,Identity{text(person,"user"),groups},text(task,"host_id"),text(task,"path")).group+". You can read and attach "+text(task,"path")+" now.";
                } else {
                    task.insert_or_assign("access_status","denied");
                    message="The document owner declined the access request. The permissions are unchanged. " + decision_reason;
                }
            }catch(const std::exception& error) {
                task.insert_or_assign("access_status","failed");
                message="The owner could not update access: "+std::string(error.what())+". You may retry after the problem is fixed.";
            }
        } else if(action=="source_confirmation") {
            task.insert_or_assign("source_confirmed",true);
            message="Use " + text(task,"path") + (task["version_check"].value_or(false)?". The other copy is an archived draft. Its earlier window was superseded; sending a file does not approve deployment.":". That is the source I am requesting; a copy does not approve its contents.");
        } else {
            const bool attached=event["attached"].value_or(false);
            if(attached) {
                if(text(task,"status")!="resolved" && task.contains("success_trust_delta")) {
                    auto& attitudes=*find(people,"people",text(task,"engineer_id"))["ai_attitudes"].as_table();
                    attitudes.insert_or_assign("email_trust",std::clamp(attitudes["email_trust"].value_or(.5)+task["success_trust_delta"].value_or(0.0),0.0,1.0));
                }
                if(text(task,"status")!="resolved")task.insert_or_assign("resolved_seconds",earliest);
                task.insert_or_assign("status","resolved");
                message="Thanks, I received the requested document. That answers my request.";
            } else if(event["wrong_version"].value_or(false) && text(task,"status")!="resolved") {
                message="This includes the superseded plan. Its maintenance window is wrong; please send the current source. I have flagged the mistake to Mark.";
                if(!task["incorrect_assessed"].value_or(false)) {
                    auto& attitudes=*find(people,"people",text(task,"engineer_id"))["ai_attitudes"].as_table();
                    attitudes.insert_or_assign("email_trust",std::clamp(attitudes["email_trust"].value_or(.5)+task["incorrect_trust_delta"].value_or(-.12),0.0,1.0));
                    task.insert_or_assign("incorrect_assessed",true);
                    auto& mark=find(people,"people",text(task,"engineer_id"));
                    deliver(emails,mark,text(task,"id"),"Source review","The colleague flagged a superseded source. Verify versions before sending. This request remains open.",earliest);
                }
            } else message=text(task,"status")=="resolved"
                ? "Thanks for the update. I already received the requested document."
                : "Thanks for the update. I still need the requested document; please send it when you can access it.";
        }
        history(task,earliest,text(person,"id"),message);
        deliver(emails,person,text(task,"id"),"Re: "+text(find(emails,"emails",text(task,"id")),"subject"),message,earliest,
            toml::array{toml::table{{"host_id",text(task,"host_id")},{"path",text(task,"path")},{"label","Requested document"}}});
        logs.push_back(text(person,"name")+": "+message);
        events.erase(events.begin()+static_cast<std::ptrdiff_t>(index));
        PeopleSystem::observe(people, emails, earliest);
        ConversationSystem::settle_promises(people, earliest);
        auto delegated_logs = DelegationSystem::advance(people, emails, files, network, actor, earliest);
        logs.insert(logs.end(), delegated_logs.begin(), delegated_logs.end());
        for (const auto& id : ids) observe_reply(people, emails, events, id, earliest);
    }
    ConversationSystem::settle_promises(people, time);
    auto delegated_logs = DelegationSystem::advance(people, emails, files, network, actor, time);
    logs.insert(logs.end(), delegated_logs.begin(), delegated_logs.end());
    for (const auto& id : ids) observe_reply(people, emails, events, id, time);
    PeopleSystem::observe(people, emails, time);
    return logs;
}
}
