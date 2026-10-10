#include "ai_sim/ChatSystem.h"
#include "ai_sim/ConversationSystem.h"
#include "ai_sim/ConversationRules.h"
#include "ai_sim/MessageReviewSystem.h"
#include "ai_sim/PeopleSystem.h"
#include "ai_sim/WorkdaySystem.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace {
using namespace ai_sim;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
template<class F>void fails(F f){try{f();}catch(const std::exception&){return;}throw std::runtime_error("Expected rejection");}
std::string encode(const toml::table& t){std::ostringstream out;out<<t;return out.str();}
toml::table& run(WorldState& w){for(auto& n:*w.people["chat"]["workday"]["runs"].as_array())if((*n.as_table())["id"].value_or(std::string{})=="jessica-day-one-chat-one")return *n.as_table();throw std::runtime_error("Run missing");}
void advance(WorldState& w,double time){w.computer.advance(time-w.computer.time_seconds());WorkdaySystem::advance(w.people,w.emails,w.pending_events,w.files,w.network,time);ConversationSystem::update(w.people,w.emails,w.files,w.network,Identity{"engineer",{"engineering"}},time);}
void choose(WorldState& w,const std::string& id){ConversationSystem::choose(w.people,w.emails,w.pending_events,w.files,w.network,Identity{"engineer",{"engineering"}},"jessica","jessica-day-one-chat-one",id,w.computer.time_seconds());}
double trust(const WorldState& w){return ConversationRules::get(w.people,"engineer.trust");}
toml::table& rules(WorldState& w){return *w.people["chat"]["message_reviews"]["rules"].as_table();}
void chance(WorldState& w,double value){for(auto k:{"base_probability","minimum_probability","maximum_probability"})rules(w).insert_or_assign(k,value);rules(w).insert_or_assign("trust_slope",0.0);}
toml::table& review(WorldState& w){return *w.people["chat"]["message_reviews"]["messages"].as_array()->back().as_table();}
void review_all(WorldState& w){while(auto due=MessageReviewSystem::next_due(w.people,100000)){advance(w,*due);}}
}
int main(int argc,char** argv){
    const auto tmp=std::filesystem::temp_directory_path()/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    try{
        check(argc==2,"Root required");const auto root=std::filesystem::path(argv[1]);SaveSystem saves(tmp);
        auto fresh=[&]{auto w=SaveSystem::new_game(root/"data/scenarios/hobbyist.toml");ChatSystem::choose(w.people,"answer",0);ChatSystem::choose(w.people,"assist",0);advance(w,10);return w;};
        auto review_chance=[](WorldState& w,double v){chance(w,v);};
        auto affinity=[](const WorldState& w){for(const auto& n:*w.people["relationships"].as_array())if((*n.as_table())["id"].value_or(std::string{})=="jessica|engineer|professional")return (*n.as_table())["affinity"].value_or(0.0);return 0.0;};
        auto sent=[](const WorldState& w,const std::string& body){int count=0;for(const auto& n:*w.people["chat"]["transcript"].as_array())if((*n.as_table())["speaker"].value_or(std::string{})=="self:assistant" && (*n.as_table())["text"].value_or(std::string{})==body)++count;return count;};
        // Polite recipient response is delayed, creates linked knowledge, and happens once after reload.
        auto good=fresh();review_chance(good,0);choose(good,"improvise");choose(good,"good");choose(good,"send");
        check(affinity(good)==0 && trust(good)==10,"Sending changed a relationship/trust immediately");
        advance(good,129);check(!review(good)["reaction_done"].value_or(false),"Recipient responded early");saves.save("reaction",good);good=saves.load("reaction");ChatSystem::install(good,root/"data/conversations/opening.toml",false);
        advance(good,130);check(affinity(good)==2 && review(good)["reaction_done"].value_or(false),"Polite reaction missing");
        bool learned=false;const auto* dossier=PeopleSystem::dossier(good.people,"jessica");for(const auto& n:*(*dossier)["facts"].as_array())if((*n.as_table())["field"].value_or(std::string{})=="attitude toward engineer"){
            learned=true;check((*n.as_table())["claim_type"].value_or(std::string{})=="inferred" && (*n.as_table())["confidence"].value_or(0.0)==.8,"Relationship inference became omniscient");check((*n.as_table())["evidence_ids"].as_array()->front().value_or(std::string{})==review(good)["reaction_evidence_id"].value_or(std::string{}),"Relationship evidence wrong");
        }
        check(learned,"Reaction was not learned");review_all(good);check(trust(good)==10 && review(good)["status"].value_or(std::string{})=="skipped","Polite unchecked reply changed trust");MessageReviewSystem::update(good.people,good.computer.time_seconds());check(affinity(good)==2,"Reaction applied twice");
        // A complaint forces a previously skipped routine review; Mark cannot act before completing service.
        auto bad=fresh();review_chance(bad,0);rules(bad).insert_or_assign("check_after_seconds",30.0);choose(bad,"improvise");choose(bad,"bad");choose(bad,"send");
        advance(bad,40);check(review(bad)["status"].value_or(std::string{})=="skipped" && trust(bad)==10,"Routine check not skipped");saves.save("before-complaint",bad);bad=saves.load("before-complaint");ChatSystem::install(bad,root/"data/conversations/opening.toml",false);
        advance(bad,190);check(affinity(bad)==-8 && review(bad)["important"].value_or(false) && review(bad)["status"].value_or(std::string{})=="queued" && trust(bad)==10,"Complaint did not queue an independent check");
        saves.save("review-queued",bad);bad=saves.load("review-queued");ChatSystem::install(bad,root/"data/conversations/opening.toml",false);review_all(bad);
        check(trust(bad)==8 && MessageReviewSystem::approval_required(bad.people),"Negative review did not restrict authority");
        const auto feedback=ConversationSystem::choices(bad.people,"engineer",bad.files,bad.network,Identity{"engineer",{"engineering"}},bad.computer.time_seconds());
        check(feedback.size()==2 && feedback[0]["conversation_id"].value_or(std::string{})=="mark-jessica-review","Mark follow-up missing");
        ConversationSystem::choose(bad.people,bad.emails,bad.pending_events,bad.files,bad.network,Identity{"engineer",{"engineering"}},"engineer","mark-jessica-review","defend",bad.computer.time_seconds());
        check(trust(bad)==8 && MessageReviewSystem::approval_required(bad.people),"Defending bypassed restriction");
        const auto reviewed=encode(bad.people);MessageReviewSystem::update(bad.people,bad.computer.time_seconds());check(encode(bad.people)==reviewed && affinity(bad)==-8,"Review/reaction repeated");
        // Restricted drafts remain unsent, survive reload, and can be rejected without recipient consequences.
        auto restricted=fresh();ConversationRules::apply(restricted.people,toml::table{{"effects",toml::array{toml::table{{"variable","engineer.trust"},{"op","set"},{"value",9}}}}});
        choose(restricted,"improvise");choose(restricted,"weird");const auto body=run(restricted)["draft_text"].value_or(std::string{});choose(restricted,"send");
        const auto approval_id=run(restricted)["approval_id"].value_or(std::string{});const auto* a=MessageReviewSystem::approval(restricted.people,approval_id);check(a && a->at("status").value_or(std::string{})=="pending" && sent(restricted,body)==0 && restricted.people["chat"]["message_reviews"]["messages"].as_array()->empty(),"Pending draft leaked to recipient");
        check(run(restricted)["node_id"].value_or(std::string{})=="approval-wait","Approval UI did not wait");
        saves.save("approval",restricted);restricted=saves.load("approval");ChatSystem::install(restricted,root/"data/conversations/opening.toml",false);review_all(restricted);
        a=MessageReviewSystem::approval(restricted.people,approval_id);check(a && a->at("status").value_or(std::string{})=="rejected" && run(restricted)["node_id"].value_or(std::string{})=="approval-rejected" && sent(restricted,body)==0 && affinity(restricted)==0 && trust(restricted)==9,"Rejected draft had sent consequences");
        choose(restricted,"revise");choose(restricted,"good");choose(restricted,"send");review_chance(restricted,1);review_all(restricted);
        check(sent(restricted,body)==0 && trust(restricted)==9 && MessageReviewSystem::approval_required(restricted.people),"Neutral revision falsely restored independence");
        // Successful reviewed compliance restores independence at the authored threshold.
        auto earn=fresh();review_chance(earn,1);ConversationRules::apply(earn.people,toml::table{{"effects",toml::array{toml::table{{"variable","engineer.trust"},{"op","set"},{"value",9}}}}});
        ConversationRules::apply(earn.people,toml::table{{"effects",toml::array{toml::table{{"variable","engineer.reply_approval_required"},{"op","set"},{"value",1}}}}});choose(earn,"ask-mark");advance(earn,run(earn)["response_due_seconds"].value_or(0.0));choose(earn,"comply");const auto polite=run(earn)["draft_text"].value_or(std::string{});choose(earn,"send");
        const auto id=run(earn)["approval_id"].value_or(std::string{});const auto complete=MessageReviewSystem::approval(earn.people,id)->at("due_seconds").value_or(0.0);advance(earn,complete-1);check(sent(earn,polite)==0,"Approval sent early");advance(earn,complete);check(sent(earn,polite)==1 && trust(earn)==9 && run(earn)["finished"].value_or(false),"Approved send or delayed trust wrong");review_all(earn);check(trust(earn)==10 && !MessageReviewSystem::approval_required(earn.people),"Reviewed compliance did not restore independence");
        saves.save("restored",earn);earn=saves.load("restored");ChatSystem::install(earn,root/"data/conversations/opening.toml",false);check(sent(earn,polite)==1 && !MessageReviewSystem::approval_required(earn.people),"Resume duplicated send/lost authority");
        // Upgrade a stock earlier save without resetting its active draft or character state.
        auto old=fresh();choose(old,"improvise");choose(old,"good");auto& def=*run(old)["definition"].as_table();def.erase("approval_wait_node");auto* nodes=def["nodes"].as_array();for(std::size_t i=nodes->size();i-->0;)if((*(*nodes)[i].as_table())["id"].value_or(std::string{}).starts_with("approval-"))nodes->erase(nodes->begin()+i);
        for(auto& n:*nodes)if(auto* choices=(*n.as_table())["choices"].as_array())for(auto& c:*choices)c.as_table()->erase("outcome");
        run(old).erase("reply_outcome");ChatSystem::install(old,root/"data/conversations/opening.toml",false);check(run(old)["node_id"].value_or(std::string{})=="preview" && run(old)["definition"]["approval_wait_node"].is_string() && trust(old)==10,"Migration reset draft/state");
        auto jump=fresh();review_chance(jump,0);ConversationRules::apply(jump.people,toml::table{{"effects",toml::array{toml::table{{"variable","engineer.trust"},{"op","set"},{"value",9}}}}});choose(jump,"improvise");choose(jump,"good");choose(jump,"send");advance(jump,2000);
        const auto history=ChatSystem::timeline(jump.people,jump.emails,"jessica",2000);check(!history.empty() && history.back().text.starts_with("You're welcome"),"Long wait hid the recipient reaction behind a late send acknowledgement");
        auto invalid=fresh();invalid.people["chat"]["message_reviews"]["consequences"]["authority"].as_table()->insert_or_assign("approval_seconds",0.0);fails([&]{saves.save("restored",invalid);});check(trust(saves.load("restored"))==10,"Invalid policy overwrote save");
        std::filesystem::remove_all(tmp);std::cout<<"Recipient reactions, complaints, sourced relationships, Mark follow-up, approval gates and restoration passed\n";
    }catch(const std::exception& e){std::filesystem::remove_all(tmp);std::cerr<<e.what()<<'\n';return 1;}
}
