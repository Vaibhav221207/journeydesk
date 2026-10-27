#include "desk_key.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace {
int passed=0,failed=0;
void require(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
void test(const std::string& name,const std::function<void()>& body) {
    try {body();++passed;std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception& error){++failed;std::cout<<"FAIL "<<name<<": "<<error.what()<<'\n';}
}
void rejected(const std::function<void()>& body,const std::string& private_value={}) {
    try {body();}catch(const std::exception& error) {
        if(!private_value.empty())require(std::string(error.what()).find(private_value)==std::string::npos,"exception disclosed a private key");
        return;
    }
    throw std::runtime_error("Expected key resolution to fail closed");
}
struct Temp {
    fs::path dir;
    Temp() {
        static std::atomic<int> counter{0};
        dir=fs::temp_directory_path()/("journey-key-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(counter++));
        fs::create_directories(dir);
    }
    ~Temp(){std::error_code error;fs::remove_all(dir,error);}
};
std::string bytes(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(file)),{});
}
void fixture(const fs::path& path,const std::string& value) {
    std::ofstream file(path,std::ios::binary);file.write(value.data(),static_cast<std::streamsize>(value.size()));file.close();
#ifndef _WIN32
    fs::permissions(path,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace);
#endif
}
void incompatible_in_both_sources(const std::string& value) {
    Temp temp;const auto storage=temp.dir/"bookings.json",path=temp.dir/"desk.key";
    rejected([&]{journey::resolve_desk_key(storage,value);},value);
    require(!fs::exists(path),"incompatible override created a local key");
    fixture(path,value);rejected([&]{journey::resolve_desk_key(storage);},value);
    require(bytes(path)==value,"incompatible private key was trimmed or rotated");
}
}

int main() {
    // Break caught: generating a key on each resolution prevents desk access after restart.
    test("new private key is strong hex and reused after reservation file creation",[] {
        Temp temp;const auto storage=temp.dir/"bookings.json";const auto first=journey::resolve_desk_key(storage);
        require(first.size()==32 && first.find_first_not_of("0123456789abcdefABCDEF")==std::string::npos,"generated key is not 32 hexadecimal characters");
        fixture(storage,"{}");const auto again=journey::resolve_desk_key(storage);
        require(again==first && fs::is_regular_file(temp.dir/"desk.key"),"restart changed local desk key");
    });
    // Break caught: an environment override must not create directories or rotate local files.
    test("explicit override wins before any local filesystem access",[] {
        Temp temp;const auto blocked=temp.dir/"blocked";fixture(blocked,"file blocks directory creation");
        const std::string override_key="fixture-override-for-tests";
        require(journey::resolve_desk_key(blocked/"bookings.json",override_key)==override_key,"explicit override was ignored");
        require(!fs::exists(temp.dir/"desk.key") && bytes(blocked)=="file blocks directory creation","override touched local storage");
    });
    test("override preserves existing local key bytes and later local reuse",[] {
        Temp temp;const auto path=temp.dir/"desk.key";const std::string existing="fixture-existing-local-key";fixture(path,existing+"\n");
        const auto old_time=fs::file_time_type::clock::now()-std::chrono::hours{24};fs::last_write_time(path,old_time);const auto recorded_time=fs::last_write_time(path);
        require(journey::resolve_desk_key(temp.dir/"bookings.json","fixture-temporary-override")=="fixture-temporary-override","override not selected");
        require(journey::resolve_desk_key(temp.dir/"bookings.json")==existing,"existing key was not reused");
        require(bytes(path)==existing+"\n" && fs::last_write_time(path)==recorded_time,"resolver rewrote an existing key");
    });
    test("user written header safe key permits one newline ending without altering its value",[] {
        Temp temp;const auto path=temp.dir/"desk.key";const std::string value="Fixture_User-Key.123~";
        for(const auto& ending:{"\n","\r\n"}) {
            fixture(path,value+ending);require(journey::resolve_desk_key(temp.dir/"bookings.json")==value,"resolver changed user key characters");
        }
    });
    // Break caught: UI trimming or header whitespace normalization must not change an accepted key.
    test("whitespace key values are rejected in overrides and preserved private files",[] {
        for(const auto& value:{" fixture-header-key","fixture-header-key ","fixture header key"})incompatible_in_both_sources(value);
    });
    // Break caught: browser headers cannot transport arbitrary Unicode credentials unchanged.
    test("Unicode key values are rejected in overrides and preserved private files",[] {
        incompatible_in_both_sources("fixture-\xe2\x98\x83-key");
    });
    // Break caught: percent decoding must never transform an accepted credential into another value.
    test("percent encoded key values are rejected in overrides and preserved private files",[] {
        incompatible_in_both_sources("fixture%2Dencoded%2Dkey");
    });
    // Break caught: invalid overrides otherwise become insecure keys or multiline log content.
    test("short and control containing overrides are rejected without disclosing values",[] {
        Temp temp;const std::vector<std::string> invalid{"short","fixture-secret\nsecond-line","fixture-secret\tvalue",std::string("fixture-secret\0value",20),"fixture-secret\x7fvalue"};
        for(const auto& value:invalid)rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json",value);},value);
        require(!fs::exists(temp.dir/"desk.key"),"invalid override created a private key file");
    });
    // Break caught: regenerating unreadable/corrupt keys silently changes access credentials.
    test("corrupt existing keys are preserved rather than regenerated",[] {
        Temp temp;const auto path=temp.dir/"desk.key";
        const std::vector<std::string> invalid{"","short","fixture-secret\nsecond-line","fixture-secret\n\n",std::string("\xef\xbb\xbf")+"fixture-user-key","fixture-secret\tvalue"};
        for(const auto& value:invalid) {
            fixture(path,value);rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");},value);
            require(bytes(path)==value,"corrupt key was overwritten");
        }
    });
    // Break caught: an oversized private file must fail without consuming or replacing all its contents.
    test("oversized key file is rejected and preserved while 256 byte keys accept newline endings",[] {
        Temp temp;const auto path=temp.dir/"desk.key";const std::string oversized(1024,'A');fixture(path,oversized);
        rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");},oversized);
        require(bytes(path)==oversized,"oversized key file was changed");
        const std::string maximum(256,'B');
        for(const auto& ending:{"","\n","\r\n"}) {
            fixture(path,maximum+ending);require(journey::resolve_desk_key(temp.dir/"bookings.json")==maximum,"256 byte key or supported ending was rejected");
        }
        fixture(path,std::string(257,'C')+"\n");rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");});
        require(bytes(path)==std::string(257,'C')+"\n","257 byte key was rotated");
    });
    // Break caught: environment overrides must enforce the same byte limit before touching the filesystem.
    test("oversized environment override is rejected without filesystem mutation",[] {
        Temp temp;const auto storage=temp.dir/"missing"/"bookings.json";const std::string oversized(257,'A');
        rejected([&]{journey::resolve_desk_key(storage,oversized);},oversized);
        require(!fs::exists(storage.parent_path()),"oversized override created local storage");
        const std::string maximum(256,'B');require(journey::resolve_desk_key(storage,maximum)==maximum,"256 byte override was rejected");
        require(!fs::exists(storage.parent_path()),"valid override touched local storage");
    });
    test("a directory at desk key path is preserved and rejected",[] {
        Temp temp;fs::create_directory(temp.dir/"desk.key");fixture(temp.dir/"desk.key"/"marker","keep directory");
        rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");});
        require(bytes(temp.dir/"desk.key"/"marker")=="keep directory","nonregular path was changed");
    });
    test("missing reservation parent directories are created once",[] {
        Temp temp;const auto storage=temp.dir/"nested"/"private"/"bookings.json";const auto first=journey::resolve_desk_key(storage);
        require(fs::is_regular_file(storage.parent_path()/"desk.key") && journey::resolve_desk_key(storage)==first,"missing parent key was not persisted");
        require(!fs::exists(storage),"key resolver created reservation data");
    });
    test("failed private file creation does not fall back to an ephemeral key",[] {
        Temp temp;const auto blocked=temp.dir/"blocked";fixture(blocked,"preserve blocker");
        rejected([&]{journey::resolve_desk_key(blocked/"bookings.json");});
        require(bytes(blocked)=="preserve blocker" && !fs::exists(temp.dir/"desk.key"),"failed creation changed storage or used fallback");
    });
    test("separate reservation directories receive independent stable keys",[] {
        Temp temp;const auto a=temp.dir/"a"/"bookings.json",b=temp.dir/"b"/"bookings.json";
        const auto first=journey::resolve_desk_key(a),second=journey::resolve_desk_key(b);
        require(first!=second && journey::resolve_desk_key(a)==first && journey::resolve_desk_key(b)==second,"separate data directories shared or rotated a key");
    });
    test("reservation files in one directory share that directory's desk key",[] {
        Temp temp;const auto first=journey::resolve_desk_key(temp.dir/"one.json");
        require(journey::resolve_desk_key(temp.dir/"two.json")==first,"same directory created another desk key");
    });
    // Break caught: competing creators must never overwrite each other's winning key.
    test("concurrent creators all resolve the same exclusively created key",[] {
        Temp temp;const auto storage=temp.dir/"concurrent"/"bookings.json";std::atomic<int> failures{0};std::mutex mutex;std::vector<std::string> keys;std::vector<std::thread> threads;
        for(int i=0;i<16;++i)threads.emplace_back([&]{try {const auto key=journey::resolve_desk_key(storage);const std::lock_guard guard(mutex);keys.push_back(key);}catch(...){++failures;}});
        for(auto& thread:threads)thread.join();
        require(failures==0 && keys.size()==16,"concurrent creation failed");
        for(const auto& key:keys)require(key==keys.front(),"concurrent creator returned a different key");
        require(journey::resolve_desk_key(storage)==keys.front(),"concurrent key was not saved");
    });
    test("unreadable existing key fails closed and is preserved",[] {
        Temp temp;const auto path=temp.dir/"desk.key";const std::string existing="fixture-locked-existing-key";fixture(path,existing);
#ifdef _WIN32
        HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(locked!=INVALID_HANDLE_VALUE,"could not lock private key fixture");
        try {rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");});}
        catch(...){CloseHandle(locked);throw;}
        CloseHandle(locked);
#else
        fs::permissions(path,fs::perms::none,fs::perm_options::replace);
        try {rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");});}
        catch(...){fs::permissions(path,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace);throw;}
        fs::permissions(path,fs::perms::owner_read|fs::perms::owner_write,fs::perm_options::replace);
#endif
        require(bytes(path)==existing,"unreadable private key was rotated");
    });
    test("relative reservation filename uses the current directory",[] {
        Temp temp;const auto original=fs::current_path();fs::current_path(temp.dir);
        try {
            const auto key=journey::resolve_desk_key("bookings.json");
            require(fs::is_regular_file(temp.dir/"desk.key") && journey::resolve_desk_key("other.json")==key,"relative data filename chose wrong key directory");
        }catch(...){fs::current_path(original);throw;}
        fs::current_path(original);
    });
#ifndef _WIN32
    test("created POSIX key is readable and writable only by its owner",[] {
        Temp temp;journey::resolve_desk_key(temp.dir/"bookings.json");struct stat info{};
        require(::stat((temp.dir/"desk.key").c_str(),&info)==0 && (info.st_mode&0777)==0600,"private key permissions are not 0600");
    });
    test("symlink key paths are rejected without changing their target",[] {
        Temp temp;const auto target=temp.dir/"target";fixture(target,"fixture-symlink-target-key");fs::create_symlink(target,temp.dir/"desk.key");
        rejected([&]{journey::resolve_desk_key(temp.dir/"bookings.json");});
        require(fs::is_symlink(temp.dir/"desk.key") && bytes(target)=="fixture-symlink-target-key","resolver followed or replaced symlink key");
    });
#endif
    std::cout<<passed<<" passed, "<<failed<<" failed\n";return failed?1:0;
}
