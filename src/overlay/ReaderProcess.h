// Starts/stops D3Hud.Reader.exe (next to the overlay exe) in the background for
// the overlay's "Start reader" button. The child gets no console; its output
// goes to reader.log, and a kill-on-close job object ends it with the overlay.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <string>

namespace d3hud {

class ReaderProcess {
public:
    ReaderProcess();
    ~ReaderProcess();

    // Launches `D3Hud.Reader.exe --attach "Diablo III64.exe" --publish --hz <hz>`; false
    // (see lastError()) if the exe is missing or CreateProcess fails.
    bool Start(int hz = 60);
    void Stop();
    bool Running() const;
    DWORD pid() const { return pid_; }
    DWORD exitCode() const;  // after it stopped; STILL_ACTIVE while running
    const std::string& lastError() const { return error_; }
    // Last non-empty line of reader.log (refreshed at most every 500 ms).
    const std::string& LastLogLine();

private:
    std::string dir_;  // exe directory, trailing backslash
    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;
    DWORD pid_ = 0;
    std::string error_;
    std::string lastLine_;
    ULONGLONG lastLineAt_ = 0;
};

}  // namespace d3hud
