// platform_stub.cpp: the melonDS Platform:: interface for a headless test harness.
// Files go through stdio, threads/semaphores/mutexes through the C++ standard library, everything
// else (saves, network, camera, microphone, AAC, addons) is a no-op.
// Copyright 2026 ROCKNIXDS contributors. GPL-3.0-or-later, like melonDS.

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "Platform.h"

namespace melonDS::Platform
{

void SignalStop(StopReason reason, void* userdata) {}

std::string GetLocalFilePath(const std::string& filename) { return filename; }

struct FileHandle { FILE* f; };

static FileHandle* Wrap(FILE* f)
{
    if (!f) return nullptr;
    return new FileHandle{f};
}

FileHandle* OpenFile(const std::string& path, FileMode mode)
{
    bool read = mode & Read, write = mode & Write, preserve = mode & Preserve;
    bool nocreate = mode & NoCreate, text = mode & Text, append = mode & Append;
    std::string m;
    if (append) m = read ? "a+" : "a";
    else if (write && read) m = preserve ? "r+" : "w+";
    else if (write) m = preserve ? "r+" : "w";
    else m = "r";
    if (!text) m += "b";
    FILE* f = fopen(path.c_str(), m.c_str());
    if (!f && write && preserve && !nocreate)
        f = fopen(path.c_str(), text ? "w+" : "w+b");
    return Wrap(f);
}

FileHandle* OpenLocalFile(const std::string& path, FileMode mode) { return OpenFile(path, mode); }

bool FileExists(const std::string& name)
{
    FILE* f = fopen(name.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

bool LocalFileExists(const std::string& name) { return FileExists(name); }

bool CheckFileWritable(const std::string& filepath)
{
    FILE* f = fopen(filepath.c_str(), "ab");
    if (!f) return false;
    fclose(f);
    return true;
}

bool CheckLocalFileWritable(const std::string& filepath) { return CheckFileWritable(filepath); }

bool CloseFile(FileHandle* file)
{
    if (!file) return false;
    bool ok = fclose(file->f) == 0;
    delete file;
    return ok;
}

bool IsEndOfFile(FileHandle* file) { return feof(file->f) != 0; }
bool FileReadLine(char* str, int count, FileHandle* file) { return fgets(str, count, file->f) != nullptr; }
u64 FilePosition(FileHandle* file) { return (u64)ftell(file->f); }

bool FileSeek(FileHandle* file, s64 offset, FileSeekOrigin origin)
{
    int o = origin == FileSeekOrigin::Start ? SEEK_SET : origin == FileSeekOrigin::Current ? SEEK_CUR : SEEK_END;
    return fseek(file->f, (long)offset, o) == 0;
}

void FileRewind(FileHandle* file) { rewind(file->f); }
u64 FileRead(void* data, u64 size, u64 count, FileHandle* file) { return fread(data, size, count, file->f); }
bool FileFlush(FileHandle* file) { return fflush(file->f) == 0; }
u64 FileWrite(const void* data, u64 size, u64 count, FileHandle* file) { return fwrite(data, size, count, file->f); }

u64 FileWriteFormatted(FileHandle* file, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int n = vfprintf(file->f, fmt, args);
    va_end(args);
    return n < 0 ? 0 : (u64)n;
}

u64 FileLength(FileHandle* file)
{
    long pos = ftell(file->f);
    fseek(file->f, 0, SEEK_END);
    long len = ftell(file->f);
    fseek(file->f, pos, SEEK_SET);
    return (u64)len;
}

static int LogThreshold = Warn;
void SetLogThreshold(int level) { LogThreshold = level; }

void Log(LogLevel level, const char* fmt, ...)
{
    if (level < LogThreshold) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

struct Thread { std::thread t; };

Thread* Thread_Create(std::function<void()> func) { return new Thread{std::thread(std::move(func))}; }
void Thread_Free(Thread* thread)
{
    if (thread->t.joinable()) thread->t.join();
    delete thread;
}
void Thread_Wait(Thread* thread) { if (thread->t.joinable()) thread->t.join(); }

struct Semaphore
{
    std::mutex m;
    std::condition_variable cv;
    int count = 0;
};

Semaphore* Semaphore_Create() { return new Semaphore(); }
void Semaphore_Free(Semaphore* sema) { delete sema; }
void Semaphore_Reset(Semaphore* sema)
{
    std::lock_guard<std::mutex> lk(sema->m);
    sema->count = 0;
}
void Semaphore_Wait(Semaphore* sema)
{
    std::unique_lock<std::mutex> lk(sema->m);
    sema->cv.wait(lk, [&]{ return sema->count > 0; });
    sema->count--;
}
bool Semaphore_TryWait(Semaphore* sema, int timeout_ms)
{
    std::unique_lock<std::mutex> lk(sema->m);
    if (!sema->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return sema->count > 0; }))
        return false;
    sema->count--;
    return true;
}
void Semaphore_Post(Semaphore* sema, int count)
{
    std::lock_guard<std::mutex> lk(sema->m);
    sema->count += count;
    sema->cv.notify_all();
}

struct Mutex { std::mutex m; };
Mutex* Mutex_Create() { return new Mutex(); }
void Mutex_Free(Mutex* mutex) { delete mutex; }
void Mutex_Lock(Mutex* mutex) { mutex->m.lock(); }
void Mutex_Unlock(Mutex* mutex) { mutex->m.unlock(); }
bool Mutex_TryLock(Mutex* mutex) { return mutex->m.try_lock(); }

void Sleep(u64 usecs) { usleep(usecs); }

u64 GetMSCount()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
u64 GetUSCount()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void WriteNDSSave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {}
void WriteGBASave(const u8* savedata, u32 savelen, u32 writeoffset, u32 writelen, void* userdata) {}
void WriteFirmware(const Firmware& firmware, u32 writeoffset, u32 writelen, void* userdata) {}
void WriteDateTime(int year, int month, int day, int hour, int minute, int second, void* userdata) {}

void MP_Begin(void* userdata) {}
void MP_End(void* userdata) {}
int MP_SendPacket(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_RecvPacket(u8* data, u64* timestamp, void* userdata) { return 0; }
int MP_SendCmd(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_SendReply(u8* data, int len, u64 timestamp, u16 aid, void* userdata) { return 0; }
int MP_SendAck(u8* data, int len, u64 timestamp, void* userdata) { return 0; }
int MP_RecvHostPacket(u8* data, u64* timestamp, void* userdata) { return 0; }
u16 MP_RecvReplies(u8* data, u64 timestamp, u16 aidmask, void* userdata) { return 0; }

int Net_SendPacket(u8* data, int len, void* userdata) { return 0; }
int Net_RecvPacket(u8* data, void* userdata) { return 0; }

void Camera_Start(int num, void* userdata) {}
void Camera_Stop(int num, void* userdata) {}
void Camera_CaptureFrame(int num, u32* frame, int width, int height, bool yuv, void* userdata) {}

void Mic_Start(void* userdata) {}
void Mic_Stop(void* userdata) {}
int Mic_ReadInput(s16* data, int maxlength, void* userdata) { return 0; }

struct AACDecoder {};
AACDecoder* AAC_Init() { return nullptr; }
void AAC_DeInit(AACDecoder* dec) {}
bool AAC_Configure(AACDecoder* dec, int frequency, int channels) { return false; }
bool AAC_DecodeFrame(AACDecoder* dec, const void* input, int inputlen, void* output, int outputlen) { return false; }

bool Addon_KeyDown(KeyType type, void* userdata) { return false; }
void Addon_RumbleStart(u32 len, void* userdata) {}
void Addon_RumbleStop(void* userdata) {}
float Addon_MotionQuery(MotionQueryType type, void* userdata) { return 0.0f; }

struct DynamicLibrary { void* h; };
DynamicLibrary* DynamicLibrary_Load(const char* lib)
{
    void* h = dlopen(lib, RTLD_LAZY);
    if (!h) return nullptr;
    return new DynamicLibrary{h};
}
void DynamicLibrary_Unload(DynamicLibrary* lib)
{
    if (!lib) return;
    dlclose(lib->h);
    delete lib;
}
void* DynamicLibrary_LoadFunction(DynamicLibrary* lib, const char* name)
{
    if (!lib) return nullptr;
    return dlsym(lib->h, name);
}

}
