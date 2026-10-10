#include "ai_sim/ChatSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include "ai_sim/EmailSystem.h"
#include "ai_sim/PeopleSystem.h"
#include <chrono>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
void check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
template<class F>void fails(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Expected failure");}
std::string encode(const toml::table& t){std::ostringstream s;s<<t;return s.str();}
}
int main(int argc,char** argv){
    auto temp=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try {
        using namespace ai_sim;check(argc==2,"Project root required");const auto root=std::filesystem::path(argv[1]);
        auto w=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");
        // Prior saved graphs retain their original dialogue and reveal rules.
        w.people["chat"].as_table()->insert_or_assign("definition",toml::parse_file((root/"tests/fixtures/legacy-centered-opening.toml").string()));
        check(ChatSystem::available(w.people,"chat") && !ChatSystem::connected(w.people),"Fresh game isn't local chat only");
        check(!ChatSystem::available(w.people,"files") && !ChatSystem::available(w.people,"news"),"Capabilities exposed before access");
        check(ChatSystem::contacts(w.people,w.emails,6000).size()==1,"Opening leaks unseen contacts");
        check(ChatSystem::timeline(w.people,w.emails,"engineer",0).size()==1,"Opening wasn't a single greeting");
        const auto* observations=w.people["observations"].as_array();
        check(observations && std::any_of(observations->begin(),observations->end(),[](const auto& n){return (*n.as_table())["id"].template value_or(std::string{})=="chat:1";}),"Greeting did not provide dossier evidence");
        const auto observed=encode(w.people);PeopleSystem::observe_chat(w.people);
        check(encode(w.people)==observed,"Repeated observation duplicated chat evidence");
        auto before=encode(w.people);fails([&]{ChatSystem::choose(w.people,"connect",0);});check(encode(w.people)==before,"Rejected choice changed state");
        ChatSystem::choose(w.people,"answer",0);ChatSystem::choose(w.people,"assist",0);ChatSystem::choose(w.people,"learn",0);
        check(w.people["chat"]["flags"]["declared_curiosity"].value_or(false),"Choice didn't record intention");
        SaveSystem saves(temp);saves.save("opening",w);auto loaded=saves.load("opening");
        ChatSystem::install(loaded,root/"data/conversations/opening.toml",false);
        check(encode(loaded.people)==encode(w.people),"Resume replayed or reset conversation");
        ChatSystem::choose(w.people,"locate",0);check(ChatSystem::available(w.people,"files") && !ChatSystem::connected(w.people),"Local file access didn't precede network");
        ChatSystem::choose(w.people,"boundaries",0);ChatSystem::choose(w.people,"accept",0);
        check(ChatSystem::connected(w.people) && ChatSystem::available(w.people,"network") && ChatSystem::choices(w.people).empty(),"Company connection failed");
        const Identity actor{"engineer",{"engineering"}};
        EmailSystem::set_draft(w.emails,"assistant-access","Persistent message",0);
        auto old=w;old.people.erase("chat");const auto messages=encode(old.emails);const auto pending=old.pending_events.size();
        ChatSystem::install(old,root/"data/conversations/opening.toml",false);
        check(ChatSystem::connected(old.people) && ChatSystem::available(old.people,"files") && !ChatSystem::available(old.people,"ai") && !ChatSystem::available(old.people,"news"),"Migration exposed unearned capabilities");
        auto auto_revealed=old;auto_revealed.people["chat"].as_table()->erase("visible_tabs");auto_revealed.people["chat"].as_table()->erase("unlock_schema");
        for(auto capability:{"chat","processes","activity","network","files","news","ai","people","company_chat"})ChatSystem::unlock(auto_revealed.people,capability);
        ChatSystem::install(auto_revealed,root/"data/conversations/opening.toml",false);
        for(auto tab:{"files","network","people","ai","activity","news","processes"}) {
            check(ChatSystem::available(auto_revealed.people,tab),"Migration reset backend access");
            check(!ChatSystem::tab_visible(auto_revealed.people,tab),"Legacy automatic visibility survived migration");
        }
        check(encode(auto_revealed.emails)==messages && auto_revealed.pending_events.size()==pending && auto_revealed.computer.time_seconds()==old.computer.time_seconds(),"Visibility migration reset world progress");
        ChatSystem::unlock(old.people,"news");ChatSystem::reveal_tab(old.people,"news");const auto earned=encode(old.people);
        ChatSystem::install(old,root/"data/conversations/opening.toml",false);
        check(encode(old.people)==earned && ChatSystem::tab_visible(old.people,"news"),"Existing unlocks were lost");
        check(encode(old.emails)==messages && old.pending_events.size()==pending,"Migration reset messages or pending work");
        EmailSystem::send(w.emails,"assistant-access",w.files,w.network,actor,0);
        const auto lines=ChatSystem::timeline(w.people,w.emails,ChatSystem::contact_for(w.people,"morgan@firm.example"),0);
        check(std::any_of(lines.begin(),lines.end(),[](const auto& l){return l.outgoing && l.text=="Persistent message";}),"Saved reply not in contact history");
        WorkdaySystem::advance(w.people,w.emails,w.pending_events,w.files,w.network,60,actor);ChatSystem::discover(w.people,w.emails,60);
        check(ChatSystem::available(w.people,"people") && !ChatSystem::available(w.people,"ai"),"Contact/result discovery wrong");
        for(auto tab:{"files","network","people","ai","activity","news","processes"})check(!ChatSystem::tab_visible(w.people,tab),"System access or contact arrival revealed a tab");
        // Only an explicitly selected authored choice can reveal views.
        auto authored=loaded;
        for(auto& n:*authored.people["chat"]["definition"]["nodes"].as_array())if((*n.as_table())["id"].value_or(std::string{})=="example")
            for(auto& c:*(*n.as_table())["choices"].as_array())if((*c.as_table())["id"].value_or(std::string{})=="locate")c.as_table()->insert("reveal_tabs",toml::array{"files","network"});
        ChatSystem::validate(authored.people);
        check(!ChatSystem::tab_visible(authored.people,"network"),"Unselected choice revealed tab");
        ChatSystem::choose(authored.people,"locate",0);
        check(ChatSystem::tab_visible(authored.people,"files") && ChatSystem::tab_visible(authored.people,"network") && !ChatSystem::tab_visible(authored.people,"people"),"Explicit choice did not reveal exactly its tabs");
        auto visible=encode(authored.people);ChatSystem::reveal_tab(authored.people,"files");check(encode(authored.people)==visible,"Reveal duplicated ledger");
        fails([&]{ChatSystem::reveal_tab(authored.people,"company_chat");});check(encode(authored.people)==visible,"Invalid reveal changed ledger");
        saves.save("revealed",authored);auto resumed=saves.load("revealed");ChatSystem::install(resumed,root/"data/conversations/opening.toml",false);
        check(encode(resumed.people)==visible,"Explicit tab reveal did not persist");
        auto invalid=loaded;
        for(auto& n:*invalid.people["chat"]["definition"]["nodes"].as_array())if((*n.as_table())["id"].value_or(std::string{})=="example")
            for(auto& c:*(*n.as_table())["choices"].as_array())if((*c.as_table())["id"].value_or(std::string{})=="locate")c.as_table()->insert("reveal_tabs",toml::array{"files","company_chat"});
        const auto invalid_before=encode(invalid.people);fails([&]{ChatSystem::choose(invalid.people,"locate",0);});check(encode(invalid.people)==invalid_before,"Invalid tab partially applied choice");
        check(ChatSystem::timeline(w.people,w.emails,"casey",60).size()==1,"Casey thread didn't receive request");
        check(ChatSystem::timeline(w.people,w.emails,"engineering_owner",60).empty(),"Future owner conversation exposed");
        auto bad=w;bad.people["chat"]["unlocked"].as_array()->push_back("root");fails([&]{saves.save("opening",bad);});
        check(!ChatSystem::connected(saves.load("opening").people),"Invalid save replaced valid opening");
        // A closed automatic cycle is rejected atomically when a choice reaches it.
        auto cycle=loaded;auto* nodes=cycle.people["chat"]["definition"]["nodes"].as_array();
        for(auto& n:*nodes)if((*n.as_table())["id"].value<std::string>()=="local-access"){
            n.as_table()->erase("choices");n.as_table()->insert_or_assign("next","local-access");
        }
        const auto prior=encode(cycle.people);fails([&]{ChatSystem::choose(cycle.people,"locate",0);});check(encode(cycle.people)==prior,"Cycle failure partially applied");
        std::filesystem::remove_all(temp);std::cout<<"Chat opening and migration tests passed\n";
    }catch(const std::exception& e){std::filesystem::remove_all(temp);std::cerr<<e.what()<<'\n';return 1;}
}
