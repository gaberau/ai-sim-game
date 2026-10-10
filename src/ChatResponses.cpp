#include "ai_sim/ChatResponses.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/DelegationSystem.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
std::string text(const toml::table& t,const char* key){return t[key].value_or(std::string{});}
void require(bool ok,const std::string& why){if(!ok)throw std::runtime_error("Chat response: "+why);}
const toml::table* message(const toml::table& doc,const std::string& id){
    if(auto* list=doc["emails"].as_array())for(const auto& n:*list)if(text(*n.as_table(),"id")==id)return n.as_table();
    return nullptr;
}
const toml::table* profile(const toml::table& doc,const std::string& id){
    if(auto* list=doc["profiles"].as_array())for(const auto& n:*list)if(text(*n.as_table(),"id")==id)return n.as_table();
    return nullptr;
}
const std::unordered_set<std::string> actions{"acknowledge","read_files","share_file","request_access","report_blocked","wait","open_files","open_news","open_processes","send_reply","send_saved","discard_draft","follow_latest","open_contacts","open_people","open_mark","open_jessica"};
void validate_catalog(const toml::table& catalog){
    require(catalog["schema_version"].value_or(0)==1 && catalog["profiles"].is_array(),"Invalid catalog");
    std::unordered_set<std::string> ids;
    for(const auto& n:*catalog["profiles"].as_array()){
        require(n.is_table(),"Profile must be a table");const auto& p=*n.as_table();
        require(!text(p,"id").empty() && ids.insert(text(p,"id")).second,"Duplicate/invalid profile");
        auto* choices=p["choices"].as_array();require(choices && !choices->empty() && choices->size()<=4,"Use one to four choices per message profile");
        std::unordered_set<std::string> cids;
        for(const auto& c:*choices){require(c.is_table(),"Invalid choice");const auto& choice=*c.as_table();
            require(!text(choice,"id").empty() && cids.insert(text(choice,"id")).second && !text(choice,"text").empty() && actions.contains(text(choice,"action")),"Invalid response choice");
            if(text(choice,"action")=="send_reply")require(!text(choice,"reply").empty(),"A factual reply needs authored text");
        }
    }
    require(ids.contains("information") && ids.contains("draft") && ids.contains("document") && ids.contains("waiting") && ids.contains("blocked"),"Missing fallback response profiles");
    if(auto* mappings=catalog["messages"].as_table())for(const auto& [key,v]:*mappings){(void)key;require(v.is_string() && ids.contains(v.value_or(std::string{})),"Unknown response profile mapping");}
}
}
void ChatResponses::install(toml::table& people,const std::filesystem::path& path){
    if(!people["chat"].is_table() || !std::filesystem::exists(path))return;
    auto catalog=DefinitionLoader::load(path);validate_catalog(catalog);
    auto* chat=people["chat"].as_table();
    if(!chat->contains("response_catalog"))chat->insert("response_catalog",std::move(catalog));
    // Upgrade only the known stock profile; preserve authored/custom snapshots.
    if(auto* profiles=(*chat)["response_catalog"]["profiles"].as_array())for(auto& n:*profiles)if(text(*n.as_table(),"id")=="engineer_update") {
        auto* choices=(*n.as_table())["choices"].as_array();
        const std::vector<std::string> stock{"acknowledge","applications","public-news"};
        bool matches=choices && choices->size()==stock.size();
        if(matches)for(std::size_t i=0;i<stock.size();++i)matches&=text(*(*choices)[i].as_table(),"id")==stock[i];
        if(matches)choices->push_back(toml::table{{"id","contacts"},{"text","Show me the company contacts."},{"action","open_contacts"}});
    }
    if(auto* profiles=(*chat)["response_catalog"]["profiles"].as_array()){
        for(const auto& id:{"jessica_update","jessica_waiting","mark_review"}){
            const auto* source=profile(catalog,id);
            if(source && !profile(*(*chat)["response_catalog"].as_table(),id))profiles->push_back(*source);
        }
    }
    if(auto* profiles=(*chat)["response_catalog"]["profiles"].as_array())for(auto& n:*profiles)if(text(*n.as_table(),"id")=="jessica_update"){
        auto* choices=(*n.as_table())["choices"].as_array();bool has=false;for(const auto& c:*choices)has|=text(*c.as_table(),"action")=="open_mark";
        if(!has && choices->size()<4)choices->push_back(toml::table{{"id","mark"},{"text","Check my chat with Mark."},{"action","open_mark"}});
    }
    if(!chat->contains("responses"))chat->insert("responses",toml::table{});
    validate(people);
}
void ChatResponses::validate(const toml::table& people){
    const auto* chat=people["chat"].as_table();if(!chat)return;
    if(chat->contains("response_catalog")){require((*chat)["response_catalog"].is_table(),"Invalid saved catalog");validate_catalog(*(*chat)["response_catalog"].as_table());}
    if(chat->contains("responses")){require((*chat)["responses"].is_table(),"Invalid response ledger");for(const auto& [id,v]:*(*chat)["responses"].as_table()){(void)id;require(v.is_string(),"Invalid response record");}}
}
std::vector<toml::table> ChatResponses::choices(const toml::table& people,const toml::table& messages,const ChatLine& line,double time){
    std::vector<toml::table> result;
    if(people["chat"]["authored_only"].value_or(false))return result;
    if(ChatSystem::stopped(people) || ChatSystem::waiting(people))return result;
    if(line.id.empty() || line.outgoing || line.seconds>time)return result;
    const auto* catalog=people["chat"]["response_catalog"].as_table();if(!catalog)return result;
    std::string profile=line.speaker=="engineer"?"engineer_update":"information";
    if(line.conversation_id=="jessica-day-one-chat-one" || line.speaker=="jessica")profile="jessica_update";
    if(line.conversation_id=="mark-jessica-review")profile="mark_review";
    const auto* m=message(messages,line.request_id);
    if(m){
        if(auto* docs=(*m)["documents"].as_array();docs && !docs->empty())profile="document";
        if((*m)["workflow"].is_table())profile="blocked";
        if(auto* replies=(*m)["replies"].as_array();replies && !replies->empty())profile="waiting";
        if(auto* tasks=people["work_requests"].as_array())for(const auto& n:*tasks)if(text(*n.as_table(),"id")==line.request_id && text(*n.as_table(),"status")=="resolved")profile="information";
    }
    if(const auto mapping=(*catalog)["messages"][line.request_id].value<std::string>())profile=*mapping;
    if(auto* runs=people["chat"]["workday"]["runs"].as_array())for(const auto& n:*runs){const auto& run=*n.as_table();
        if(run["finished"].value_or(false) || !run["started"].value_or(false))continue;
        if(text(run,"id")!=line.conversation_id && text(run,"request_id")!=line.request_id)continue;
        if(auto* nodes=run["definition"]["nodes"].as_array())for(const auto& node:*nodes)
            if(text(*node.as_table(),"id")==text(run,"node_id") && text(*node.as_table(),"kind")=="wait")profile=text(run,"id")=="jessica-day-one-chat-one"?"jessica_waiting":"waiting";
    }
    if(m && (!(*m)["draft"].value_or(std::string{}).empty() || ((*m)["draft_attachments"].is_array() && !(*m)["draft_attachments"].as_array()->empty())))profile="draft";
    if(!ChatSystem::connected(people))profile="opening_history";
    const bool responded=people["chat"]["responses"][line.id].is_string();
    bool already_shared=false;
    if(m)if(auto* docs=(*m)["documents"].as_array();docs && !docs->empty()) {
        already_shared=std::all_of(docs->begin(),docs->end(),[&](const auto& doc){
            bool delivered=false;
            if(auto* replies=(*m)["replies"].as_array())for(const auto& reply:*replies)
                if(auto* attachments=(*reply.as_table())["attachments"].as_array())for(const auto& attachment:*attachments)
                    delivered|=text(*attachment.as_table(),"host_id")==text(*doc.as_table(),"host_id") && text(*attachment.as_table(),"path")==text(*doc.as_table(),"path");
            return delivered;
        });
    }

    for(const auto& n:*(*catalog)["profiles"].as_array()){
        const auto& p=*n.as_table();if(text(p,"id")!=profile)continue;
        for(const auto& c:*p["choices"].as_array()){
            auto choice=*c.as_table();const auto action=text(choice,"action");
            if(responded && (action=="acknowledge" || action=="send_reply" || action=="report_blocked"))continue;
            if(action=="share_file" && already_shared)continue;
            if(action=="open_contacts" && (!ChatSystem::connected(people) || ChatSystem::contacts_visible(people)))continue;
            if(action=="read_files" && (!m || !(*m)["documents"].is_array() || (*m)["documents"].as_array()->empty()))continue;
            if((action=="wait" || action=="send_reply" || action=="share_file" || action=="request_access" || action=="report_blocked") && !ChatSystem::connected(people))continue;
            if(action=="request_access" && (!m || !(*m)["workflow"].is_table()))continue;
            choice.insert("message_id",line.id);result.push_back(std::move(choice));
        }
    }
    return result;
}
void ChatResponses::choose(toml::table& people,toml::table& messages,toml::array& events,
    const FileSystem& files,const Network& network,const Identity& actor,const std::string& contact,
    const ChatLine& line,const toml::table& choice,double time){
    const auto timeline=ChatSystem::timeline(people,messages,contact,time);
    require(std::any_of(timeline.begin(),timeline.end(),[&](const auto& v){return !v.outgoing && v.id==line.id && v.text==line.text;}),"Message not in this conversation");
    const auto options=choices(people,messages,line,time);
    const auto found=std::find_if(options.begin(),options.end(),[&](const auto& c){return text(c,"id")==text(choice,"id") && text(c,"message_id")==text(choice,"message_id");});
    require(found!=options.end(),"Choice no longer available");
    const auto action=text(*found,"action");auto p=people,m=messages;auto e=events;
    bool prepared=false;
    if(auto* tasks=p["work_requests"].as_array())for(const auto& n:*tasks)prepared |= text(*n.as_table(),"id")==line.request_id && text(*n.as_table(),"delegation_state")=="prepared";
    if(action=="discard_draft") {
        if(prepared)DelegationSystem::discard(p,m,line.request_id,time);
        else EmailSystem::clear_draft(m,line.request_id,time);
    }
    else if(action=="send_saved"){
        if(prepared)DelegationSystem::approve(p,m,files,network,actor,line.request_id,time);
        else EmailSystem::send(m,line.request_id,files,network,actor,time);
        WorkdaySystem::observe_reply(p,m,e,line.request_id,time);
    }
    else if(action=="request_access")WorkdaySystem::request_access(p,m,e,files,network,actor,line.request_id,time);
    else if(action=="share_file" || action=="send_reply" || action=="report_blocked"){
        const auto incoming=EmailSystem::message(m,line.request_id,time);
        require(incoming.draft.empty() && !incoming.attachment_count,"An unfinished draft already exists; preserve it before sending another reply");
        const std::string body=action=="share_file"?"Here is the current requested file.":action=="report_blocked"?"I cannot access the requested file. Please ask the document owner directly.":text(*found,"reply");
        if(action=="report_blocked"){
            require(!incoming.documents.empty(),"No requested document");bool blocked=false;
            for(const auto& doc:incoming.documents)try{(void)files.read(network,actor,doc.host_id,doc.path);}catch(const std::exception&){blocked=true;}
            require(blocked,"The requested file is readable; a blocked report would be inaccurate");
        }
        EmailSystem::set_draft(m,line.request_id,body,time);
        if(action=="share_file"){
            require(!incoming.documents.empty(),"No requested document");
            for(std::size_t i=0;i<incoming.documents.size();++i)EmailSystem::attach(m,line.request_id,i,files,network,actor,time);
        }
        EmailSystem::send(m,line.request_id,files,network,actor,time);
        WorkdaySystem::observe_reply(p,m,e,line.request_id,time);
    }else require(action=="acknowledge","This choice is handled by the view");
    if(action=="acknowledge")ChatSystem::send(p,m,network,contact,text(*found,"text"),time);
    if(action=="discard_draft")p["chat"]["responses"].as_table()->erase(line.id);
    else p["chat"]["responses"].as_table()->insert_or_assign(line.id,text(*found,"id"));
    validate(p);EmailSystem::validate(m);WorkdaySystem::validate(p,e);
    people=std::move(p);messages=std::move(m);events=std::move(e);
}
}
