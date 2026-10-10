#include "ai_sim/ChatSystem.h"
#include "ai_sim/ChatResponses.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/DefinitionLoader.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/PhaseWorkSystem.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace ai_sim {
namespace {
void require(bool ok, const std::string& why) { if(!ok)throw std::runtime_error("Chat: "+why); }
std::string text(const toml::table& t,const char* key){return t[key].value_or(std::string{});}
const std::unordered_set<std::string> capabilities{"chat","files","network","people","ai","news","activity","processes","company_chat"};
const toml::table& node(const toml::table& chat,const std::string& id){
    for(const auto& n:*chat["definition"]["nodes"].as_array())if(text(*n.as_table(),"id")==id)return *n.as_table();
    throw std::runtime_error("Chat: unknown conversation node "+id);
}
bool passes(const toml::table& people,const toml::table& value){
    const auto& chat=*people["chat"].as_table();
    const auto flag=text(value,"requires_flag");
    return (flag.empty() || chat["flags"][flag].value_or(false)==value["requires_value"].value_or(true)) && ConversationRules::passes(people,value);
}
const std::unordered_set<std::string> tabs{"chat","files","network","people","ai","news","activity","processes"};
void effects(toml::table& people,const toml::table& value){
    auto& chat=*people["chat"].as_table();
    if(value["reveal_interface"].value_or(false))chat.insert_or_assign("interface_visible",true);
    if(auto* unlocks=value["unlock"].as_array())for(const auto& c:*unlocks)ChatSystem::unlock(people,c.value_or(std::string{}));
    if(const auto flag=value["set_flag"].value<std::string>())chat["flags"].as_table()->insert_or_assign(*flag,value["set_value"].value_or(true));
    if(const auto declaration=value["declaration"].value<std::string>())chat["declarations"].as_array()->push_back(*declaration);
}
void enter(toml::table& people,std::string id,double time){
    auto& chat=*people["chat"].as_table();
    for(int transitions=0;transitions<256;++transitions){
        const auto& n=node(chat,id);chat.insert_or_assign("node_id",id);
        const auto kind=n["kind"].value_or(std::string{"message"});
        chat.erase("waiting");
        const bool had_company_chat=ChatSystem::connected(people);
        effects(people,n);
        if(!had_company_chat && ChatSystem::connected(people))chat.insert_or_assign("connected_seconds",time);
        ConversationRules::apply(people,n);
        if(n["restart"].value_or(false)) {
            const auto session=chat["session"].value_or(std::int64_t{0})+1;
            require(session<=1000000,"Restart limit exceeded");
            chat.insert_or_assign("session",session);
            if(!chat.contains("restart_history"))chat.insert("restart_history",toml::array{});
            chat["restart_history"].as_array()->push_back(toml::table{{"session",session},{"seconds",time},{"node_id",id}});
        }
        if(n["stop"].value_or(false))chat.insert_or_assign("stopped",true);
        if(kind=="condition"){
            id=text(n,passes(people,n)?"on_true":"on_false");continue;
        }
        if(!text(n,"text").empty()) {
            chat["transcript"].as_array()->push_back(toml::table{
                {"speaker",n["speaker"].value_or(std::string{"engineer"})},{"text",text(n,"text")},{"seconds",time},{"node_id",id},{"session",chat["session"].value_or(std::int64_t{0})}});
            chat.insert_or_assign("prompt_id","chat:"+std::to_string(chat["transcript"].as_array()->size()));
        }
        if(kind=="wait") {chat.insert_or_assign("waiting",true);return;}
        if(kind=="end") {chat.insert_or_assign("finished",true);return;}
        if(n["choices"].is_array() && !n["choices"].as_array()->empty())return;
        id=text(n,"next");require(!id.empty(),"Node needs choices, next or kind=end");
    }
    throw std::runtime_error("Chat: automatic conversation cycle exceeds 256 steps");
}
void validate_definition(const toml::table& def){
    require(def["schema_version"].value_or(0)==1 && def["conversation"].is_table() && def["nodes"].is_array(),"Invalid conversation definition");
    std::unordered_set<std::string> ids;
    for(const auto& n:*def["nodes"].as_array()){
        require(n.is_table(),"Node must be a table");const auto& t=*n.as_table();
        require(!text(t,"id").empty() && ids.insert(text(t,"id")).second,"Duplicate/empty node ID");
    }
    ConversationRules::validate_definition(def);
    require(ids.contains(def["conversation"]["start"].value_or(std::string{})),"Unknown start node");
    auto check_effects=[&](const toml::table& t){
        for(auto key:{"set_flag","requires_flag","declaration"})if(t.contains(key))require(t[key].is_string() && !text(t,key).empty(),"Invalid flag/declaration");
        if(t.contains("reveal_interface"))require(t["reveal_interface"].is_boolean(),"reveal_interface must be boolean");
        for(auto key:{"set_value","requires_value"})if(t.contains(key))require(t[key].is_boolean(),"Flag values must be boolean");
        if(t.contains("unlock")){require(t["unlock"].is_array(),"unlock must be an array");for(const auto& c:*t["unlock"].as_array())require(capabilities.contains(c.value_or(std::string{})),"Unknown capability");}
    };
    for(const auto& n:*def["nodes"].as_array()){
        const auto& t=*n.as_table();const auto kind=t["kind"].value_or(std::string{"message"});
        require(kind=="message" || kind=="event" || kind=="condition" || kind=="end" || kind=="wait","Unknown node kind");
        check_effects(t);
        require(!t.contains("reveal_contacts"),"Contact reveals belong on player choices");
        require(!t.contains("reveal_tabs"),"Tab reveals belong on player choices");
        for(auto key:{"text","speaker"})if(t.contains(key))require(t[key].is_string(),"Node text/speaker must be a string");
        bool continuation=kind=="end" || kind=="wait";
        if(kind=="wait")require(!t.contains("next") && !t.contains("choices") && !t.contains("reveal_interface"),"Opening wait must hold without continuation or interface reveal");
        if(kind=="condition")require((!text(t,"requires_flag").empty() || t["test"].is_table()) && ids.contains(text(t,"on_true")) && ids.contains(text(t,"on_false")),"Invalid condition branches");
        if(kind=="condition")continuation=true;
        if(t.contains("next")){require(ids.contains(text(t,"next")),"Unknown next node");continuation=true;}
        if(t.contains("choices")){
            require(t["choices"].is_array(),"choices must be an array");std::unordered_set<std::string> choices;
            for(const auto& c:*t["choices"].as_array()){
                require(c.is_table(),"Choice must be a table");const auto& v=*c.as_table();
                if(v.contains("reveal_contacts"))require(v["reveal_contacts"].is_boolean(),"reveal_contacts must be boolean");
                if(v.contains("reveal_tabs")) {
                    require(v["reveal_tabs"].is_array(),"reveal_tabs must be an array");
                    for(const auto& tab:*v["reveal_tabs"].as_array())require(tabs.contains(tab.value_or(std::string{})),"Unknown tab");
                }
                require(!text(v,"id").empty() && choices.insert(text(v,"id")).second && !text(v,"text").empty() && ids.contains(text(v,"next")),"Invalid choice");check_effects(v);
            }
            continuation|=!t["choices"].as_array()->empty();
        }
        require(!(t.contains("next") && t["choices"].is_array() && !t["choices"].as_array()->empty()),"Node cannot have both next and choices");
        require(continuation,"Node has no continuation");
    }
}
}
void ChatSystem::install(WorldState& world,const std::filesystem::path& path,bool fresh){
    if(world.people.contains("chat")){
        auto* saved=world.people["chat"].as_table();
        // Old capability unlocks have no UI provenance. Preserve them and migrate
        // only visibility; future explicitly revealed tabs survive every resume.
        require(saved!=nullptr,"Invalid saved chat");
        if(!saved->contains("visible_tabs"))saved->insert("visible_tabs",toml::array{"chat"});
        if(auto* def=(*saved)["definition"].as_table())validate_definition(*def);
        ConversationRules::install(world.people,path.parent_path().parent_path()/"people");
        ChatResponses::install(world.people,path.parent_path()/"responses.toml");
        PeopleSystem::install(world.people,path.parent_path().parent_path()/"people/jessica.toml");
        if(connected(world.people) && !world.people["chat"].as_table()->contains("connected_seconds"))world.people["chat"].as_table()->insert("connected_seconds",world.computer.time_seconds());
        MessageReviewSystem::install(world.people,path.parent_path()/"reviews/mark.toml");
        MessageReviewSystem::install_consequences(world.people,path.parent_path()/"reviews/reply_consequences.toml");
        PhaseWorkSystem::install(world,path.parent_path().parent_path()/"workflows/phase_one.toml");
        ConversationSystem::install(world.people,path.parent_path()/"first_workday.toml");
        ConversationSystem::install(world.people,path.parent_path()/"jessica_day_one.toml");
        ConversationSystem::install(world.people,path.parent_path()/"mark_reply_review.toml");
        ConversationSystem::install(world.people,path.parent_path()/"phase_one_work.toml");
        ConversationSystem::install(world.people,path.parent_path()/"source_versions.toml");
        validate(world.people);return;
    }
    // Capabilities and interface visibility are independent, including custom scenarios.
    const bool has_definition=std::filesystem::exists(path);
    toml::table chat{{"schema_version",1},{"unlock_schema",2},{"visible_tabs",toml::array{"chat"}},{"flags",toml::table{}},{"unlocked",toml::array{"chat"}},
        {"transcript",toml::array{}},{"declarations",toml::array{}},{"finished",!fresh || !has_definition}};
    auto candidate=world;
    if(has_definition){auto definition=DefinitionLoader::load(path);validate_definition(definition);chat.insert("definition",definition);}
    candidate.people.insert("chat",std::move(chat));
    ConversationRules::install(candidate.people,path.parent_path().parent_path()/"people");
    if(fresh && has_definition){
        enter(candidate.people,(*candidate.people["chat"].as_table())["definition"]["conversation"]["start"].value_or(std::string{}),candidate.computer.time_seconds());
        candidate.paused=true;
    }else{
        if(!has_definition)for(const auto& c:capabilities)unlock(candidate.people,c);
        else { unlock(candidate.people,"files"); unlock(candidate.people,"network"); }

        unlock(candidate.people,"company_chat");
        candidate.people["chat"]["flags"].as_table()->insert_or_assign("company_chat",true);
        discover(candidate.people,candidate.emails,candidate.computer.time_seconds());
    }
    ChatResponses::install(candidate.people,path.parent_path()/"responses.toml");
    PeopleSystem::observe_chat(candidate.people);
    PeopleSystem::install(candidate.people,path.parent_path().parent_path()/"people/jessica.toml");
    if(connected(candidate.people) && !candidate.people["chat"].as_table()->contains("connected_seconds"))candidate.people["chat"].as_table()->insert("connected_seconds",candidate.computer.time_seconds());
    MessageReviewSystem::install(candidate.people,path.parent_path()/"reviews/mark.toml");
    MessageReviewSystem::install_consequences(candidate.people,path.parent_path()/"reviews/reply_consequences.toml");
    PhaseWorkSystem::install(candidate,path.parent_path().parent_path()/"workflows/phase_one.toml");
    ConversationSystem::install(candidate.people,path.parent_path()/"first_workday.toml");
    ConversationSystem::install(candidate.people,path.parent_path()/"jessica_day_one.toml");
    ConversationSystem::install(candidate.people,path.parent_path()/"mark_reply_review.toml");
    ConversationSystem::install(candidate.people,path.parent_path()/"phase_one_work.toml");
    ConversationSystem::install(candidate.people,path.parent_path()/"source_versions.toml");
    validate(candidate.people);world=std::move(candidate);
}
void ChatSystem::validate(const toml::table& people){
    ChatResponses::validate(people);
    if(!people.contains("chat"))return;
    const auto* c=people["chat"].as_table();require(c && (*c)["schema_version"].value_or(0)==1,"Invalid saved chat");
    require((*c)["flags"].is_table() && (*c)["unlocked"].is_array() && (*c)["transcript"].is_array() && (*c)["declarations"].is_array() && (*c)["finished"].is_boolean(),"Missing chat state");
    for(const auto& [key,v]:*(*c)["flags"].as_table()){(void)key;require(v.is_boolean(),"Saved flag must be boolean");}
    for(const auto& v:*(*c)["unlocked"].as_array())require(capabilities.contains(v.value_or(std::string{})),"Invalid saved capability");
    if(c->contains("visible_tabs")) {
        require((*c)["visible_tabs"].is_array(),"Invalid visible tab ledger");
        std::unordered_set<std::string> seen;
        for(const auto& v:*(*c)["visible_tabs"].as_array()) {
            const auto tab=v.value_or(std::string{});
            require(tabs.contains(tab) && seen.insert(tab).second,"Invalid/duplicate visible tab");
        }
        require(seen.contains("chat"),"Chat must remain visible");
    }
    for(const auto& v:*(*c)["declarations"].as_array())require(v.is_string(),"Invalid declaration");
    if(c->contains("read_transcript")) {
        require((*c)["read_transcript"].is_table(),"Invalid transcript read markers");
        for(const auto& [key,v]:*(*c)["read_transcript"].as_table()) {
            (void)key;auto index=v.value<std::int64_t>();require(index && *index>=0,"Invalid read marker");
        }
    }
    for(const auto& n:*(*c)["transcript"].as_array()){
        require(n.is_table(),"Invalid transcript");const auto& t=*n.as_table();const auto stamp=t["seconds"].value<double>();
        require(t["speaker"].is_string() && t["text"].is_string() && stamp && std::isfinite(*stamp) && *stamp>=0,"Invalid transcript entry");
    }
    if(c->contains("definition")){require((*c)["definition"].is_table(),"Invalid stored definition");validate_definition(*(*c)["definition"].as_table());}
    if(!(*c)["finished"].value_or(true)){
        require(c->contains("definition"),"Active conversation has no definition");(void)node(*c,text(*c,"node_id"));
    }
    ConversationRules::validate_state(people);
    if(c->contains("interface_visible"))require((*c)["interface_visible"].is_boolean(),"Invalid interface reveal");
    if(c->contains("contacts_visible"))require((*c)["contacts_visible"].is_boolean(),"Invalid contacts reveal");
    if(c->contains("waiting"))require((*c)["waiting"].is_boolean(),"Invalid waiting state");
    if(waiting(people))require(!(*c)["finished"].value_or(true) && node(*c,text(*c,"node_id"))["kind"].value_or(std::string{})=="wait","Waiting state must hold at an opening wait node");
    if(c->contains("stopped"))require((*c)["stopped"].is_boolean() && (!(*c)["stopped"].value_or(false) || (*c)["finished"].value_or(false)),"Invalid shutdown state");
    if(c->contains("prompt_id"))require((*c)["prompt_id"].is_string(),"Invalid prompt ID");
    const auto session=(*c)["session"].value_or(std::int64_t{0});
    if(c->contains("session"))require((*c)["session"].is_integer() && session>=0 && session<=1000000,"Invalid restart session");
    if(c->contains("restart_history")) {
        const auto* history=(*c)["restart_history"].as_array();require(history && history->size()==static_cast<std::size_t>(session),"Invalid restart journal");
        std::int64_t expected=0;double previous=0;
        for(const auto& value:*history) {
            require(value.is_table(),"Invalid restart entry");const auto& entry=*value.as_table();auto seconds=entry["seconds"].value<double>();
            require(entry["session"].value_or(std::int64_t{-1})==++expected && seconds && std::isfinite(*seconds) && *seconds>=previous && !text(entry,"node_id").empty(),"Invalid restart entry");previous=*seconds;
        }
    } else require(session==0,"Missing restart journal");
    if(c->contains("connected_seconds")){const auto time=(*c)["connected_seconds"].value<double>();require(time && std::isfinite(*time) && *time>=0,"Invalid connection time");}
    MessageReviewSystem::validate(people);
    ConversationSystem::validate(people);
}
bool ChatSystem::available(const toml::table& people,const std::string& capability){
    const auto* c=people["chat"].as_table();if(!c)return true;
    for(const auto& n:*(*c)["unlocked"].as_array())if(n.value_or(std::string{})==capability)return true;
    return false;
}
bool ChatSystem::valid_tab(const std::string& tab){return tabs.contains(tab);}
bool ChatSystem::tab_visible(const toml::table& people,const std::string& tab){
    if(!valid_tab(tab))return false;
    const auto* list=people["chat"]["visible_tabs"].as_array();
    if(!list)return tab=="chat";
    for(const auto& value:*list)if(value.value_or(std::string{})==tab)return true;
    return false;
}
void ChatSystem::reveal_tab(toml::table& people,const std::string& tab){
    require(valid_tab(tab),"Unknown tab");
    auto* chat=people["chat"].as_table();require(chat!=nullptr,"Chat is not installed");
    if(!chat->contains("visible_tabs"))chat->insert("visible_tabs",toml::array{"chat"});
    require((*chat)["visible_tabs"].is_array(),"Invalid visible tab ledger");
    if(!tab_visible(people,tab))(*chat)["visible_tabs"].as_array()->push_back(tab);
}
bool ChatSystem::interface_visible(const toml::table& people){
    if(auto value=people["chat"]["interface_visible"].value<bool>())return *value;
    // Older saved graphs have no reveal property: completed openings and
    // explicitly earned views keep their existing interface.
    if(connected(people) || (people["chat"]["finished"].value_or(false) && !stopped(people)))return true;
    if(auto* list=people["chat"]["visible_tabs"].as_array())for(const auto& tab:*list)
        if(tab.value_or(std::string{})!="chat")return true;
    return false;
}
bool ChatSystem::contacts_visible(const toml::table& people){return connected(people) && people["chat"]["contacts_visible"].value_or(false);}
void ChatSystem::reveal_contacts(toml::table& people){require(connected(people),"Company chat is not connected");people["chat"].as_table()->insert_or_assign("contacts_visible",true);}
bool ChatSystem::waiting(const toml::table& people){return people["chat"]["waiting"].value_or(false);}
bool ChatSystem::stopped(const toml::table& people){return people["chat"]["stopped"].value_or(false);}
bool ChatSystem::connected(const toml::table& people){return !stopped(people) && available(people,"company_chat");}
void ChatSystem::unlock(toml::table& people,const std::string& capability){
    require(capabilities.contains(capability),"Unknown capability");
    if(available(people,capability))return;
    people["chat"]["unlocked"].as_array()->push_back(capability);
}
std::vector<toml::table> ChatSystem::choices(const toml::table& people){
    std::vector<toml::table> result;const auto* chat=people["chat"].as_table();
    if(!chat || (*chat)["finished"].value_or(true))return result;
    const auto current=text(*chat,"node_id");
    const auto& n=node(*chat,current);
    if(auto* list=n["choices"].as_array())for(const auto& c:*list)if(passes(people,*c.as_table()))result.push_back(*c.as_table());
    return result;
}
void ChatSystem::choose(toml::table& people,const std::string& id,double time){
    require(std::isfinite(time) && time>=0,"Invalid clock");
    validate(people);
    const auto options=choices(people);const auto found=std::find_if(options.begin(),options.end(),[&](const auto& c){return text(c,"id")==id;});
    require(found!=options.end(),"Choice is not currently available");
    auto candidate=people;auto& c=*candidate["chat"].as_table();
    c["transcript"].as_array()->push_back(toml::table{{"speaker","self:assistant"},{"text",text(*found,"text")},{"seconds",time},{"choice_id",id}});
    if(auto* list=(*found)["reveal_tabs"].as_array())for(const auto& tab:*list)reveal_tab(candidate,tab.value_or(std::string{}));
    ConversationRules::apply(candidate,*found);
    effects(candidate,*found);
    if((*found)["reveal_contacts"].value_or(false))reveal_contacts(candidate);
    enter(candidate,text(*found,"next"),time);PeopleSystem::observe_chat(candidate);validate(candidate);people=std::move(candidate);
}
std::string ChatSystem::contact_for(const toml::table& people,const std::string& address){
    if(auto* list=people["people"].as_array())for(const auto& n:*list)if(text(*n.as_table(),"email")==address)return text(*n.as_table(),"id");
    return address;
}
std::vector<ChatContact> ChatSystem::contacts(const toml::table& people,const toml::table& messages,double time){
    std::vector<ChatContact> result{{"engineer",PeopleSystem::name(people,"engineer"),0,0}};
    if(!connected(people))return result;
    for(const auto& m:EmailSystem::messages(messages,time)){
        const auto id=contact_for(people,m.sender);
        auto found=std::find_if(result.begin(),result.end(),[&](const auto& c){return c.id==id;});
        if(found==result.end()){result.push_back({id,PeopleSystem::name(people,id),m.arrival_seconds,0});found=std::prev(result.end());if(found->name==id)found->name=m.sender;}
        found->latest_seconds=std::max(found->latest_seconds,m.arrival_seconds);if(!m.read)++found->unread;
    }
    if(auto* transcript=people["chat"]["transcript"].as_array())for(std::size_t i=0;i<transcript->size();++i) {
        const auto& line=*(*transcript)[i].as_table();if(text(line,"speaker")=="self:assistant" || line["seconds"].value_or(0.0)>time)continue;
        const auto contact=line["contact_id"].value_or(std::string{"engineer"});
        auto found=std::find_if(result.begin(),result.end(),[&](const auto& c){return c.id==contact;});
        if(found==result.end()){result.push_back({contact,PeopleSystem::name(people,contact),0,0});found=std::prev(result.end());}
        found->latest_seconds=std::max(found->latest_seconds,line["seconds"].value_or(0.0));
        if(static_cast<std::int64_t>(i)>=people["chat"]["read_transcript"][contact].value_or(std::int64_t{0}))++found->unread;
    }
    return result;
}
void ChatSystem::mark_read(toml::table& people,const std::string& contact) {
    auto* chat=people["chat"].as_table();if(!chat)return;
    if(!(*chat)["read_transcript"].is_table())chat->insert("read_transcript",toml::table{});
    (*chat)["read_transcript"].as_table()->insert_or_assign(contact,static_cast<std::int64_t>((*chat)["transcript"].as_array()->size()));
}
std::vector<ChatLine> ChatSystem::timeline(const toml::table& people,const toml::table& messages,const std::string& contact,double time){
    std::vector<ChatLine> lines;
    if(auto* list=people["chat"]["transcript"].as_array())for(std::size_t i=0;i<list->size();++i){const auto& t=*(*list)[i].as_table();if(t["contact_id"].value_or(std::string{"engineer"})!=contact || t["seconds"].value_or(0.0)>time)continue;lines.push_back({text(t,"speaker"),text(t,"text"),{},t["seconds"].value_or(0.0),text(t,"speaker")=="self:assistant","chat:"+std::to_string(i+1),text(t,"conversation_id"),text(t,"node_id")});
        if(auto* runs=people["chat"]["workday"]["runs"].as_array())for(const auto& run:*runs){const auto& r=*run.as_table();
            if(text(r,"id")!=text(t,"conversation_id") || text(r,"contact")!=contact)continue;
            if(auto* tasks=people["work_requests"].as_array())for(const auto& task:*tasks)
                if(text(*task.as_table(),"id")==text(r,"request_id") && text(*task.as_table(),"colleague_id")==contact)lines.back().request_id=text(r,"request_id");
        }
    }
    if(connected(people))for(const auto& m:EmailSystem::messages(messages,time))if(contact_for(people,m.sender)==contact){
        auto body=m.body;
        for(const auto& d:m.documents)body+="\nLinked file: "+d.path;
        lines.push_back({contact,body,m.id,m.arrival_seconds,false,"message:"+m.id,{}, {}});
        for(const auto& n:*messages["emails"].as_array())if(text(*n.as_table(),"id")==m.id)if(auto* replies=(*n.as_table())["replies"].as_array())for(const auto& r:*replies){
            const auto& t=*r.as_table();if(t["sent_seconds"].value_or(0.0)>time)continue;auto body=text(t,"body");
            if(auto* attachments=t["attachments"].as_array())for(const auto& a:*attachments)body+="\nShared file: "+text(*a.as_table(),"path");
            lines.push_back({"self:assistant",body,m.id,t["sent_seconds"].value_or(0.0),true,"reply:"+m.id,{}, {}});
        }
    }
    std::stable_sort(lines.begin(),lines.end(),[](const auto& a,const auto& b){
        if(a.seconds!=b.seconds)return a.seconds<b.seconds;
        // The received request precedes its authored response prompt at the same instant.
        return a.conversation_id.empty() && !b.conversation_id.empty();
    });return lines;
}
void ChatSystem::send(toml::table& people,const toml::table& messages,const Network& network,const std::string& contact,const std::string& body,double time){
    require(connected(people),"Company chat is not connected");
    require(std::isfinite(time) && time>=0 && body.find_first_not_of(" \t\r\n")!=std::string::npos,"Message is empty or clock invalid");
    require(body.size()<=static_cast<std::size_t>(messages["mailbox"]["max_draft_bytes"].value_or(1048576)),"Message exceeds byte limit");
    const auto known=contacts(people,messages,time);
    require(std::any_of(known.begin(),known.end(),[&](const auto& c){return c.id==contact;}),"Contact has not been encountered");
    EmailSystem::check_connection(messages,network);
    if(contact!="engineer" && people["chat"]["message_reviews"]["consequences"].is_table())MessageReviewSystem::submit(people,contact,"standalone:"+contact,body,{},0.0,time,{});
    else people["chat"]["transcript"].as_array()->push_back(toml::table{{"contact_id",contact},{"speaker","self:assistant"},{"text",body},{"seconds",time}});
    PeopleSystem::observe_chat(people);
}
void ChatSystem::discover(toml::table& people,const toml::table& messages,double time){
    // Backend capability discovery never changes the visible_tabs ledger.
    if(!people.contains("chat") || !connected(people))return;
    if(contacts(people,messages,time).size()>1)unlock(people,"people");
    if(auto* tasks=people["work_requests"].as_array())for(const auto& n:*tasks){const auto& t=*n.as_table();
        if(t["arrival_seconds"].value_or(0.0)>time)continue;
        unlock(people,"activity");
        if(text(t,"access_status")!="not_requested")unlock(people,"network");
        if(text(t,"status")=="resolved")unlock(people,"ai");
    }
}
}
