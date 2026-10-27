#include "engine.hpp"
#include "structures.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>
#endif

namespace journey {
AppError::AppError(std::string message, int code, json details)
    : std::runtime_error(std::move(message)), status(code), fields(std::move(details)) {}
namespace {
using Day = std::chrono::sys_days;
std::string trim(std::string value) {
    const auto first=value.find_first_not_of(" \t\r\n");
    if(first==std::string::npos)return {};
    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
}
std::string lower(std::string value) {
    for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
[[noreturn]] void invalid(const std::string& field,const std::string& message) {throw AppError(message,400,{{field,message}});}
void object(const json& input) {if(!input.is_object())invalid("request","A JSON object is required.");}
std::string text(const json& input,const std::string& field) {
    if(!input.contains(field) || !input[field].is_string())invalid(field,"Enter a valid "+field+".");
    return input[field].get<std::string>();
}
bool boolean(const json& input,const std::string& field,bool fallback=false) {
    if(!input.contains(field))return fallback;
    if(!input[field].is_boolean())invalid(field,field+" must be true or false.");
    return input[field].get<bool>();
}
int integer(const json& input,const std::string& field,int minimum,int maximum) {
    if(!input.contains(field) || !input[field].is_number_integer() || input[field].is_boolean())invalid(field,"Enter a whole number for "+field+".");
    if(input[field]<minimum || input[field]>maximum)invalid(field,field+" is outside the permitted range.");
    return input[field].get<int>();
}
Day parse_date(const std::string& value) {
    if(value.size()!=10 || value[4]!='-' || value[7]!='-')invalid("date","Use a valid date in YYYY-MM-DD format.");
    for(std::size_t i=0;i<value.size();++i)if(i!=4 && i!=7 && !std::isdigit(static_cast<unsigned char>(value[i])))invalid("date","Use a valid date in YYYY-MM-DD format.");
    const int y=std::stoi(value.substr(0,4));
    const unsigned m=static_cast<unsigned>(std::stoi(value.substr(5,2))),d=static_cast<unsigned>(std::stoi(value.substr(8,2)));
    const std::chrono::year_month_day day{std::chrono::year{y},std::chrono::month{m},std::chrono::day{d}};
    if(y<1 || !day.ok())invalid("date","That calendar date does not exist.");
    return Day{day};
}
std::string date_string(Day day) {
    const std::chrono::year_month_day parts{day};char out[16];
    std::snprintf(out,sizeof(out),"%04d-%02u-%02u",static_cast<int>(parts.year()),static_cast<unsigned>(parts.month()),static_cast<unsigned>(parts.day()));
    return out;
}
std::string contact(const std::string& value) {
    std::string digits;
    for(unsigned char c:value) {
        if(c>='0' && c<='9')digits.push_back(static_cast<char>(c));
        else if(c!='+' && c!=' ' && c!='(' && c!=')' && c!='-')invalid("contact","Contact must contain 10 to 15 digits.");
    }
    if(digits.size()<10 || digits.size()>15)invalid("contact","Contact must contain 10 to 15 digits.");
    return digits;
}
std::string passenger_name(const std::string& value) {
    auto cleaned=trim(value);std::size_t characters=0;
    for(unsigned char c:cleaned) {if(c<32 || c==127)invalid("name","Name cannot contain control characters.");if((c&0xc0)!=0x80)++characters;}
    if(characters==0 || characters>100)invalid("name","Name must contain 1 to 100 characters.");
    return cleaned;
}
std::string gender(const std::string& value) {
    if(value!="Female" && value!="Male" && value!="Other" && value!="Prefer not to say")invalid("gender","Choose a listed gender option.");
    return value;
}
std::string random_token() {
    std::array<unsigned char,32> bytes{};
#ifdef _WIN32
    // Use Windows' cryptographic generator without requiring a shipped DLL.
    HMODULE library=LoadLibraryW(L"bcrypt.dll");
    using Generate = LONG (WINAPI*)(void*,unsigned char*,unsigned long,unsigned long);
    const auto address=library?GetProcAddress(library,"BCryptGenRandom"):nullptr;
    Generate generate=nullptr;
    static_assert(sizeof(generate)==sizeof(address));
    std::memcpy(&generate,&address,sizeof(generate));
    const bool ok=generate && generate(nullptr,bytes.data(),static_cast<unsigned long>(bytes.size()),2)==0;
    if(library)FreeLibrary(library);
    if(!ok)throw AppError("Secure booking references are temporarily unavailable.",503);
#else
    std::size_t filled=0;
    while(filled<bytes.size()) {
        const auto n=::getrandom(bytes.data()+filled,bytes.size()-filled,0);
        if(n<0 && errno==EINTR)continue;
        if(n<=0)throw AppError("Secure booking references are temporarily unavailable.",503);
        filled+=static_cast<std::size_t>(n);
    }
#endif
    static constexpr char digits[]="0123456789ABCDEF";std::string result="JD-";
    for(auto b:bytes){result.push_back(digits[b>>4]);result.push_back(digits[b&15]);}
    return result;
}
std::vector<json> service_catalogue() {
    auto s=[](std::string id,std::string number,std::string name,std::string mode,std::string from,std::string to,std::string departure,std::string arrival,int offset,int duration,int fare) {
        const int capacity=mode=="Bus"?12:18;
        return json{{"id",id},{"number",number},{"name",name},{"mode",mode},{"source",from},{"destination",to},{"departure",departure},{"arrival",arrival},{"arrival_day_offset",offset},{"duration_minutes",duration},{"fare",fare},{"capacity",capacity}};
    };
    return {
        s("GJ01","GJ-B101","Coastal Express","Bus","Ahmedabad","Mumbai","08:00","17:30",0,570,850),
        s("GJ02","GJ-T201","Western Intercity","Train","Ahmedabad","Mumbai","06:30","13:00",0,390,620),
        s("GJ03","GJ-B102","Evening Coastliner","Bus","Ahmedabad","Mumbai","21:00","06:30",1,570,950),
        s("MH01","MH-B101","Gujarat Connector","Bus","Mumbai","Ahmedabad","07:00","16:30",0,570,850),
        s("MH02","MH-T201","Western Return","Train","Mumbai","Ahmedabad","15:00","21:30",0,390,620),
        s("GJ04","GJ-B103","Surat Shuttle","Bus","Ahmedabad","Surat","09:00","13:30",0,270,450),
        s("GJ05","GJ-T202","Surat Intercity","Train","Surat","Ahmedabad","17:00","20:30",0,210,330),
        s("MH03","MH-B102","Deccan Shuttle","Bus","Mumbai","Pune","10:00","13:30",0,210,380),
        s("MH04","MH-B103","Mumbai Connector","Bus","Pune","Mumbai","16:00","19:30",0,210,380),
        s("GJ06","GJ-T203","Coast Intercity","Train","Surat","Mumbai","08:30","12:00",0,210,350),
        s("GJ07","GJ-T204","Gujarat Morning Intercity","Train","Ahmedabad","Surat","07:15","10:45",0,210,330),
        s("GJ08","GJ-B104","Deccan Nightliner","Bus","Ahmedabad","Pune","18:30","07:00",1,750,1200),
        s("GJ09","GJ-T205","Sabarmati Deccan Express","Train","Ahmedabad","Pune","20:00","05:30",1,570,780),
        s("MH05","MH-B104","Tapi Connector","Bus","Mumbai","Surat","07:30","12:00",0,270,500),
        s("MH06","MH-T202","Coast Return","Train","Mumbai","Surat","18:15","21:45",0,210,350),
        s("MH07","MH-T203","Deccan Intercity","Train","Mumbai","Pune","06:45","09:45",0,180,260),
        s("GJ10","GJ-B105","Sabarmati Shuttle","Bus","Surat","Ahmedabad","14:30","19:00",0,270,450),
        s("GJ11","GJ-B106","Mumbai Coastliner","Bus","Surat","Mumbai","15:00","19:30",0,270,500),
        s("GJ12","GJ-B107","Tapi Deccan Nightliner","Bus","Surat","Pune","22:00","05:30",1,450,780),
        s("GJ13","GJ-T206","Tapi Deccan Express","Train","Surat","Pune","16:00","22:00",0,360,540),
        s("MH08","MH-B105","Sabarmati Nightliner","Bus","Pune","Ahmedabad","18:00","06:30",1,750,1200),
        s("MH09","MH-T204","Deccan Sabarmati Express","Train","Pune","Ahmedabad","19:30","05:00",1,570,780),
        s("MH10","MH-T205","Deccan Return","Train","Pune","Mumbai","17:30","20:30",0,180,260),
        s("MH11","MH-B106","Tapi Nightliner","Bus","Pune","Surat","21:00","04:30",1,450,780),
        s("MH12","MH-T206","Deccan Tapi Express","Train","Pune","Surat","07:00","13:00",0,360,540)
    };
}
struct Booking {std::string reference,id,name,gender,contact,status;int age=0,seat=0;};
struct JourneyState {
    std::string service_id,date;bool demo=false;int capacity=0;
    // Fixed seat array: buses use 12 entries and trains use all 18.
    std::array<std::string,18> seats{};
    LinkedList<Booking> records;
    LinkedQueue<std::string> waiting;
    MinHeap free_seats;
    JourneyState()=default;
    JourneyState(std::string service,std::string day,bool is_demo,int size):service_id(std::move(service)),date(std::move(day)),demo(is_demo),capacity(size) {for(int n=1;n<=capacity;++n)free_seats.push(n);}
    Booking* find(const std::string& reference) {return records.find([&](const Booking& b){return b.reference==reference;});}
    const Booking* find(const std::string& reference) const {return records.find([&](const Booking& b){return b.reference==reference;});}
};
struct State {std::map<std::string,JourneyState> journeys;std::uint64_t next_id=1;std::string demo_date;};
std::string journey_key(const std::string& id,const std::string& date,bool demo) {return (demo?"demo|":"normal|")+id+"|"+date;}
bool trusted_desk(const json& input) {return input.contains("_desk_authorized") && input["_desk_authorized"].is_boolean() && input["_desk_authorized"].get<bool>();}
void atomic_write(const std::filesystem::path& path,const std::string& bytes) {
    const auto temporary=std::filesystem::path(path.native()+std::filesystem::path(".tmp-"+random_token().substr(3,16)).native());bool created=false;
    try {
        if(!path.parent_path().empty())std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("create");
        created=true;std::size_t offset=0;bool ok=true;
        while(offset<bytes.size()) {
            DWORD written=0;const DWORD count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-offset,1024*1024));
            if(!WriteFile(file,bytes.data()+offset,count,&written,nullptr) || written==0){ok=false;break;}
            offset+=written;
        }
        if(ok)ok=FlushFileBuffers(file)!=0;
        if(!CloseHandle(file))ok=false;
        if(!ok)throw std::runtime_error("write");
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("replace");
#else
        const int file=::open(temporary.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(file<0)throw std::runtime_error("create");
        created=true;std::size_t offset=0;bool ok=true;
        while(offset<bytes.size()) {
            const auto n=::write(file,bytes.data()+offset,bytes.size()-offset);
            if(n<0 && errno==EINTR)continue;
            if(n<=0){ok=false;break;}
            offset+=static_cast<std::size_t>(n);
        }
        if(ok)ok=::fsync(file)==0;
        if(::close(file)!=0)ok=false;
        if(!ok)throw std::runtime_error("write");
        if(::rename(temporary.c_str(),path.c_str())!=0)throw std::runtime_error("replace");
#endif
    } catch(...) {
        if(created){std::error_code ec;std::filesystem::remove(temporary,ec);}
        throw AppError("The reservation could not be saved. Please try again.",503);
    }
}
}
struct ReservationEngine::Impl {
    mutable std::mutex mutex;
    std::filesystem::path storage;
    std::string today_override;
    std::vector<json> services=service_catalogue();
    State state;

    Day today() const {
        return today_override.empty()?std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()+std::chrono::minutes{330}):parse_date(today_override);
    }
    const json& service(const std::string& id) const {
        for(const auto& s:services)if(s["id"]==id)return s;
        invalid("service_id","Choose a listed service.");
    }
    struct Selection {std::string service_id,date,key;bool demo;int capacity;};
    Selection select(const json& input,bool booking_date=false) const {
        object(input);const auto id=text(input,"service_id");const auto& s=service(id);
        const auto date=text(input,"date");const auto day=parse_date(date);const bool demo=boolean(input,"demo");
        if(demo && (id!="GJ01" || date!=state.demo_date))invalid("demo","Demonstration journeys use the labelled GJ01 sample date only.");
        if(booking_date && !demo && (day<today() || day>today()+std::chrono::days{365}))invalid("date","Booking dates must be between today and 365 days ahead.");
        return {id,date,journey_key(id,date,demo),demo,s["capacity"].get<int>()};
    }
    JourneyState empty(const Selection& s) const {return JourneyState(s.service_id,s.date,s.demo,s.capacity);}
    JourneyState& ensure(State& target,const Selection& s) const {
        auto [it,inserted]=target.journeys.try_emplace(s.key,s.service_id,s.date,s.demo,s.capacity);
        (void)inserted;return it->second;
    }
    json public_journey(const JourneyState& j) const {
        json seats=json::array();int booked=0;
        for(int n=1;n<=j.capacity;++n) {
            const bool taken=!j.seats[n-1].empty();if(taken)++booked;
            seats.push_back({{"number",n},{"status",taken?"Booked":"Available"}});
        }
        return {{"service",service(j.service_id)},{"date",j.date},{"demo",j.demo},{"seats",seats},{"counts",{{"available",j.capacity-booked},{"booked",booked},{"waiting",j.waiting.size()}}}};
    }
    int position(const JourneyState& j,const Booking& b) const {
        if(b.status!="Waiting")return 0;
        int number=1;
        for(const auto& ref:j.waiting.values()){if(ref==b.reference)return number;++number;}
        throw std::logic_error("Waiting booking is absent from FIFO");
    }
    json ticket_json(const JourneyState& j,const Booking& b) const {
        json seat=nullptr,queue=nullptr;
        if(b.status=="Confirmed")seat=b.seat;
        if(b.status=="Waiting")queue=position(j,b);
        return {{"reference",b.reference},{"passenger_id",b.id},{"name",b.name},{"age",b.age},{"gender",b.gender},{"contact",b.contact},{"service",service(j.service_id)},{"date",j.date},{"demo",j.demo},{"status",b.status},{"seat_number",seat},{"queue_position",queue},{"fare",service(j.service_id)["fare"]}};
    }
    json serialize(const State& target) const {
        json journeys=json::array();
        for(const auto& [unused,j]:target.journeys) {
            (void)unused;json records=json::array();
            j.records.for_each([&](const Booking& b){records.push_back({{"reference",b.reference},{"passenger_id",b.id},{"name",b.name},{"age",b.age},{"gender",b.gender},{"contact",b.contact},{"status",b.status},{"seat_number",b.seat?json(b.seat):json(nullptr)}});});
            journeys.push_back({{"service_id",j.service_id},{"date",j.date},{"demo",j.demo},{"records",records},{"waiting",j.waiting.values()}});
        }
        return {{"schema_version",1},{"next_id",target.next_id},{"demo_date",target.demo_date},{"journeys",journeys}};
    }
    void commit(State&& candidate) {
        // All public methods hold the mutex. A deep candidate is saved before it
        // replaces the live state, so failed writes leave seats, FIFO and IDs intact.
        if(!storage.empty())atomic_write(storage,serialize(candidate).dump(2));
        state=std::move(candidate);
    }
    void load() {
        if(storage.empty())return;
        try {
            if(!std::filesystem::exists(storage))return;
            if(!std::filesystem::is_regular_file(storage) || std::filesystem::file_size(storage)>32*1024*1024)throw std::runtime_error("invalid storage file");
            std::ifstream file(storage,std::ios::binary);
            if(!file)throw std::runtime_error("open failed");
            json document;file>>document;file>>std::ws;
            if(!file.eof())throw std::runtime_error("trailing data");
            if(!document.is_object() || !document.at("schema_version").is_number_integer() || document.at("schema_version")!=1 || !document.at("next_id").is_number_unsigned() || document.at("next_id").get<std::uint64_t>()==0 || !document.at("journeys").is_array())throw std::runtime_error("invalid schema");
            State loaded;loaded.next_id=document.at("next_id").get<std::uint64_t>();
            loaded.demo_date=text(document,"demo_date");parse_date(loaded.demo_date);
            std::set<std::string> references,ids;std::uint64_t highest_id=0;
            for(const auto& entry:document.at("journeys")) {
                object(entry);const auto id=text(entry,"service_id");const auto& s=service(id);
                const auto date=text(entry,"date");parse_date(date);
                if(!entry.contains("demo") || !entry["demo"].is_boolean())throw std::runtime_error("missing namespace");
                const bool demo=boolean(entry,"demo");
                if(demo && (id!="GJ01" || date!=loaded.demo_date))throw std::runtime_error("invalid demo namespace");
                JourneyState j(id,date,demo,s["capacity"].get<int>());const auto k=journey_key(id,date,demo);
                if(loaded.journeys.count(k) || !entry.at("records").is_array() || !entry.at("waiting").is_array())throw std::runtime_error("invalid journey");
                std::set<std::string> active,expected_waiting;
                for(const auto& record:entry.at("records")) {
                    Booking b;b.reference=text(record,"reference");b.id=text(record,"passenger_id");b.name=passenger_name(text(record,"name"));b.age=integer(record,"age",1,120);b.gender=gender(text(record,"gender"));b.contact=contact(text(record,"contact"));b.status=text(record,"status");
                    if(b.name!=record["name"].get<std::string>() || b.contact!=record["contact"].get<std::string>() || b.reference.size()!=67 || b.reference.substr(0,3)!="JD-" || !std::all_of(b.reference.begin()+3,b.reference.end(),[](unsigned char c){return std::isxdigit(c)!=0;}) || !references.insert(b.reference).second)throw std::runtime_error("invalid reference or passenger");
                    if(!ids.insert(b.id).second)throw std::runtime_error("duplicate passenger identifier");
                    if(!demo || b.id.size()>=7) {
                        if(b.id.size()<7 || b.id[0]!='P' || !std::all_of(b.id.begin()+1,b.id.end(),[](unsigned char c){return c>='0' && c<='9';}))throw std::runtime_error("invalid passenger identifier");
                        const auto number=std::stoull(b.id.substr(1));
                        if(number==0)throw std::runtime_error("invalid passenger identifier");
                        highest_id=std::max(highest_id,static_cast<std::uint64_t>(number));
                    } else if(b.id.size()<2 || b.id[0]!='P' || !std::all_of(b.id.begin()+1,b.id.end(),[](unsigned char c){return c>='0' && c<='9';}))throw std::runtime_error("invalid demo passenger identifier");
                    if(b.status!="Confirmed" && b.status!="Waiting" && b.status!="Cancelled")throw std::runtime_error("invalid status");
                    if(b.status!="Cancelled" && !active.insert(lower(b.name)+"|"+b.contact).second)throw std::runtime_error("duplicate active passenger");
                    if(b.status=="Confirmed") {
                        b.seat=integer(record,"seat_number",1,j.capacity);
                        if(!j.seats[b.seat-1].empty() || !j.free_seats.remove(b.seat))throw std::runtime_error("duplicate seat");
                        j.seats[b.seat-1]=b.reference;
                    } else {
                        if(!record.contains("seat_number") || !record["seat_number"].is_null())throw std::runtime_error("inactive record has seat");
                        if(b.status=="Waiting")expected_waiting.insert(b.reference);
                    }
                    j.records.push_back(b);
                }
                for(const auto& ref:entry.at("waiting")) {
                    if(!ref.is_string() || !expected_waiting.erase(ref.get<std::string>()))throw std::runtime_error("invalid queue");
                    j.waiting.push(ref.get<std::string>());
                }
                if(!expected_waiting.empty() || (!j.waiting.empty() && !j.free_seats.empty()))throw std::runtime_error("inconsistent FIFO or seat counts");
                loaded.journeys.emplace(k,std::move(j));
            }
            if(loaded.next_id<=highest_id || loaded.next_id==std::numeric_limits<std::uint64_t>::max())throw std::runtime_error("invalid identifier counter");
            state=std::move(loaded);
        } catch(...) {throw AppError("Stored reservations could not be loaded. The original file was preserved.",503);}
    }
};

ReservationEngine::ReservationEngine(std::filesystem::path storage_path,std::string today_override):impl_(std::make_unique<Impl>()) {
    impl_->storage=std::move(storage_path);impl_->today_override=std::move(today_override);
    impl_->state.demo_date=date_string(impl_->today()+std::chrono::days{1});impl_->load();
}
ReservationEngine::~ReservationEngine()=default;
json ReservationEngine::catalog() const {
    const std::lock_guard guard(impl_->mutex);const auto today=impl_->today();
    return {{"cities",{"Ahmedabad","Mumbai","Surat","Pune"}},{"services",impl_->services},{"today",date_string(today)},{"default_date",date_string(today+std::chrono::days{1})},{"max_date",date_string(today+std::chrono::days{365})},{"demo_service","GJ01"},{"demo_date",impl_->state.demo_date},{"backend","C++20"}};
}
json ReservationEngine::search(const json& input) const {
    const std::lock_guard guard(impl_->mutex);object(input);
    const auto from=text(input,"source"),to=text(input,"destination"),mode=text(input,"mode"),date=text(input,"date");const auto day=parse_date(date);
    const std::set<std::string> cities{"Ahmedabad","Mumbai","Surat","Pune"};
    if(!cities.count(from))invalid("source","Choose a listed departure city.");
    if(!cities.count(to))invalid("destination","Choose a listed destination city.");
    if(from==to)invalid("destination","Departure and destination must be different.");
    if(mode!="Bus" && mode!="Train")invalid("mode","Choose Bus or Train.");
    if(day<impl_->today() || day>impl_->today()+std::chrono::days{365})invalid("date","Search dates must be between today and 365 days ahead.");
    json results=json::array();std::vector<json> matches;
    for(const auto& s:impl_->services)if(s["source"]==from && s["destination"]==to && s["mode"]==mode)matches.push_back(s);
    std::sort(matches.begin(),matches.end(),[](const json& a,const json& b){return a["departure"].get<std::string>()<b["departure"].get<std::string>();});
    for(const auto& s:matches) {
        const auto selection=impl_->select({{"service_id",s["id"]},{"date",date}},true);
        const auto found=impl_->state.journeys.find(selection.key);
        const auto counts=impl_->public_journey(found==impl_->state.journeys.end()?impl_->empty(selection):found->second)["counts"];
        results.push_back({{"service",s},{"available",counts["available"]},{"booked",counts["booked"]},{"waiting",counts["waiting"]}});
    }
    return {{"source",from},{"destination",to},{"date",date},{"mode",mode},{"results",results}};
}
json ReservationEngine::journey(const json& input) const {
    const std::lock_guard guard(impl_->mutex);const auto selection=impl_->select(input);const auto it=impl_->state.journeys.find(selection.key);
    return impl_->public_journey(it==impl_->state.journeys.end()?impl_->empty(selection):it->second);
}
json ReservationEngine::book(const json& input) {
    const std::lock_guard guard(impl_->mutex);object(input);
    if(input.contains("_desk_authorized"))invalid("request","Internal authorization fields are not accepted for booking.");
    const auto selection=impl_->select(input,true);
    if(!input.contains("passenger") || !input["passenger"].is_object())invalid("passenger","Enter passenger details.");
    const auto& p=input["passenger"];
    Booking b;b.name=passenger_name(text(p,"name"));b.age=integer(p,"age",1,120);b.gender=gender(text(p,"gender"));b.contact=contact(text(p,"contact"));
    int preferred=0;
    if(input.contains("seat_number") && !input["seat_number"].is_null())preferred=integer(input,"seat_number",1,selection.capacity);
    State candidate=impl_->state;auto& j=impl_->ensure(candidate,selection);
    if(j.records.find([&](const Booking& other){return other.status!="Cancelled" && lower(other.name)==lower(b.name) && other.contact==b.contact;}))throw AppError("This passenger already has an active reservation for this journey.",409);
    if(preferred && !j.seats[preferred-1].empty())throw AppError("That seat has already been booked. Choose another seat.",409,{{"seat_number","Seat is already booked."}});
    b.reference=random_token();
    if(candidate.next_id==std::numeric_limits<std::uint64_t>::max())throw AppError("Passenger identifiers are temporarily unavailable.",503);
    std::ostringstream id;id<<'P'<<std::setw(6)<<std::setfill('0')<<candidate.next_id++;b.id=id.str();
    if(!j.free_seats.empty()) {
        b.seat=preferred?preferred:j.free_seats.pop();
        if(preferred)j.free_seats.remove(preferred);
        b.status="Confirmed";j.seats[b.seat-1]=b.reference;
    } else {b.status="Waiting";j.waiting.push(b.reference);}
    j.records.push_back(b);
    const auto response=json{{"message",b.status=="Confirmed"?"Your seat is confirmed.":"Added to the waiting queue."},{"ticket",impl_->ticket_json(j,b)},{"journey",impl_->public_journey(j)}};
    impl_->commit(std::move(candidate));return response;
}
json ReservationEngine::ticket(const json& input) const {
    const std::lock_guard guard(impl_->mutex);object(input);std::string reference,number;
    try {reference=text(input,"reference");number=contact(text(input,"contact"));}
    catch(const AppError&){throw AppError("Ticket not found. Check the reference and contact number.",404);}
    for(const auto& [unused,j]:impl_->state.journeys) {
        (void)unused;const auto* b=j.find(reference);
        if(b && b->contact==number)return {{"ticket",impl_->ticket_json(j,*b)}};
    }
    throw AppError("Ticket not found. Check the reference and contact number.",404);
}
json ReservationEngine::cancel(const json& input) {
    const std::lock_guard guard(impl_->mutex);object(input);const bool desk=trusted_desk(input);std::string reference,number;
    try {reference=text(input,"reference");if(!desk)number=contact(text(input,"contact"));}
    catch(const AppError&){throw AppError("Ticket not found. Check the reference and contact number.",404);}
    std::string target_key;
    if(desk)target_key=impl_->select(input).key;
    State candidate=impl_->state;
    for(auto& [k,j]:candidate.journeys) {
        if(desk && k!=target_key)continue;
        auto* b=j.find(reference);
        if(!b || (!desk && b->contact!=number))continue;
        if(b->status=="Cancelled")throw AppError("This ticket has already been cancelled.",409);
        json promoted=nullptr;
        if(b->status=="Waiting") {
            if(!j.waiting.remove(reference))throw std::logic_error("Waiting membership missing");
        } else {
            const int seat=b->seat;j.seats[seat-1].clear();
            if(!j.waiting.empty()) {
                auto* next=j.find(j.waiting.pop());
                if(!next || next->status!="Waiting")throw std::logic_error("Waiting booking missing");
                next->seat=seat;next->status="Confirmed";j.seats[seat-1]=next->reference;
                promoted={{"seat_number",seat}};
                if(j.demo){promoted["passenger_id"]=next->id;promoted["name"]=next->name;}
            } else j.free_seats.push(seat);
        }
        b->status="Cancelled";b->seat=0;
        const auto response=json{{"message","Reservation cancelled."},{"cancelled",impl_->ticket_json(j,*b)},{"promoted",promoted},{"journey",impl_->public_journey(j)}};
        impl_->commit(std::move(candidate));return response;
    }
    throw AppError("Ticket not found. Check the reference and contact number.",404);
}
json ReservationEngine::manifest(const json& input) const {
    const std::lock_guard guard(impl_->mutex);const auto selection=impl_->select(input);
    if(!selection.demo && !trusted_desk(input))throw AppError("A valid reservation desk access key is required.",401);
    std::string query;
    if(input.contains("q"))query=lower(trim(text(input,"q")));
    const auto it=impl_->state.journeys.find(selection.key);const auto empty=impl_->empty(selection);
    const auto& j=it==impl_->state.journeys.end()?empty:it->second;
    auto matches=[&](const Booking& b){return query.empty() || lower(b.id).find(query)!=std::string::npos || lower(b.name).find(query)!=std::string::npos || lower(b.reference).find(query)!=std::string::npos;};
    json passengers=json::array(),waiting=json::array();
    for(int n=1;n<=j.capacity;++n)if(!j.seats[n-1].empty()) {
        const auto* b=j.find(j.seats[n-1]);
        if(b && matches(*b))passengers.push_back(impl_->ticket_json(j,*b));
    }
    for(const auto& ref:j.waiting.values()) {
        const auto* b=j.find(ref);
        if(b && matches(*b))waiting.push_back(impl_->ticket_json(j,*b));
    }
    return {{"journey",impl_->public_journey(j)},{"passengers",passengers},{"waiting",waiting}};
}
json ReservationEngine::load_demo(const json& input) {
    const std::lock_guard guard(impl_->mutex);object(input);const bool reset=boolean(input,"reset");
    const auto selection=impl_->select({{"service_id","GJ01"},{"date",impl_->state.demo_date},{"demo",true}});
    const auto existing=impl_->state.journeys.find(selection.key);
    if(existing!=impl_->state.journeys.end() && !reset)return {{"service_id","GJ01"},{"date",selection.date},{"demo",true},{"journey",impl_->public_journey(existing->second)}};
    State candidate=impl_->state;candidate.journeys.erase(selection.key);auto& j=impl_->ensure(candidate,selection);
    const std::array<std::string,12> names{"Rahul Shah","Priya Patel","Amit Shah","Kavya Desai","Rohan Mehta","Meera Joshi","Vikram Rao","Sana Khan","Dev Shah","Isha Patel","Arjun Desai","Anaya Mehta"};
    const std::array<int,12> seats{1,2,4,3,5,6,7,8,9,10,11,12};
    const std::array<std::string,12> ids{"P101","P102","P103","P104","P105","P106","P107","P111","P112","P113","P114","P115"};
    for(std::size_t i=0;i<names.size();++i) {
        Booking b;b.reference=random_token();b.id=ids[i];b.name=names[i];b.age=25+static_cast<int>(i);b.gender=i%2?"Female":"Male";
        b.contact="90000000"+(i<10?std::string("0"):std::string())+std::to_string(i);b.status="Confirmed";b.seat=seats[i];
        j.seats[b.seat-1]=b.reference;j.free_seats.remove(b.seat);j.records.push_back(b);
    }
    const std::array<std::string,3> waiting_names{"Neha Patel","Raj Kumar","Amit Patel"};
    for(int i=0;i<3;++i) {
        Booking b;b.reference=random_token();b.id="P"+std::to_string(108+i);b.name=waiting_names[i];b.age=27+i;b.gender=i==0?"Female":"Male";
        b.contact="900000002"+std::to_string(i);b.status="Waiting";j.records.push_back(b);j.waiting.push(b.reference);
    }
    const auto response=json{{"service_id","GJ01"},{"date",selection.date},{"demo",true},{"journey",impl_->public_journey(j)}};
    impl_->commit(std::move(candidate));return response;
}
}
