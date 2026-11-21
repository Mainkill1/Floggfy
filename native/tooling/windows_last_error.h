#pragma once
#include <windows.h>

namespace win32 {
struct LastError {
    DWORD value = GetLastError();
    void Restore() const noexcept { SetLastError(value); }
    ~LastError() { Restore(); }
};
}
