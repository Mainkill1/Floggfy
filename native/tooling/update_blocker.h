#pragma once
#include <windows.h>

namespace updates {
// The current process user's Update-folder read/write deny. State is checked
// at startup and on changes, not polled. Existing handles and protected child
// ACLs are outside this folder policy; other users are unaffected.
bool Initialize(bool enabled);
bool SetBlocked(bool blocked);
bool Blocked();
bool Available();
DWORD LastError();
}
