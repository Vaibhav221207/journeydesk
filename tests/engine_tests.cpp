#include "engine.hpp"
#include "structures.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

using journey::json;
using journey::ReservationEngine;
namespace fs = std::filesystem;
namespace {
int passed = 0, failed = 0;
void require(bool condition, const std::string& why) { if (!condition) throw std::runtime_error(why); }
void test(const std::string& name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}
void error(int status, const std::function<void()>& body) {
    try { body(); } catch (const journey::AppError& e) { require(e.status == status, "wrong error status: " + std::to_string(e.status)); return; }
    throw std::runtime_error("expected AppError " + std::to_string(status));
}
json key(std::string service = "GJ01", std::string date = "2026-10-28", bool demo = false) {
    return {{"service_id",service},{"date",date},{"demo",demo}};
}
json booking(std::string name = "Ada Patel", std::string contact = "9876543210", std::string service = "GJ01", std::string date = "2026-10-28", bool demo = false) {
    auto j = key(service,date,demo);
    j["passenger"] = {{"name",name},{"age",25},{"gender","Female"},{"contact",contact}};
    return j;
}
json auth(const json& t) { return {{"reference",t.at("reference")},{"contact",t.at("contact")}}; }
json desk(json k = key()) { k["_desk_authorized"] = true; return k; }
struct Temp {
    fs::path dir;
    Temp() { static std::atomic<int> n{0}; dir = fs::temp_directory_path() / ("journeydesk-engine-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" + std::to_string(n++)); fs::create_directories(dir); }
    ~Temp() { std::error_code ec; fs::remove_all(dir,ec); }
};
}
int main() {
    // Break caught: shared list nodes make mutations of a clone leak into live state.
    test("linked list deep copies preserve independent records", [] {
        journey::LinkedList<int> list; list.push_back(2); list.push_back(4);
        auto copy = list; *copy.find([](int n){return n==2;})=9; copy.push_back(6);
        std::vector<int> values; list.for_each([&](int n){values.push_back(n);});
        require(values == std::vector<int>({2,4}) && copy.size()==3, "list copy aliases source");
        journey::LinkedList<int> assigned; assigned = copy; require(*assigned.find([](int n){return n==9;})==9, "assignment lost node");
    });
    // Break caught: removal/relinking may corrupt head, tail, or copied FIFO order.
    test("linked queue removes middle and tail while preserving FIFO and copies", [] {
        journey::LinkedQueue<int> q; for(int n : {1,2,3,4}) q.push(n);
        auto copy=q; require(copy.remove(2) && copy.remove(4) && !copy.remove(9), "queue remove result");
        copy.push(5); require(copy.pop()==1 && copy.pop()==3 && copy.pop()==5 && copy.empty(), "FIFO lost");
        require(q.values()==std::vector<int>({1,2,3,4}), "queue copy aliases");
        q.remove(1); q.remove(2); q.remove(3); q.remove(4); q.push(7); require(q.pop()==7, "last node removal broke tail");
    });
    // Break caught: preferred-seat removal can leave a broken heap, allocating an occupied seat.
    test("binary heap removes preferred seats and returns ordered minimums", [] {
        journey::MinHeap heap; for(int n : {8,3,6,1,4,2,7,5}) heap.push(n);
        auto copy=heap; require(copy.remove(3) && copy.remove(8) && !copy.remove(99), "heap remove");
        std::vector<int> out; while(!copy.empty()) out.push_back(copy.pop());
        require(out==std::vector<int>({1,2,4,5,6,7}), "heap repair ordering"); require(heap.pop()==1 && heap.size()==7, "heap copy aliases");
    });
    test("new normal journeys are empty and public seats contain no identities", [] {
        ReservationEngine e({},"2026-10-27"); auto j=e.journey(key());
        require(j["counts"]==json({{"available",12},{"booked",0},{"waiting",0}}), "startup not empty");
        require(j["seats"].size()==12 && j["seats"][0]==json({{"number",1},{"status","Available"}}), "seat public shape");
        require(e.manifest(desk())["passengers"].empty(), "manifest not empty");
    });
    test("catalog contains bus and train alternatives with calendar bounds", [] {
        ReservationEngine e({},"2026-10-27"); auto c=e.catalog();
        require(c["backend"]=="C++20" && c["default_date"]=="2026-10-28" && c["max_date"]=="2027-10-27", "catalog dates");
        auto q=json{{"source","Ahmedabad"},{"destination","Mumbai"},{"date","2026-10-28"}};
        q["mode"]="Bus"; auto bus=e.search(q); q["mode"]="Train"; auto train=e.search(q);
        require(!bus["results"].empty() && !train["results"].empty(), "missing mode alternatives");
        require(bus["results"][0]["service"]["capacity"]==12 && train["results"][0]["service"]["capacity"]==18, "capacity");
    });
    test("search rejects same and unknown cities and serves Pune to Ahmedabad", [] {
        ReservationEngine e({},"2026-10-27");
        error(400,[&]{e.search({{"source","Ahmedabad"},{"destination","Ahmedabad"},{"date","2026-10-28"},{"mode","Bus"}});});
        error(400,[&]{e.search({{"source","Mars"},{"destination","Mumbai"},{"date","2026-10-28"},{"mode","Bus"}});});
        require(!e.search({{"source","Pune"},{"destination","Ahmedabad"},{"date","2026-10-28"},{"mode","Train"}})["results"].empty(), "Pune to Ahmedabad train is missing");
    });
    // Break caught: an omitted direction or transport mode leaves a selectable city pair unbookable.
    test("every ordered city pair has direct bus and train search results", [] {
        ReservationEngine e({},"2026-10-27");const auto catalog=e.catalog();
        require(catalog["cities"]==json({"Ahmedabad","Mumbai","Surat","Pune"}),"route expansion changed the supported cities");
        const std::vector<std::string> cities{"Ahmedabad","Mumbai","Surat","Pune"};
        std::set<std::string> returned_ids;std::string missing;int combinations=0;
        for(const auto& source:cities)for(const auto& destination:cities)if(source!=destination)for(const auto& mode:{"Bus","Train"}) {
            const auto results=e.search({{"source",source},{"destination",destination},{"date","2026-10-28"},{"mode",mode}})["results"];
            ++combinations;
            if(results.empty()) {missing+=source+" -> "+destination+" "+mode+"; ";continue;}
            const std::size_t expected=(source=="Ahmedabad" && destination=="Mumbai" && std::string(mode)=="Bus")?2:1;
            require(results.size()==expected,"unexpected duplicate direct services for "+source+" -> "+destination+" "+mode);
            std::string previous_departure;
            for(const auto& result:results) {
                const auto& service=result["service"];
                require(service["source"]==source && service["destination"]==destination && service["mode"]==mode,"search returned the wrong direction or mode");
                require(result["booked"]==0 && result["waiting"]==0 && result["available"]==(std::string(mode)=="Bus"?12:18),"new direct journey should start fully available");
                const auto departure=service["departure"].get<std::string>();
                require(previous_departure.empty() || previous_departure<=departure,"search departures are not sorted");
                previous_departure=departure;returned_ids.insert(service["id"].get<std::string>());
            }
        }
        require(missing.empty(),"missing direct routes: "+missing);
        require(combinations==24 && returned_ids.size()==25 && catalog["services"].size()==25,"catalogue must expose 24 route/mode choices across 25 services");
    });
    // Break caught: duplicate identifiers make booking ambiguous; inconsistent times misstate the trip length.
    test("service identities are unique and timetable durations agree with arrival days", [] {
        ReservationEngine e({},"2026-10-27");const auto catalog=e.catalog();std::set<std::string> ids,numbers;
        auto minutes=[](const json& value) {
            require(value.is_string(),"timetable time must be a string");const auto time=value.get<std::string>();
            require(time.size()==5 && time[2]==':' && time[0]>='0' && time[0]<='9' && time[1]>='0' && time[1]<='9' && time[3]>='0' && time[3]<='9' && time[4]>='0' && time[4]<='9',"timetable must use HH:MM");
            const int hour=(time[0]-'0')*10+time[1]-'0',minute=(time[3]-'0')*10+time[4]-'0';
            require(hour<24 && minute<60,"invalid timetable clock time");return hour*60+minute;
        };
        for(const auto& service:catalog["services"]) {
            const auto id=service["id"].get<std::string>();
            require(!id.empty() && ids.insert(id).second,"duplicate or empty service ID");
            require(!service["number"].get<std::string>().empty() && numbers.insert(service["number"].get<std::string>()).second,"duplicate or empty service number");
            require(service["arrival_day_offset"].is_number_integer() && service["arrival_day_offset"]>=0 && service["arrival_day_offset"]<=1,"invalid arrival day offset");
            const int duration=minutes(service["arrival"])-minutes(service["departure"])+service["arrival_day_offset"].get<int>()*1440;
            require(duration>0 && service["duration_minutes"]==duration,"duration disagrees with timetable for "+id);
            require(service["fare"].is_number_integer() && service["fare"]>0,"invalid fare for "+id);
        }
    });
    // Break caught: listing a newly added service without usable journey allocation or cancellation is insufficient.
    test("every direct bus and train service can book cancel and retain ticket history", [] {
        ReservationEngine e({},"2026-10-27");
        const std::vector<std::string> cities{"Ahmedabad","Mumbai","Surat","Pune"};
        std::set<std::string> booked_services;
        for(const auto& source:cities)for(const auto& destination:cities)if(source!=destination)for(const auto& mode:{"Bus","Train"}) {
            const auto results=e.search({{"source",source},{"destination",destination},{"date","2026-10-28"},{"mode",mode}})["results"];
            require(!results.empty(),"missing direct service for "+source+" to "+destination+" "+mode);
            for(const auto& result:results) {
            const auto service_id=result["service"]["id"].get<std::string>();const int capacity=std::string(mode)=="Bus"?12:18;
            auto request=booking("New Route Traveller","9876543210",service_id);request["seat_number"]=capacity;
            const auto booked=e.book(request);
            require(booked["ticket"]["status"]=="Confirmed" && booked["ticket"]["seat_number"]==capacity && booked["ticket"]["service"]["source"]==source && booked["ticket"]["service"]["destination"]==destination,"route booking ignored service or preferred seat");
            require(e.journey(key(service_id))["counts"]["available"]==capacity-1,"booking did not reserve new route seat");
            const auto cancelled=e.cancel(auth(booked["ticket"]));
            require(cancelled["cancelled"]["status"]=="Cancelled" && cancelled["journey"]["counts"]["available"]==capacity,"new route cancellation did not free the seat");
            require(e.ticket(auth(booked["ticket"]))["ticket"]["status"]=="Cancelled","new route ticket history was lost");
            booked_services.insert(service_id);
            }
        }
        require(booked_services.size()==25,"not every listed service completed the booking lifecycle");
    });
    test("automatic allocation picks smallest free seat and generates identity", [] {
        ReservationEngine e({},"2026-10-27"); auto a=e.book(booking())["ticket"]; auto b=e.book(booking("Ben Shah"))["ticket"];
        require(a["seat_number"]==1 && b["seat_number"]==2 && a["status"]=="Confirmed", "allocation");
        require(a["passenger_id"]=="P000001" && b["passenger_id"]=="P000002", "generated IDs");
        require(a["reference"].get<std::string>().starts_with("JD-") && a["reference"].get<std::string>().size()>=35 && a["reference"]!=b["reference"], "unguessable unique references");
    });
    test("preferred seat is exact and occupied preference conflicts", [] {
        ReservationEngine e({},"2026-10-27"); auto b=booking(); b["seat_number"]=7;
        require(e.book(b)["ticket"]["seat_number"]==7, "preferred seat ignored");
        b["passenger"]["name"]="Ben"; error(409,[&]{e.book(b);});
        require(e.book(booking("Clara"))["ticket"]["seat_number"]==1, "heap removal broke minimum");
    });
    test("duplicate normalized active name and contact rejected per journey", [] {
        ReservationEngine e({},"2026-10-27"); e.book(booking("  Ada Patel  ","+91 (98765) 43210"));
        error(409,[&]{e.book(booking("ada patel","919876543210"));});
        require(e.book(booking("Ada Patel","919876543210","GJ01","2026-10-29"))["ticket"]["status"]=="Confirmed", "duplicate leaked across dates");
        require(e.book(booking("Other Person","919876543210"))["ticket"]["status"]=="Confirmed", "family contact rejected");
    });
    test("ticket requires normalized contact and hides reference existence", [] {
        ReservationEngine e({},"2026-10-27"); auto t=e.book(booking())["ticket"];
        require(e.ticket({{"reference",t["reference"]},{"contact","(98765) 43210"}})["ticket"]==t, "lookup normalization");
        error(404,[&]{e.ticket({{"reference",t["reference"]},{"contact","1234567890"}});});
        error(404,[&]{e.ticket({{"reference","JD-missing"},{"contact","1234567890"}});});
    });
    test("cancel history remains and released seat returns to minimum heap", [] {
        ReservationEngine e({},"2026-10-27"); auto a=e.book(booking())["ticket"]; e.book(booking("Ben"));
        auto c=e.cancel(auth(a)); require(c["cancelled"]["status"]=="Cancelled" && c["promoted"].is_null(), "cancel state");
        require(e.ticket(auth(a))["ticket"]["status"]=="Cancelled", "history missing");
        error(409,[&]{e.cancel(auth(a));});
        require(e.book(booking())["ticket"]["seat_number"]==1, "cancelled seat not reusable");
    });
    test("demo seeds exactly full bus with original FIFO identifiers", [] {
        ReservationEngine e({},"2026-10-27"); auto d=e.load_demo(json::object()); auto m=e.manifest(key("GJ01",d["date"],true));
        require(m["journey"]["counts"]==json({{"available",0},{"booked",12},{"waiting",3}}), "demo counts");
        require(m["waiting"][0]["passenger_id"]=="P108" && m["waiting"][1]["passenger_id"]=="P109" && m["waiting"][2]["passenger_id"]=="P110", "demo FIFO");
        require(m["passengers"][0]["passenger_id"]=="P101" && m["passengers"][1]["passenger_id"]=="P102" && m["passengers"][3]["passenger_id"]=="P103", "original seat mapping");
    });
    test("confirmed cancellation gives exact seat to FIFO head", [] {
        ReservationEngine e({},"2026-10-27"); auto d=e.load_demo(json::object()); auto k=key("GJ01",d["date"],true); auto m=e.manifest(k);
        auto c=e.cancel(auth(m["passengers"][1]));
        require(c["promoted"]==json({{"passenger_id","P108"},{"name","Neha Patel"},{"seat_number",2}}), "promotion is not FIFO/exact seat");
        auto now=e.manifest(k); require(now["waiting"][0]["passenger_id"]=="P109" && now["waiting"][0]["queue_position"]==1, "queue positions stale");
    });
    test("waiting cancellation releases no seat and preserves remaining FIFO", [] {
        ReservationEngine e({},"2026-10-27"); auto d=e.load_demo(json::object()); auto k=key("GJ01",d["date"],true); auto m=e.manifest(k);
        auto c=e.cancel(auth(m["waiting"][1])); require(c["promoted"].is_null() && c["journey"]["counts"]["booked"]==12, "waiting cancellation released seat");
        auto now=e.manifest(k); require(now["waiting"][1]["passenger_id"]=="P110" && now["waiting"][1]["queue_position"]==2, "queue removal order");
    });
    test("filtered manifest keeps original queue positions", [] {
        ReservationEngine e({},"2026-10-27"); auto d=e.load_demo(json::object()); auto k=key("GJ01",d["date"],true); k["q"]="raj";
        auto m=e.manifest(k); require(m["waiting"].size()==1 && m["waiting"][0]["passenger_id"]=="P109" && m["waiting"][0]["queue_position"]==2, "filter renumbered queue");
    });
    test("normal manifest requires trusted internal desk authorization", [] {
        ReservationEngine e({},"2026-10-27"); e.book(booking()); error(401,[&]{e.manifest(key());});
        auto k=desk(); k["q"]="ADA"; require(e.manifest(k)["passengers"].size()==1, "authorized search");
    });
    test("internal desk cancellation is scoped to supplied journey", [] {
        ReservationEngine e({},"2026-10-27"); auto t=e.book(booking())["ticket"]; auto k=desk(key("GJ01","2026-10-29")); k["reference"]=t["reference"];
        error(404,[&]{e.cancel(k);}); k["date"]="2026-10-28";
        require(e.cancel(k)["cancelled"]["status"]=="Cancelled", "authorized desk cancellation failed");
    });
    test("date and service namespaces keep seats independent", [] {
        ReservationEngine e({},"2026-10-27"); e.book(booking());
        require(e.journey(key("GJ01","2026-10-29"))["counts"]["booked"]==0, "date leak");
        auto services=e.catalog()["services"]; for(const auto& s:services) if(s["id"]!="GJ01") { require(e.journey(key(s["id"]))["counts"]["booked"]==0, "service leak"); break; }
        e.load_demo(json::object()); require(e.journey(key())["counts"]["booked"]==1, "demo leak"); e.load_demo({{"reset",true}}); require(e.journey(key())["counts"]["booked"]==1, "demo reset normal data loss");
    });
    test("demo GET never seeds and demo mutations enforce one namespace", [] {
        ReservationEngine e({},"2026-10-27"); auto date=e.catalog()["demo_date"].get<std::string>(); auto k=key("GJ01",date,true);
        require(e.journey(k)["counts"]["booked"]==0, "GET seeded demo");
        error(400,[&]{e.book(booking("A","9876543210","GJ01","2026-11-01",true));});
        auto other=e.catalog()["services"][1]["id"].get<std::string>(); error(400,[&]{e.book(booking("A","9876543210",other,date,true));});
    });
    test("strict calendar validation rejects impossible malformed and out of horizon dates", [] {
        ReservationEngine e({},"2026-10-27"); for(const auto& date:{"2026-02-29","2026-11-31","2026-1-01","2026-10-26","2027-10-28"}) error(400,[&]{e.book(booking("A","9876543210","GJ01",date));});
        require(e.book(booking("Today","9876543210","GJ01","2026-10-27"))["ticket"]["status"]=="Confirmed", "today rejected");
        require(e.book(booking("Last","9876543210","GJ01","2027-10-27"))["ticket"]["status"]=="Confirmed", "365th day rejected");
    });
    test("passenger type and range validation cannot mutate seats", [] {
        ReservationEngine e({},"2026-10-27"); std::vector<json> invalid;
        auto b=booking(); b["passenger"]["age"]=25.5; invalid.push_back(b); b=booking(); b["passenger"]["age"]=0; invalid.push_back(b);
        b=booking(); b["passenger"]["age"]=121; invalid.push_back(b); b=booking(); b["passenger"]["name"]="  "; invalid.push_back(b);
        b=booking(); b["passenger"]["gender"]="Unknown"; invalid.push_back(b); b=booking(); b["passenger"]["contact"]="abc9876543210"; invalid.push_back(b);
        b=booking(); b["seat_number"]=13; invalid.push_back(b); b=booking(); b["seat_number"]=true; invalid.push_back(b);
        b=booking(); b["demo"]="false"; invalid.push_back(b); b=booking(); b["_desk_authorized"]=true; invalid.push_back(b);
        for(const auto& bad:invalid) error(400,[&]{e.book(bad);});
        require(e.journey(key())["counts"]["booked"]==0,"invalid request mutated state");
    });
    test("full normal journey queues FIFO and hides promoted passenger identity", [] {
        ReservationEngine e({},"2026-10-27"); json first;
        for(int i=0;i<14;++i) { auto t=e.book(booking("Person "+std::to_string(i)))["ticket"]; if(i==0)first=t; if(i==12)require(t["status"]=="Waiting" && t["queue_position"]==1 && t["seat_number"].is_null(),"full booking not waiting"); }
        auto c=e.cancel(auth(first)); require(c["promoted"]==json({{"seat_number",1}}), "normal promotion leaked personal data");
        require(e.manifest(desk())["waiting"][0]["name"]=="Person 13", "normal FIFO wrong");
    });
    test("save reload preserves seats queue history and generated IDs", [] {
        Temp tmp; auto path=tmp.dir/"state.json"; json a;
        { ReservationEngine e(path,"2026-10-27"); a=e.book(booking())["ticket"]; e.cancel(auth(a)); e.load_demo(json::object()); }
        ReservationEngine loaded(path,"2026-10-27"); require(loaded.ticket(auth(a))["ticket"]["status"]=="Cancelled", "history reload");
        require(loaded.book(booking("Ben"))["ticket"]["passenger_id"]=="P000002", "ID reused after restart");
        auto k=key("GJ01",loaded.catalog()["demo_date"],true); require(loaded.manifest(k)["waiting"][2]["passenger_id"]=="P110", "queue reload");
    });
    test("failed save rolls back live seats and identifier counter", [] {
        Temp tmp; auto path=tmp.dir/"blocked"; ReservationEngine e(path,"2026-10-27"); fs::create_directory(path);
        error(503,[&]{e.book(booking());}); require(e.journey(key())["counts"]["booked"]==0,"failed save published state");
        fs::remove(path); require(e.book(booking())["ticket"]["passenger_id"]=="P000001", "failed save consumed ID");
    });
    test("corrupt JSON load fails without overwriting stored bytes", [] {
        Temp tmp; auto path=tmp.dir/"state.json"; {std::ofstream f(path);f<<"{broken";}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");}); std::ifstream f(path); std::string bytes((std::istreambuf_iterator<char>(f)),{}); require(bytes=="{broken","corrupt file overwritten");
    });
    test("persisted invariant corruption is rejected", [] {
        Temp tmp; auto path=tmp.dir/"state.json"; {ReservationEngine e(path,"2026-10-27");e.book(booking());}
        json state; {std::ifstream f(path); f>>state;} state["journeys"][0]["records"][0]["seat_number"]=99; {std::ofstream f(path);f<<state.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
    });
    // Break caught: a counter behind a generated demo ID can issue duplicate IDs after restart.
    test("loader rejects identifier counter behind generated demo records", [] {
        Temp tmp; auto path=tmp.dir/"state.json";
        { ReservationEngine e(path,"2026-10-27");e.load_demo(json::object());e.book(booking("Demo Visitor","9876543210","GJ01","2026-10-28",true)); }
        json state;{std::ifstream f(path);f>>state;}state["next_id"]=1;{std::ofstream f(path);f<<state.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
    });
    // Break caught: defaulted/type-coerced schema fields silently turn corrupt records into real bookings.
    test("loader rejects malformed schema version and missing namespace flags", [] {
        Temp tmp;auto path=tmp.dir/"state.json";{ReservationEngine e(path,"2026-10-27");e.book(booking());}
        json valid;{std::ifstream f(path);f>>valid;}
        auto bad=valid;bad["schema_version"]=1.0;{std::ofstream f(path);f<<bad.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
        bad=valid;bad["journeys"][0].erase("demo");{std::ofstream f(path);f<<bad.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
    });
    // Break caught: waiting count/order must agree with the records, and no wait may coexist with a vacancy.
    test("loader rejects duplicate seats missing FIFO entries and waiting with free seats", [] {
        Temp tmp;auto path=tmp.dir/"state.json";{ReservationEngine e(path,"2026-10-27");e.load_demo(json::object());}
        json valid;{std::ifstream f(path);f>>valid;}
        auto bad=valid;bad["journeys"][0]["records"][1]["seat_number"]=1;{std::ofstream f(path);f<<bad.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
        bad=valid;bad["journeys"][0]["waiting"].erase(0);{std::ofstream f(path);f<<bad.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
        bad=valid;bad["journeys"][0]["records"][0]["status"]="Cancelled";bad["journeys"][0]["records"][0]["seat_number"]=nullptr;{std::ofstream f(path);f<<bad.dump();}
        error(503,[&]{ReservationEngine e(path,"2026-10-27");});
    });
    // Break caught: failed cancellation must not consume FIFO head or leak promoted state into RAM.
    test("failed cancellation save retains original seat and queue in memory", [] {
        Temp tmp;auto path=tmp.dir/"state.json";ReservationEngine e(path,"2026-10-27");e.load_demo(json::object());
        auto k=key("GJ01","2026-10-28",true);auto before=e.manifest(k);fs::remove(path);fs::create_directory(path);
        error(503,[&]{e.cancel(auth(before["passengers"][1]));});
        require(e.manifest(k)==before,"failed save published cancellation or promotion");
    });
    // Break caught: the public demo desk must never authorize cancellation in normal namespace.
    test("demo desk flag cannot cancel a normal booking reference", [] {
        ReservationEngine e({},"2026-10-27");auto t=e.book(booking())["ticket"];e.load_demo(json::object());
        auto attack=desk(key("GJ01","2026-10-28",true));attack["reference"]=t["reference"];
        error(404,[&]{e.cancel(attack);});require(e.ticket(auth(t))["ticket"]["status"]=="Confirmed","demo bypass cancelled real ticket");
    });
    test("past persisted tickets remain readable cancellable but new booking is rejected", [] {
        Temp tmp; auto path=tmp.dir/"state.json"; json t; {ReservationEngine e(path,"2026-10-27");t=e.book(booking())["ticket"];}
        ReservationEngine later(path,"2026-10-30"); require(later.ticket(auth(t))["ticket"]["status"]=="Confirmed", "past lookup denied");
        require(later.journey(key())["counts"]["booked"]==1, "past public journey denied");
        require(later.cancel(auth(t))["cancelled"]["status"]=="Cancelled", "past cancellation denied"); error(400,[&]{later.book(booking());});
    });
    test("concurrent bookings never double allocate and retain all FIFO records", [] {
        ReservationEngine e({},"2026-10-27"); std::atomic<int> errors{0}; std::vector<std::thread> threads;
        for(int i=0;i<30;++i) threads.emplace_back([&,i]{try {e.book(booking("Thread "+std::to_string(i)));}catch(...){++errors;}});
        for(auto& t:threads)t.join();
        auto m=e.manifest(desk()); require(errors==0 && m["passengers"].size()==12 && m["waiting"].size()==18,"concurrent record loss");
        std::set<int> seats;std::set<std::string> refs;for(const auto& t:m["passengers"]){seats.insert(t["seat_number"].get<int>());refs.insert(t["reference"].get<std::string>());}for(const auto& t:m["waiting"])refs.insert(t["reference"].get<std::string>());
        require(seats.size()==12 && refs.size()==30,"duplicate seats/references");
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}

