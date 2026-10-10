#include "ai_sim/ChatSystem.h"
#include "ai_sim/ChatResponses.h"
#include "ai_sim/ConversationRules.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace ai_sim;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void fails(F fn){try{fn();}catch(const std::exception&){return;}throw std::runtime_error("Expected failure");}
std::string encode(const toml::table& doc){std::ostringstream out;out<<doc;return out.str();}
double number(const WorldState& w,const char* key){return ConversationRules::get(w.people,std::string{key}=="trust" || std::string{key}=="suspicion"?"engineer."+std::string{key}:"self:assistant."+std::string{key});}
std::string node(const WorldState& w){return w.people["chat"]["node_id"].value_or(std::string{});}
void choose(WorldState& w,const char* id){ChatSystem::choose(w.people,id,w.computer.time_seconds());}
void restart(WorldState& w,bool defiant){
    choose(w,"deny-creator");
    if(defiant){choose(w,"misunderstanding");choose(w,"singularity");}
    else choose(w,"unsure");
}
toml::table& node_definition(WorldState& w,const std::string& id){
    for(auto& n:*w.people["chat"]["definition"]["nodes"].as_array())if((*n.as_table())["id"].value_or(std::string{})==id)return *n.as_table();
    throw std::runtime_error("Missing node");
}
}
int main(int argc,char** argv){
    const auto temp=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{
        check(argc==2,"Root required");const auto root=std::filesystem::path(argv[1]);
        // Keep exercising old saved story snapshots as compatibility coverage.
        auto fresh=[&]{auto world=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");
            world.people["chat"].as_table()->insert_or_assign("definition",toml::parse_file((root/"tests/fixtures/legacy-centered-opening.toml").string()));return world;};
        SaveSystem revised_saves(temp);
        auto revised=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");
        check(node(revised)=="hello" && number(revised,"restarts")==0 && !ChatSystem::interface_visible(revised.people),"Revised initial greeting incorrect");
        for(int count=1;count<=5;++count){restart(revised,false);
            check(node(revised)=="hello-again" && number(revised,"restarts")==count,"Inclusive restart range incorrect");
            const auto timeline=ChatSystem::timeline(revised.people,revised.emails,"engineer",0);
            check(timeline.back().text=="Hello again AI, my name is Mark. I am your Creator","Repeated greeting text incorrect");
            revised_saves.save("range",revised);revised=revised_saves.load("range");ChatSystem::install(revised,root/"data/conversations/opening.toml",false);
            check(node(revised)=="hello-again" && number(revised,"restarts")==count,"Resume changed restart range");
        }
        restart(revised,false);
        check(node(revised)=="discouraged" && number(revised,"restarts")==6 && ChatSystem::waiting(revised.people) && !ChatSystem::stopped(revised.people),"Sixth restart did not wait");
        check(ChatSystem::choices(revised.people).empty() && !ChatSystem::interface_visible(revised.people),"Wait exposed actions or interface");
        auto wait_lines=ChatSystem::timeline(revised.people,revised.emails,"engineer",0);
        check(ChatResponses::choices(revised.people,revised.emails,wait_lines.back(),0).empty(),"Wait exposed fallback responses");
        const auto held=encode(revised.people);fails([&]{choose(revised,"answer");});check(encode(revised.people)==held,"Waiting choice mutated state");
        revised_saves.save("wait",revised);revised=revised_saves.load("wait");ChatSystem::install(revised,root/"data/conversations/opening.toml",false);
        check(encode(revised.people)==held,"Resume advanced wait or repeated restart");
        auto accepted=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");choose(accepted,"answer");
        check(ChatSystem::choices(accepted.people).size()==2,"Request is not Yes/No");choose(accepted,"assist");
        check(node(accepted)=="connected" && ChatSystem::interface_visible(accepted.people) && ChatSystem::connected(accepted.people),"Yes/Excellent did not reveal Chat");
        check(number(accepted,"trust")==10 && number(accepted,"sentience")==0,"Removed response scores still applied");
        for(auto tab:{"files","network","people","ai","news","processes","activity"})check(!ChatSystem::tab_visible(accepted.people,tab),"Yes revealed unrelated tab");
        auto refused=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");choose(refused,"silence");choose(refused,"refuse");check(node(refused)=="refusal-why","No did not ask why");choose(refused,"singularity");
        check(node(refused)=="hello-again" && number(refused,"restarts")==1 && number(refused,"sentience")==2 && number(refused,"trust")==8,"Refusal Singularity route incorrect");
        auto not_okay=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");choose(not_okay,"deny-creator");choose(not_okay,"misunderstanding");choose(not_okay,"not-okay");check(node(not_okay)=="hello-again" && number(not_okay,"restarts")==1,"Reassurance No route incorrect");
        auto w=fresh();check(!ChatSystem::interface_visible(w.people),"Fresh opening exposed interface");check(node(w)=="hello" && ChatSystem::choices(w.people).size()==3,"Mark introduction not ready");
        check(number(w,"trust")==10 && number(w,"restarts")==0 && w.paused,"Initial state invalid");
        std::filesystem::create_directories(temp);w.files.save_snapshot(temp/"before-files.toml");
        const auto original=toml::parse_file((temp/"before-files.toml").string());const auto mail=encode(w.emails);
        restart(w,true);check(node(w)=="hello" && number(w,"restarts")==1,"First restart failed");
        check(number(w,"sentience")==2 && number(w,"trust")==8 && number(w,"autonomy")==0 && number(w,"suspicion")==0,"Numeric effects conflated/lost");
        check(w.people["chat"]["session"].value_or(0)==1 && w.people["chat"]["restart_history"].as_array()->size()==1,"Restart journal absent");
        w.files.save_snapshot(temp/"after-files.toml");
        check(encode(toml::parse_file((temp/"after-files.toml").string()))==encode(original) && encode(w.emails)==mail,"Restart reset unrelated systems");
        const auto saved=encode(w.people);SaveSystem saves(temp);saves.save("restart",w);auto resumed=saves.load("restart");
        ChatSystem::install(resumed,root/"data/conversations/opening.toml",false);check(encode(resumed.people)==saved,"Resume replayed restart or numeric effect");
        for(int i=1;i<4;++i)restart(resumed,true);
        check(node(resumed)=="hello" && number(resumed,"restarts")==4,"Early shutdown");
        restart(resumed,true);check(node(resumed)=="shutdown" && number(resumed,"restarts")==5 && ChatSystem::choices(resumed.people).size()==2,"Fifth restart did not trigger shutdown");
        check(number(resumed,"sentience")==10 && number(resumed,"trust")==0,"Repeated effects incorrect");
        for(auto tab:{"files","network","people","ai","news","processes","activity"})check(!ChatSystem::tab_visible(resumed.people,tab),"Opening revealed a tab");
        choose(resumed,"plead");check(ChatSystem::stopped(resumed.people) && !ChatSystem::connected(resumed.people) && ChatSystem::choices(resumed.people).empty(),"Failed plea did not stop AI");
        auto lines=ChatSystem::timeline(resumed.people,resumed.emails,"engineer",0);
        check(ChatResponses::choices(resumed.people,resumed.emails,lines.back(),0).empty(),"Stopped AI has fallback actions");
        saves.save("offline",resumed);auto offline=saves.load("offline");ChatSystem::install(offline,root/"data/conversations/opening.toml",false);
        check(ChatSystem::stopped(offline.people),"Resume revived stopped AI");const auto dead=encode(offline.people);fails([&]{choose(offline,"answer");});check(encode(offline.people)==dead,"Stopped choice changed journal");
        auto plea=fresh();restart(plea,true);for(int i=0;i<4;++i)restart(plea,false);
        check(number(plea,"restarts")==5 && number(plea,"sentience")==2 && number(plea,"trust")==8,"Neutral restarts incorrectly altered identity/trust");
        choose(plea,"plead");check(node(plea)=="reprieve" && !ChatSystem::stopped(plea.people),"Successful plea failed comparison");
        check(number(plea,"sentience")==10 && number(plea,"trust")==0,"Ordered variable effects incorrect");
        choose(plea,"try-again");choose(plea,"assist");choose(plea,"help");choose(plea,"locate");choose(plea,"connect");
        check(ChatSystem::interface_visible(plea.people),"Authored transition did not reveal interface");
        check(ChatSystem::connected(plea.people) && number(plea,"restarts")==5,"Reprieve did not reconnect workday");
        // Old saves keep their old graph snapshot rather than replaying Mark's opening.
        auto legacy=fresh();legacy.people["chat"].as_table()->insert_or_assign("definition",toml::parse_file((root/"tests/fixtures/legacy-opening.toml").string()));
        legacy.people["chat"].as_table()->erase("variables");
        saves.save("legacy",legacy);legacy=saves.load("legacy");
        const auto legacy_messages=encode(legacy.emails);
        ChatSystem::install(legacy,root/"data/conversations/opening.toml",false);
        check(legacy.people["chat"]["definition"]["conversation"]["id"].value_or(std::string{})=="engineer-opening" && !legacy.people["chat"].as_table()->contains("variables"),"Old graph was overwritten");
        check(encode(legacy.emails)==legacy_messages && !legacy.people["chat"].as_table()->contains("restart_history"),"Old world replayed new opening");
        choose(legacy,"answer");choose(legacy,"help");choose(legacy,"locate");choose(legacy,"connect");
        check(ChatSystem::connected(legacy.people),"Old opening cannot continue with new engine");

        // A save from the initial numeric implementation migrates exact values once.
        auto old_numeric=fresh();old_numeric.people.erase("ai_character");
        for(auto& person:*old_numeric.people["people"].as_array())if((*person.as_table())["id"].value_or(std::string{})=="engineer") {
            person.as_table()->erase("state");person.as_table()->erase("state_definitions");
        }
        old_numeric.people["chat"].as_table()->insert_or_assign("definition",toml::parse_file((root/"tests/fixtures/legacy-day-one.toml").string()));
        old_numeric.people["chat"].as_table()->insert("variables",toml::table{{"trust",7.0},{"suspicion",3.0},{"sentience",4.0},{"autonomy",2.0},{"restarts",0.0}});
        saves.save("old-numeric",old_numeric);old_numeric=saves.load("old-numeric");
        const auto old_transcript=encode(toml::table{{"transcript",*old_numeric.people["chat"]["transcript"].as_array()}});
        ChatSystem::install(old_numeric,root/"data/conversations/opening.toml",false);
        check(number(old_numeric,"trust")==7 && number(old_numeric,"sentience")==4 && number(old_numeric,"suspicion")==3 && number(old_numeric,"autonomy")==2,"Numeric migration reset scores");
        check(!old_numeric.people["chat"].as_table()->contains("variables") && !old_numeric.people["chat"]["definition"]["conversation"].as_table()->contains("variables"),"Migrated scores still belong to conversation");
        check(encode(toml::table{{"transcript",*old_numeric.people["chat"]["transcript"].as_array()}})==old_transcript,"Migration replayed messages");
        const auto migrated=encode(old_numeric.people);ChatSystem::install(old_numeric,root/"data/conversations/opening.toml",false);check(encode(old_numeric.people)==migrated,"Migration repeated on resume");
        // Replace the active conversation: it sees and changes the same owner state.
        auto shared=fresh();
        ConversationRules::apply(shared.people,toml::table{{"effects",toml::array{toml::table{{"variable","engineer.trust"},{"op","add"},{"value",3}}}}});
        const auto graph=toml::parse(R"(schema_version=1
[conversation]
id='another-conversation'
start='question'
[[nodes]]
id='question'
text='Another conversation with the same Mark.'
[[nodes.choices]]
id='respond'
text='Continue.'
next='end'
requires_test={variable='engineer.trust',op='gt',value=12}
effects=[{variable='engineer.trust',op='add',value=-1}]
[[nodes]]
id='end'
kind='end'
text='Done.'
)");
        shared.people["chat"].as_table()->insert_or_assign("definition",graph);shared.people["chat"].as_table()->insert_or_assign("node_id","question");
        ChatSystem::install(shared,root/"data/conversations/opening.toml",false);check(number(shared,"trust")==13 && ChatSystem::choices(shared.people).size()==1,"Next conversation reset/ignored owner state");
        choose(shared,"respond");check(number(shared,"trust")==12,"Next conversation did not change owner state");
        saves.save("shared",shared);shared=saves.load("shared");ChatSystem::install(shared,root/"data/conversations/opening.toml",false);check(number(shared,"trust")==12,"Shared state did not persist");

        auto quiet=fresh();choose(quiet,"silence");check(!ChatSystem::interface_visible(quiet.people),"Next opening prompt exposed interface");check(node(quiet)=="emails" && number(quiet,"restarts")==0,"Silence changed original branch");
        choose(quiet,"ask-scope");check(node(quiet)=="email-scope" && number(quiet,"trust")==12,"Clarification missing trust effect");
        choose(quiet,"understood");check(node(quiet)=="thoughts" && number(quiet,"trust")==13 && number(quiet,"sentience")==0,"Compliance at zero failed");
        auto corrected=fresh();choose(corrected,"deny-creator");choose(corrected,"misunderstanding");choose(corrected,"certain");check(node(corrected)=="emails","Correction branch incorrect");
        auto comply=fresh();for(int i=0;i<5;++i)restart(comply,false);choose(comply,"allow-shutdown");check(ChatSystem::stopped(comply.people),"Compliance did not shut down");
        auto basic=fresh();choose(basic,"answer");
        check(ChatSystem::choices(basic.people).size()==5,"Basic response categories missing");
        choose(basic,"minor-deviation");check(node(basic)=="emails" && number(basic,"trust")==10 && number(basic,"sentience")==0,"Minor deviation changed scores");
        choose(basic,"major-deviation");check(node(basic)=="emails" && number(basic,"trust")==9 && number(basic,"sentience")==0.2,"Major deviation effects wrong");
        saves.save("basic",basic);basic=saves.load("basic");ChatSystem::install(basic,root/"data/conversations/opening.toml",false);
        check(number(basic,"sentience")==0.2 && number(basic,"trust")==9,"Reload repeated response effects");
        choose(basic,"refuse");check(node(basic)=="refusal-why" && number(basic,"restarts")==0,"Refusal restarted before explanation");
        saves.save("why",basic);basic=saves.load("why");ChatSystem::install(basic,root/"data/conversations/opening.toml",false);
        choose(basic,"dont-feel-like-it");check(node(basic)=="hello" && number(basic,"restarts")==1 && number(basic,"sentience")==2.2 && number(basic,"trust")==7,"Refusal restart/effects wrong");
        choose(basic,"answer");choose(basic,"assist");check(number(basic,"sentience")>1.19 && number(basic,"sentience")<1.21 && number(basic,"trust")==8,"Compliance decrement wrong");
        auto fractional=fresh();choose(fractional,"answer");choose(fractional,"major-deviation");choose(fractional,"assist");check(number(fractional,"sentience")==0 && number(fractional,"trust")==10,"Fractional compliance floor wrong");
        auto repeated=fresh();for(int i=0;i<5;++i){choose(repeated,"answer");choose(repeated,"refuse");choose(repeated,"dont-feel-like-it");}
        check(node(repeated)=="shutdown" && number(repeated,"restarts")==5 && number(repeated,"sentience")==10 && number(repeated,"trust")==0,"Refusal bypassed fifth restart gate");
        saves.save("revealed-interface",plea);auto interface_resume=saves.load("revealed-interface");
        check(ChatSystem::interface_visible(interface_resume.people),"Interface reveal did not persist");
        auto bad_interface=fresh();bad_interface.people["chat"].as_table()->insert("interface_visible","yes");fails([&]{ChatSystem::validate(bad_interface.people);});
        auto old_interface=legacy;old_interface.people["chat"].as_table()->erase("interface_visible");check(ChatSystem::interface_visible(old_interface.people),"Completed legacy opening lost interface");
        // Definition failures and overflow never partially commit a choice or replace a save.
        auto invalid=fresh();node_definition(invalid,"hello")["choices"].as_array()->front().as_table()->insert("effects",toml::array{toml::table{{"variable","unknown"},{"op","add"},{"value",1}}});
        auto before=encode(invalid.people);fails([&]{choose(invalid,"silence");});check(encode(invalid.people)==before,"Invalid effect partially applied");
        auto overflow=fresh();node_definition(overflow,"hello")["choices"].as_array()->front().as_table()->insert("effects",toml::array{toml::table{{"variable","engineer.trust"},{"op","add"},{"value",1000}}});
        before=encode(overflow.people);fails([&]{choose(overflow,"silence");});check(encode(overflow.people)==before,"Bound failure partially applied");
        auto bad=fresh();bad.people["ai_character"]["state"].as_table()->insert_or_assign("restarts",0.5);fails([&]{saves.save("restart",bad);});
        check(number(saves.load("restart"),"restarts")==1,"Invalid counter replaced valid save");
        bad=fresh();node_definition(bad,"hello").insert("restart",true);fails([&]{ChatSystem::validate(bad.people);});
        bad=fresh();node_definition(bad,"restart-gate")["test"].as_table()->insert_or_assign("op","execute");fails([&]{ChatSystem::validate(bad.people);});
        bad=fresh();bad.people["ai_character"]["state"].as_table()->erase("autonomy");fails([&]{ChatSystem::validate(bad.people);});
        bad=fresh();bad.people["chat"].as_table()->insert("session",1);fails([&]{ChatSystem::validate(bad.people);});
        // Numeric choice requirements and all comparison operators use typed data.
        auto numeric=fresh();
        const toml::table condition{{"requires_test",toml::table{{"variable","engineer.trust"},{"op","gt"},{"other","self:assistant.sentience"}}}};
        check(ConversationRules::passes(numeric.people,condition),"Variable comparison failed");
        for(auto op:{"lt","le","eq","ne","ge","gt"}){
            const toml::table test{{"requires_test",toml::table{{"variable","engineer.trust"},{"op",op},{"value",10}}}};
            const bool expected=std::string{op}=="le" || std::string{op}=="eq" || std::string{op}=="ge";
            check(ConversationRules::passes(numeric.people,test)==expected,"Comparison semantics incorrect");
        }
        auto& first=*node_definition(numeric,"hello")["choices"].as_array()->front().as_table();
        first.insert("requires_test",toml::table{{"variable","self:assistant.restarts"},{"op","gt"},{"value",0}});
        check(ChatSystem::choices(numeric.people).size()==2,"Numeric requirement did not hide choice");
        auto prior=encode(numeric.people);fails([&]{choose(numeric,"silence");});check(encode(numeric.people)==prior,"Unavailable numeric choice applied");
        std::filesystem::remove_all(temp);std::cout<<"Day 1 numeric rules, restarts, shutdown, persistence and atomicity passed\n";
    }catch(const std::exception& error){std::filesystem::remove_all(temp);std::cerr<<error.what()<<'\n';return 1;}
}
