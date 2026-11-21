#include <windows.h>
#include <shlobj.h>
#include <aclapi.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::wstring fixture;
static HRESULT folder_result = S_OK;
static unsigned folder_frees = 0;
static void WINAPI FixtureFolderFree(void* folder) {
    if (folder) ++folder_frees;
    CoTaskMemFree(folder);
}
static int fail_security_read = 0;
static bool fail_security_write = false;
static unsigned security_writes = 0;
static DWORD WINAPI TestGetSecurityInfo(HANDLE h, SE_OBJECT_TYPE type, SECURITY_INFORMATION info,
    PSID* owner, PSID* group, PACL* dacl, PACL* sacl, PSECURITY_DESCRIPTOR* sd) {
    if (fail_security_read && --fail_security_read == 0) return ERROR_NOT_ENOUGH_MEMORY;
    return GetSecurityInfo(h, type, info, owner, group, dacl, sacl, sd);
}
static DWORD WINAPI TestSetSecurityInfo(HANDLE h, SE_OBJECT_TYPE type, SECURITY_INFORMATION info,
    PSID owner, PSID group, PACL dacl, PACL sacl) {
    ++security_writes;
    if (fail_security_write) { fail_security_write = false; return ERROR_ACCESS_DENIED; }
    return SetSecurityInfo(h, type, info, owner, group, dacl, sacl);
}
// Substitute only the folder lookup. Every token, ACL and file operation uses
// real Windows APIs, exclusively beneath a unique directory in Windows TEMP.
[[maybe_unused]] static HRESULT WINAPI FixtureKnownFolder(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR* out) {
    const size_t bytes = (fixture.size() + 1) * sizeof(wchar_t);
    *out = static_cast<PWSTR>(CoTaskMemAlloc(bytes));
    if (!*out) return E_OUTOFMEMORY;
    memcpy(*out, fixture.c_str(), bytes);
    return folder_result;
}
#define SHGetKnownFolderPath FixtureKnownFolder
#define CoTaskMemFree FixtureFolderFree
#define GetSecurityInfo TestGetSecurityInfo
#define SetSecurityInfo TestSetSecurityInfo
#include "../native/update_blocker.cpp"
#undef SHGetKnownFolderPath
#undef CoTaskMemFree
#undef GetSecurityInfo
#undef SetSecurityInfo

static void Check(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s (Windows=%lu blocker=%lu)\n", message,
                     GetLastError(), updates::LastError());
        std::exit(1);
    }
}
static bool Open(const std::wstring& path, DWORD access) {
    HANDLE h = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
static void WriteFileFixture(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(h != INVALID_HANDLE_VALUE, "create file fixture");
    DWORD count = 0;
    Check(WriteFile(h, "fixture", 7, &count, nullptr) && count == 7, "write fixture");
    CloseHandle(h);
}
static std::vector<BYTE> Dacl(const std::wstring& path) {
    PACL acl = nullptr; PSECURITY_DESCRIPTOR sd = nullptr;
    Check(GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr, &sd) == ERROR_SUCCESS,
        "read fixture ACL");
    std::vector<BYTE> result;
    if (acl) result.assign(reinterpret_cast<BYTE*>(acl), reinterpret_cast<BYTE*>(acl) + acl->AclSize);
    LocalFree(sd);
    return result;
}
static void ParentPolicy(const std::wstring& path, DWORD permissions = FILE_ALL_ACCESS & ~FILE_WRITE_ATTRIBUTES,
                         DWORD inheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT) {
    HANDLE token = nullptr;
    Check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != FALSE, "query fixture user");
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    Check(GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE, "read fixture user");
    CloseHandle(token);
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = permissions;
    entry.grfAccessMode = SET_ACCESS;
    entry.grfInheritance = inheritance;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.TrusteeType = TRUSTEE_IS_USER;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(reinterpret_cast<TOKEN_USER*>(buffer.data())->User.Sid);
    PACL acl = nullptr;
    Check(SetEntriesInAclW(1, &entry, nullptr, &acl) == ERROR_SUCCESS, "build custom parent policy");
    Check(SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, acl, nullptr) == ERROR_SUCCESS, "set custom parent policy");
    LocalFree(acl);
}
static void Junction(const std::wstring& path, const std::wstring& destination) {
    // mklink /J uses an ordinary NTFS junction and needs no symlink privilege.
    std::wstring command = L"cmd.exe /d /c mklink /J \"" + path + L"\" \"" + destination + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Check(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, nullptr, &startup, &process) != FALSE, "launch fixture junction creation");
    Check(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0, "junction creation finishes");
    DWORD exit = 1;
    Check(GetExitCodeProcess(process.hProcess, &exit) != FALSE && exit == 0, "create fixture junction");
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
}
int main() {
    wchar_t temp[MAX_PATH], unique[MAX_PATH];
    Check(GetTempPathW(MAX_PATH, temp) != 0, "find Windows TEMP");
    Check(GetTempFileNameW(temp, L"flu", 0, unique) != 0, "reserve unique fixture");
    Check(DeleteFileW(unique) && CreateDirectoryW(unique, nullptr), "create isolated fixture");
    fixture = unique;
    folder_result = HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    Check(!updates::Initialize(false) && updates::LastError() == ERROR_ACCESS_DENIED,
          "preserve a failed shell folder lookup");
    Check(folder_frees == 1, "release a folder path returned with a failed lookup");
    folder_result = S_OK;
    const std::wstring parent = fixture + L"\\Spotify", update = parent + L"\\Update";
    Check(CreateDirectoryW(parent.c_str(), nullptr), "create parent fixture");
    Check(updates::Initialize(false), "default off succeeds with no Update folder");
    Check(!updates::Blocked() && updates::Available(), "default off state");
    Check(GetFileAttributesW(update.c_str()) == INVALID_FILE_ATTRIBUTES, "default off does not create Update");
    Check(updates::SetBlocked(true), "enable creates missing folder");
    Check(updates::Blocked() && updates::Available(), "enabled state verified");
    Check(!Open(update, FILE_LIST_DIRECTORY), "folder ordinary read blocked");
    Check(!Open(update, FILE_ADD_FILE), "folder ordinary write blocked");
    Check(Open(update, READ_CONTROL | WRITE_DAC), "ACL can still be restored");
    const auto blocked = Dacl(update);
    const unsigned writes_before = security_writes;
    Check(updates::SetBlocked(true) && Dacl(update) == blocked, "repeated enable idempotent");
    Check(updates::Initialize(true) && security_writes == writes_before, "healthy startup validates without rewriting ACL");
    fail_security_write = true;
    Check(!updates::SetBlocked(false) && updates::Blocked() && updates::Available(), "failed restore retains checked state and retry ability");
    Check(updates::LastError() == ERROR_ACCESS_DENIED, "failed restore exposes error");
    fail_security_read = 3; // Current, parent, then the restored target ACL.
    Check(!updates::SetBlocked(false) && updates::Blocked() && updates::Available(),
          "unverified restore rolls back to recoverable blocked policy");
    Check(updates::SetBlocked(false), "disable restores access");
    Check(Open(update, GENERIC_READ | GENERIC_WRITE), "normal folder access restored");
    const std::wstring child = update + L"\\payload.bin";
    WriteFileFixture(child);
    Check(updates::SetBlocked(true), "block existing payload");
    Check(!Open(child, GENERIC_READ) && !Open(child, GENERIC_WRITE), "inherited child read/write blocked");
    Check(updates::Initialize(false), "disabled startup removes our deny");
    Check(Open(child, GENERIC_READ | GENERIC_WRITE), "inherited child restored");
    const auto normal = Dacl(update);
    Check(updates::Initialize(false) && Dacl(update) == normal, "off startup leaves unrelated ACL unchanged");
    Check(updates::Initialize(true), "enabled startup restores missing deny");
    Check(!Open(update, FILE_LIST_DIRECTORY), "startup repair blocks access");
    Check(updates::SetBlocked(false), "final restoration");
    fail_security_read = 2; // Initial ACL read succeeds, post-mutation read fails.
    Check(!updates::SetBlocked(true) && updates::Blocked() && updates::Available(), "post-apply verification failure refreshes state and permits restoration");
    Check(updates::LastError() == ERROR_NOT_ENOUGH_MEMORY, "verification failure exposes original error");
    Check(updates::SetBlocked(false), "restore after verification failure");
    // A custom parent restriction must survive restoration, while its permitted
    // normal writes work. A broad full-control reset would fail these checks.
    ParentPolicy(parent);

    Check(!Open(update, FILE_WRITE_ATTRIBUTES) && Open(update, FILE_ADD_FILE), "custom parent policy inherited");
    Check(updates::SetBlocked(true) && updates::SetBlocked(false), "restore custom parent policy");
    Check(!Open(update, FILE_WRITE_ATTRIBUTES) && Open(update, FILE_ADD_FILE), "custom parent restriction preserved");
    const auto custom = Dacl(update);
    Check(updates::Initialize(false) && Dacl(update) == custom, "off startup preserves custom ACL exactly");
    ParentPolicy(update, FILE_ALL_ACCESS);
    ParentPolicy(parent, FILE_ALL_ACCESS, NO_INHERITANCE);
    Check(updates::SetBlocked(true), "block with non-inheritable parent policy");
    Check(updates::SetBlocked(false), "restore parent with non-inheritable policy");
    Check(Open(update, GENERIC_READ | GENERIC_WRITE), "non-inheritable parent permissions reapplied");
    ParentPolicy(update);
    Check(updates::Initialize(false) && !Open(update, FILE_WRITE_ATTRIBUTES), "startup leaves unrelated explicit restriction");
    Check(updates::SetBlocked(false) && Open(update, FILE_WRITE_ATTRIBUTES), "explicit disable reapplies parent even without marker");
    // A NULL target DACL must not become an inheritable full-control grant
    // for Everyone on a child whose existing ACL is deliberately restricted.
    ParentPolicy(child, FILE_ALL_ACCESS, NO_INHERITANCE);
    Check(SetNamedSecurityInfoW(const_cast<wchar_t*>(update.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS, "prepare NULL target DACL");
    auto child_policy = Dacl(child);
    Check(SetNamedSecurityInfoW(const_cast<wchar_t*>(child.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, reinterpret_cast<PACL>(child_policy.data()), nullptr) == ERROR_SUCCESS,
        "make restricted child inherit additions");
    Check(updates::SetBlocked(true), "block NULL-DACL target");
    child_policy = Dacl(child);
    PACL child_acl = reinterpret_cast<PACL>(child_policy.data());
    BYTE world[SECURITY_MAX_SID_SIZE]; DWORD world_size = sizeof(world);
    Check(CreateWellKnownSid(WinWorldSid, nullptr, world, &world_size) != FALSE, "create Everyone probe SID");
    DWORD world_access = 0;
    for (DWORD i = 0; i < child_acl->AceCount; ++i) {
        void* raw = nullptr;
        Check(GetAce(child_acl, i, &raw) != FALSE, "inspect child access policy");
        const auto ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE &&
            EqualSid(const_cast<DWORD*>(&ace->SidStart), world)) world_access |= ace->Mask;
    }
    Check(world_access == 0, "NULL target handling does not broaden child Everyone access");
    Check(updates::SetBlocked(false), "restore NULL-DACL fixture to parent policy");
    Check(DeleteFileW(child.c_str()) && RemoveDirectoryW(update.c_str()), "remove payload fixture");
    ParentPolicy(parent, FILE_ALL_ACCESS);

    const std::wstring elsewhere = fixture + L"\\Elsewhere";
    Check(CreateDirectoryW(elsewhere.c_str(), nullptr), "create reparse destination fixture");
    const auto untouched = Dacl(elsewhere);
    Junction(update, elsewhere);
    Check(!updates::Initialize(true) && !updates::Available() && !updates::Blocked() &&
          updates::LastError() != ERROR_SUCCESS, "reject Update reparse point honestly");
    Check(Dacl(elsewhere) == untouched && Open(elsewhere, GENERIC_READ | GENERIC_WRITE), "Update junction destination untouched");
    Check(!updates::Initialize(false), "disabled startup also rejects Update reparse point");
    Check(RemoveDirectoryW(update.c_str()) && RemoveDirectoryW(parent.c_str()), "remove Update junction and parent");
    Junction(parent, elsewhere);
    Check(!updates::Initialize(true), "reject parent Spotify reparse point");
    Check(GetFileAttributesW((elsewhere + L"\\Update").c_str()) == INVALID_FILE_ATTRIBUTES &&
          Dacl(elsewhere) == untouched, "parent junction destination untouched");
    Check(RemoveDirectoryW(parent.c_str()) && RemoveDirectoryW(elsewhere.c_str()) &&
          RemoveDirectoryW(fixture.c_str()), "remove fixture");
    std::puts("PASS: isolated Windows Update ACL integration tests");
}
