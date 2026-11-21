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
}
