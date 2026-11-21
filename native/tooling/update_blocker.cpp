#include "windows_handle.h"
#include "update_blocker.h"
#include <aclapi.h>
#include "windows_known_folder.h"
#include <string>
#include <vector>
#include <new>

namespace updates {
namespace {
// Specific data/metadata bits, deliberately excluding READ_CONTROL, WRITE_DAC,
// SYNCHRONIZE and DELETE so the same account can remove this policy.
constexpr DWORD deny_mask = FILE_READ_DATA | FILE_READ_EA | FILE_READ_ATTRIBUTES |
    FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES;
constexpr BYTE deny_flags = OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE;
SRWLOCK state_lock = SRWLOCK_INIT;
struct Lock {
    Lock() { AcquireSRWLockExclusive(&state_lock); }
    ~Lock() { ReleaseSRWLockExclusive(&state_lock); }
};
using win32::Handle;
struct Security {
    PSECURITY_DESCRIPTOR value = nullptr;
    PACL dacl = nullptr;
    ~Security() { LocalFree(value); }
};
std::wstring parent_path, update_path;
std::vector<BYTE> user_sid;
bool initialized = false, blocked = false, available = false;
DWORD last_error = ERROR_NOT_READY;

void RefreshAfterFailure();
bool Fail(DWORD error) {
    RefreshAfterFailure();
    last_error = error ? error : ERROR_INVALID_DATA;
    return false;
}
bool ReadSecurity(HANDLE handle, Security& security) {
    DWORD error = GetSecurityInfo(handle, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &security.dacl, nullptr, &security.value);
    return error == ERROR_SUCCESS || Fail(error);
}
bool OpenDirectory(const std::wstring& path, DWORD access, Handle& handle) {
    // Omitting FILE_SHARE_DELETE pins the opened directory against replacement
    // throughout the operation. OPEN_REPARSE_POINT inspects the link itself.
    handle.value = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle.value == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.value, &info)) return false;
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        SetLastError(ERROR_REPARSE_TAG_INVALID);
        return false;
    }
    return true;
}
bool IsMarker(const ACE_HEADER* header) {
    if (header->AceType != ACCESS_DENIED_ACE_TYPE || header->AceFlags != deny_flags ||
        header->AceSize < sizeof(ACCESS_DENIED_ACE)) return false;
    const auto ace = reinterpret_cast<const ACCESS_DENIED_ACE*>(header);
    return ace->Mask == deny_mask && EqualSid(const_cast<DWORD*>(&ace->SidStart), user_sid.data());
}
bool HasMarker(PACL acl) {
    if (!acl) return false;
    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void* ace = nullptr;
        if (GetAce(acl, i, &ace) && IsMarker(static_cast<ACE_HEADER*>(ace))) return true;
    }
    return false;
}
bool Access(PSECURITY_DESCRIPTOR sd, DWORD& granted) {
    Handle token, impersonation;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &token.value) ||
        !DuplicateToken(token.value, SecurityImpersonation, &impersonation.value))
        return false;
    GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    alignas(PRIVILEGE_SET) BYTE privileges[sizeof(PRIVILEGE_SET) + 16 * sizeof(LUID_AND_ATTRIBUTES)]{};
    DWORD size = sizeof(privileges);
    BOOL access_status = FALSE;
    if (!AccessCheck(sd, impersonation.value, MAXIMUM_ALLOWED, &mapping,
            reinterpret_cast<PRIVILEGE_SET*>(privileges), &size, &granted, &access_status))
        return false;
    if (!access_status) granted = 0;
    return true;
}
// Recovery must not allocate C++ memory: it also runs from exception handlers.
void RefreshAfterFailure() {
    available = false;
    if (!initialized) return;
    Handle parent, target;
    if (!OpenDirectory(parent_path, READ_CONTROL, parent) ||
        !OpenDirectory(update_path, READ_CONTROL | WRITE_DAC, target)) return;
    Security security;
    if (GetSecurityInfo(target.value, SE_FILE_OBJECT,
        OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
        nullptr, nullptr, &security.dacl, nullptr, &security.value) != ERROR_SUCCESS) return;
    DWORD access = 0;
    if (!Access(security.value, access)) return;
    blocked = HasMarker(security.dacl) && (access & deny_mask) == 0;
    available = (access & (READ_CONTROL | WRITE_DAC)) == (READ_CONTROL | WRITE_DAC);
}
bool Change(bool enable, bool startup = false) {
    if (!initialized) return Fail(ERROR_NOT_READY);
    Handle parent;
    if (!OpenDirectory(parent_path, READ_CONTROL, parent)) {
        DWORD error = GetLastError();
        if (!enable && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)) {
            blocked = false; available = true; last_error = ERROR_SUCCESS; return true;
        }
        return Fail(error);
    }
    Handle target;
    if (!OpenDirectory(update_path, READ_CONTROL | WRITE_DAC, target)) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) return Fail(error);
        if (!enable) {
            blocked = false; available = true; last_error = ERROR_SUCCESS; return true;
        }
        if (!CreateDirectoryW(update_path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
            return Fail(GetLastError());
        if (!OpenDirectory(update_path, READ_CONTROL | WRITE_DAC, target)) return Fail(GetLastError());
    }
    Security current;
    if (!ReadSecurity(target.value, current)) return false;
    const bool marker = HasMarker(current.dacl);
    if (enable && marker) {
        DWORD access = 0;
        if (!Access(current.value, access)) return Fail(GetLastError());
        if ((access & deny_mask) == 0 &&
            (access & (READ_CONTROL | WRITE_DAC)) == (READ_CONTROL | WRITE_DAC)) {
            blocked = true; available = true; last_error = ERROR_SUCCESS; return true;
        }
    }
    if (!enable && !marker && startup) {
        // A default-off launch must not reset an unrelated custom ACL.
        blocked = false; available = true; last_error = ERROR_SUCCESS; return true;
    }
    DWORD expected_access = 0;
    if (enable) {
        // A NULL DACL permits everybody; do not replace it with deny-only,
        // which would accidentally remove other accounts' access.
        const DWORD sid_bytes = GetLengthSid(user_sid.data());
        const DWORD extra = sizeof(ACCESS_DENIED_ACE) - sizeof(DWORD) + sid_bytes;
        const DWORD old_size = current.dacl ? current.dacl->AclSize : sizeof(ACL);
        const DWORD null_allow_size = current.dacl ? 0 : sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE;
        std::vector<BYTE> buffer(old_size + extra + null_allow_size);
        PACL acl = reinterpret_cast<PACL>(buffer.data());
        if (!InitializeAcl(acl, static_cast<DWORD>(buffer.size()), ACL_REVISION_DS) ||
            !AddAccessDeniedAceEx(acl, ACL_REVISION_DS, deny_flags, deny_mask, user_sid.data()))
            return Fail(GetLastError());
        if (current.dacl) {
            for (DWORD i = 0; i < current.dacl->AceCount; ++i) {
                void* raw = nullptr;
                if (!GetAce(current.dacl, i, &raw)) return Fail(GetLastError());
                auto ace = static_cast<ACE_HEADER*>(raw);
                if (!IsMarker(ace) && !AddAce(acl, ACL_REVISION_DS, MAXDWORD, ace, ace->AceSize))
                    return Fail(GetLastError());
            }
        } else {
            BYTE everyone[SECURITY_MAX_SID_SIZE]; DWORD size = sizeof(everyone);
            if (!CreateWellKnownSid(WinWorldSid, nullptr, everyone, &size) ||
                !AddAccessAllowedAceEx(acl, ACL_REVISION_DS, 0, FILE_ALL_ACCESS, everyone))
                return Fail(GetLastError());
        }
        DWORD error = SetSecurityInfo(target.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
            nullptr, nullptr, acl, nullptr);
        if (error) return Fail(error);
    } else {
        Security parent_security;
        if (!ReadSecurity(parent.value, parent_security)) return false;
        Handle token;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return Fail(GetLastError());
        GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
        PSECURITY_DESCRIPTOR inherited = nullptr;
        if (!CreatePrivateObjectSecurity(parent_security.value, nullptr, &inherited, TRUE, token.value, &mapping))
            return Fail(GetLastError());
        BOOL present = FALSE, defaulted = FALSE;
        PACL inherited_acl = nullptr;
        bool ok = GetSecurityDescriptorDacl(inherited, &present, &inherited_acl, &defaulted) != FALSE;
        if (!ok) Fail(GetLastError());
        if (ok && !Access(inherited, expected_access)) ok = Fail(GetLastError());
        if (ok) {
            // An empty unprotected DACL asks Windows to inherit the current
            // parent's policy. Copying the synthesized ACEs would turn them
            // explicit and prevent later parent-policy changes propagating.
            ACL empty{};
            InitializeAcl(&empty, sizeof(empty), ACL_REVISION);
            bool inherits_directory_access = false;
            if (parent_security.dacl) {
                for (DWORD i = 0; i < parent_security.dacl->AceCount; ++i) {
                    void* raw = nullptr;
                    if (GetAce(parent_security.dacl, i, &raw) &&
                        (static_cast<ACE_HEADER*>(raw)->AceFlags & CONTAINER_INHERIT_ACE))
                        inherits_directory_access = true;
                }
            }
            // A parent may grant access only to itself. Reapply that policy
            // explicitly when there are no directory-inheritable entries.
            PACL restored = inherits_directory_access ? &empty : parent_security.dacl;
            if (!inherits_directory_access && !Access(parent_security.value, expected_access)) {
                DestroyPrivateObjectSecurity(&inherited);
                return Fail(GetLastError());
            }
            DWORD error = SetSecurityInfo(target.value, SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                nullptr, nullptr, restored, nullptr);
            if (error) ok = Fail(error);
        }
        DestroyPrivateObjectSecurity(&inherited);
        if (!ok) return false;
    }
    auto verification_failure = [&](DWORD error) {
        if (!enable) {
            // Keep a recoverable marker if restoration cannot be verified.
            SECURITY_DESCRIPTOR_CONTROL control = 0; DWORD revision = 0;
            if (GetSecurityDescriptorControl(current.value, &control, &revision))
                SetSecurityInfo(target.value, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION |
                    ((control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION :
                                                   UNPROTECTED_DACL_SECURITY_INFORMATION),
                    nullptr, nullptr, current.dacl, nullptr);
        }
        return Fail(error);
    };
    Security verified;
    if (!ReadSecurity(target.value, verified)) return verification_failure(last_error);
    DWORD granted = 0;
    if (!Access(verified.value, granted)) return verification_failure(GetLastError());
    if (enable) {
        if (!HasMarker(verified.dacl) || (granted & deny_mask) != 0 ||
            (granted & (READ_CONTROL | WRITE_DAC)) != (READ_CONTROL | WRITE_DAC))
            return Fail(ERROR_ACCESS_DENIED);
    } else if (HasMarker(verified.dacl) || (granted & deny_mask) != (expected_access & deny_mask)) {
        return verification_failure(ERROR_ACCESS_DENIED);
    }
    blocked = enable; available = true; last_error = ERROR_SUCCESS;
    return true;
}
}

bool Initialize(bool enabled) {
    Lock lock;
    try {
    initialized = false; blocked = false; available = false;
    const HRESULT result = history::ReadKnownFolder(FOLDERID_LocalAppData, parent_path);
    if (FAILED(result)) return Fail(HRESULT_FACILITY(result) == FACILITY_WIN32 ? HRESULT_CODE(result) : ERROR_PATH_NOT_FOUND);
    parent_path += L"\\Spotify";
    update_path = parent_path + L"\\Update";
    Handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) return Fail(GetLastError());
    DWORD bytes = 0;
    GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);
    if (!bytes) return Fail(GetLastError());
    std::vector<BYTE> buffer(bytes);
    if (!GetTokenInformation(token.value, TokenUser, buffer.data(), bytes, &bytes)) return Fail(GetLastError());
    PSID sid = reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid;
    user_sid.resize(GetLengthSid(sid));
    if (!CopySid(static_cast<DWORD>(user_sid.size()), user_sid.data(), sid)) return Fail(GetLastError());
    initialized = true;
    return Change(enabled, true);
    } catch (const std::bad_alloc&) { return Fail(ERROR_NOT_ENOUGH_MEMORY); }
      catch (...) { return Fail(ERROR_UNHANDLED_EXCEPTION); }
}
bool SetBlocked(bool value) {
    Lock lock;
    try { return Change(value); }
    catch (const std::bad_alloc&) { return Fail(ERROR_NOT_ENOUGH_MEMORY); }
    catch (...) { return Fail(ERROR_UNHANDLED_EXCEPTION); }
}
bool Blocked() { Lock lock; return blocked; }
bool Available() { Lock lock; return available; }
DWORD LastError() { Lock lock; return last_error; }
}
