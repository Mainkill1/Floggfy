#pragma once
#include <windows.h>

namespace win32 {
struct Handle {
    HANDLE value;
    explicit Handle(HANDLE handle=INVALID_HANDLE_VALUE) noexcept : value(handle) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

// FindFirstFile handles must be released with FindClose.
struct FindHandle {
    HANDLE value;
    explicit FindHandle(HANDLE handle=INVALID_HANDLE_VALUE) noexcept : value(handle) {}
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
    FindHandle(const FindHandle&) = delete;
    FindHandle& operator=(const FindHandle&) = delete;
};
}
