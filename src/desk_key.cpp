#include "desk_key.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace journey {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t max_key_bytes=256;
constexpr std::size_t max_key_file_bytes=max_key_bytes+2; // Optional CRLF.
[[noreturn]] void unavailable() {
    throw std::runtime_error("Could not read or create the private reservation desk key. Check the data directory and desk.key file; existing credentials were preserved.");
}

bool valid_key(const std::string& value) {
    // Restrict credentials to characters transmitted unchanged by the browser
    // and HTTP header parser. Do not normalize a user's existing credential.
    if(value.size()<12 || value.size()>max_key_bytes)return false;
    for(const unsigned char character:value) {
        const bool letter=(character>='A' && character<='Z') || (character>='a' && character<='z');
        const bool digit=character>='0' && character<='9';
        if(!letter && !digit && character!='.' && character!='_' && character!='~' && character!='-')return false;
    }
    return true;
}

std::string new_key() {
    std::array<unsigned char,16> bytes{};
#ifdef _WIN32
    HMODULE library=LoadLibraryW(L"bcrypt.dll");
    using Generate = LONG (WINAPI*)(void*,unsigned char*,unsigned long,unsigned long);
    const auto address=library?GetProcAddress(library,"BCryptGenRandom"):nullptr;
    Generate generate=nullptr;
    static_assert(sizeof(generate)==sizeof(address));
    std::memcpy(&generate,&address,sizeof(generate));
    const bool ok=generate && generate(nullptr,bytes.data(),static_cast<unsigned long>(bytes.size()),2)==0;
    if(library)FreeLibrary(library);
    if(!ok)unavailable();
#else
    std::size_t used=0;
    while(used<bytes.size()) {
        const auto count=::getrandom(bytes.data()+used,bytes.size()-used,0);
        if(count<0 && errno==EINTR)continue;
        if(count<=0)unavailable();
        used+=static_cast<std::size_t>(count);
    }
#endif
    static constexpr char hex[]="0123456789abcdef";
    std::string result;result.reserve(32);
    for(const auto byte:bytes){result.push_back(hex[byte>>4]);result.push_back(hex[byte&15]);}
    return result;
}

enum class ReadState {Missing,Busy,Ready};
struct ReadResult {ReadState state;std::string bytes;};
#ifdef _WIN32
struct File {
    HANDLE handle;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
};
ReadResult read_existing(const fs::path& path) {
    File file{CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr)};
    if(file.handle==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)return {ReadState::Missing,{}};
        if(error==ERROR_SHARING_VIOLATION || error==ERROR_LOCK_VIOLATION)return {ReadState::Busy,{}};
        unavailable();
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if(GetFileType(file.handle)!=FILE_TYPE_DISK || !GetFileInformationByHandle(file.handle,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))unavailable();
    std::string contents;std::array<char,max_key_file_bytes+1> buffer{};
    for(;;) {
        DWORD count=0;
        const auto remaining=buffer.size()-contents.size();
        if(!ReadFile(file.handle,buffer.data(),static_cast<DWORD>(remaining),&count,nullptr))unavailable();
        if(count==0)break;
        contents.append(buffer.data(),count);
        if(contents.size()>max_key_file_bytes)unavailable();
    }
    return {ReadState::Ready,std::move(contents)};
}
bool create_exclusive(const fs::path& path,const std::string& key) {
    File file{CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr)};
    if(file.handle==INVALID_HANDLE_VALUE) {
        const auto error=GetLastError();
        if(error==ERROR_FILE_EXISTS || error==ERROR_ALREADY_EXISTS || error==ERROR_SHARING_VIOLATION)return false;
        unavailable();
    }
    const auto contents=key+"\n";std::size_t written=0;
    while(written<contents.size()) {
        DWORD count=0;
        if(!WriteFile(file.handle,contents.data()+written,static_cast<DWORD>(contents.size()-written),&count,nullptr) || count==0)unavailable();
        written+=count;
    }
    if(!FlushFileBuffers(file.handle))unavailable();
    return true;
}
#else
struct File {
    int descriptor;
    ~File(){if(descriptor>=0)::close(descriptor);}
};
ReadResult read_existing(const fs::path& path) {
    // O_NOFOLLOW rejects final-component symlinks. O_NONBLOCK prevents a FIFO
    // supplied as desk.key from blocking startup before fstat can reject it.
    File file{::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK)};
    if(file.descriptor<0) {
        if(errno==ENOENT)return {ReadState::Missing,{}};
        unavailable();
    }
    struct stat info{};
    if(::fstat(file.descriptor,&info)!=0 || !S_ISREG(info.st_mode) || !(info.st_mode&S_IRUSR) ||
        (info.st_mode&(S_IXUSR|S_IRWXG|S_IRWXO)))unavailable();
    std::string contents;std::array<char,max_key_file_bytes+1> buffer{};
    for(;;) {
        const auto remaining=buffer.size()-contents.size();
        const auto count=::read(file.descriptor,buffer.data(),remaining);
        if(count<0 && errno==EINTR)continue;
        if(count<0)unavailable();
        if(count==0)break;
        contents.append(buffer.data(),static_cast<std::size_t>(count));
        if(contents.size()>max_key_file_bytes)unavailable();
    }
    return {ReadState::Ready,std::move(contents)};
}
bool create_exclusive(const fs::path& path,const std::string& key) {
    File file{::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600)};
    if(file.descriptor<0) {
        if(errno==EEXIST)return false;
        unavailable();
    }
    if(::fchmod(file.descriptor,0600)!=0)unavailable();
    const auto contents=key+"\n";std::size_t written=0;
    while(written<contents.size()) {
        const auto count=::write(file.descriptor,contents.data()+written,contents.size()-written);
        if(count<0 && errno==EINTR)continue;
        if(count<=0)unavailable();
        written+=static_cast<std::size_t>(count);
    }
    if(::fsync(file.descriptor)!=0)unavailable();
    return true;
}
#endif
}

std::string resolve_desk_key(std::filesystem::path storage,std::string override_key) {
    if(!override_key.empty()) {
        if(!valid_key(override_key))throw std::invalid_argument("Reservation desk key must contain 12 to 256 ASCII letters, digits, or . _ ~ - characters.");
        return override_key;
    }
    const auto parent=storage.parent_path().empty()?fs::current_path():storage.parent_path();
    const auto path=parent/"desk.key";
    // A competing creator can hold the file open briefly, or expose an empty
    // newly created file before its first write. Retry reads without overwriting.
    for(int attempt=0;attempt<20;++attempt) {
        auto result=read_existing(path);
        if(result.state==ReadState::Ready) {
            if(!result.bytes.empty() && result.bytes.back()=='\n') {
                result.bytes.pop_back();
                if(!result.bytes.empty() && result.bytes.back()=='\r')result.bytes.pop_back();
            }
            if(valid_key(result.bytes))return result.bytes;
            if(result.bytes.size()>=12)unavailable();
        } else if(result.state==ReadState::Missing) {
            std::error_code error;
            fs::create_directories(parent,error);
            if(error)unavailable();
            const auto generated=new_key();
            if(create_exclusive(path,generated))return generated;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    unavailable();
}
}
