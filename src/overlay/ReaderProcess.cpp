#include "ReaderProcess.h"

#include <cstdio>
#include <vector>

namespace d3hud {

ReaderProcess::ReaderProcess() {
    char self[MAX_PATH];
    GetModuleFileNameA(nullptr, self, MAX_PATH);
    dir_ = self;
    dir_ = dir_.substr(0, dir_.find_last_of("\\/") + 1);
    job_ = CreateJobObjectA(nullptr, nullptr);
    if (job_ != nullptr) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
}

ReaderProcess::~ReaderProcess() {
    Stop();
    if (job_ != nullptr) CloseHandle(job_);
}

bool ReaderProcess::Start() {
    if (Running()) return true;
    if (process_ != nullptr) {
        CloseHandle(process_);
        process_ = nullptr;
    }
    const std::string exe = dir_ + "D3Hud.Reader.exe";
    if (GetFileAttributesA(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error_ = "D3Hud.Reader.exe not found next to the overlay";
        return false;
    }
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};  // inheritable log handle
    HANDLE log = CreateFileA((dir_ + "reader.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        error_ = "cannot create reader.log";
        return false;
    }
    STARTUPINFOA si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    std::string cmd = "\"" + exe + "\" --attach \"Diablo III64.exe\" --publish";
    std::vector<char> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back('\0');
    PROCESS_INFORMATION pi = {};
    const BOOL ok = CreateProcessA(exe.c_str(), cmdBuf.data(), nullptr, nullptr, TRUE,
                                   CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, dir_.c_str(), &si, &pi);
    CloseHandle(log);  // the child holds its own copy
    if (!ok) {
        error_ = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    if (job_ != nullptr) AssignProcessToJobObject(job_, pi.hProcess);  // before it runs
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    error_.clear();
    lastLine_.clear();
    lastLineAt_ = 0;
    return true;
}

void ReaderProcess::Stop() {
    if (Running()) {
        TerminateProcess(process_, 0);
        WaitForSingleObject(process_, 2000);
    }
}

bool ReaderProcess::Running() const {
    return process_ != nullptr && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

DWORD ReaderProcess::exitCode() const {
    DWORD code = STILL_ACTIVE;
    if (process_ != nullptr) GetExitCodeProcess(process_, &code);
    return code;
}

const std::string& ReaderProcess::LastLogLine() {
    const ULONGLONG now = GetTickCount64();
    if (process_ == nullptr || now - lastLineAt_ < 500) return lastLine_;
    lastLineAt_ = now;
    FILE* f = _fsopen((dir_ + "reader.log").c_str(), "rb", _SH_DENYNO);
    if (f == nullptr) return lastLine_;
    char buf[2048];
    _fseeki64(f, 0, SEEK_END);
    const long long size = _ftelli64(f);
    const long long from = size > static_cast<long long>(sizeof(buf)) ? size - sizeof(buf) : 0;
    _fseeki64(f, from, SEEK_SET);
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    // last non-empty line, skipping the per-5-s throughput stats
    size_t end = n;
    while (end > 0) {
        while (end > 0 && (buf[end - 1] == '\n' || buf[end - 1] == '\r')) --end;
        size_t start = end;
        while (start > 0 && buf[start - 1] != '\n') --start;
        std::string line(buf + start, end - start);
        if (!line.empty() && line.rfind("[stats]", 0) != 0 && line.rfind("  0x", 0) != 0) {
            lastLine_ = line;
            break;
        }
        end = start;
    }
    return lastLine_;
}

}  // namespace d3hud
