// These checks catch accidental removal of local-handler arguments, incomplete
// duplicate removal, Windows quoting damage, and lookalike executable matching.
#include "../native/telemetry_crash_command.h"
#include <cassert>
#include <iostream>
int main() {
 using namespace privacy::crash_command;
 const std::wstring path=L"C:\\Users\\A User\\Spotify\\crashpad_handler.exe";
 assert(ExactExecutable(path,L"c:/users/a user/spotify/CRASHPAD_HANDLER.EXE"));
 assert(!ExactExecutable(path,path+L".evil.exe"));
 assert(!ExactExecutable(path,L"C:\\Other\\crashpad_handler.exe"));
 assert(!ExactExecutable(path,L"crashpad_handler.exe"));
 assert(!ExactExecutable(path,L"C:\\Users\\A User\\Spotify\\..\\crashpad_handler.exe"));
 const std::wstring local=L"\""+path+L"\" --database=\"C:\\crash data\" --initial-client-data=1,2,3 --annotation=note=\"a\\\"b\"";
 std::wstring output;
 assert(StripUploadUrl(local,output)==Rewrite::Unchanged);
 assert(StripUploadUrl(local+L" --url=https://crashdump.spotify.com:443",output)==Rewrite::Changed);
 assert(output==local+L" ");
 assert(StripUploadUrl(local+L" --url \"https://host/path with spaces\" --url=second --no-rate-limit",output)==Rewrite::Changed);
 assert(output==local+L"    --no-rate-limit");
 assert(StripUploadUrl(local+L" \"--url=https://host\"",output)==Rewrite::Changed);
 assert(output==local+L" ");
 assert(StripUploadUrl(local+L" --url= --url \"\"",output)==Rewrite::Changed);
 assert(output==local+L"   ");
 assert(StripUploadUrl(local+L" --url",output)==Rewrite::Invalid);
 assert(StripUploadUrl(local+L" --url=\"unclosed",output)==Rewrite::Invalid);
 assert(StripUploadUrl(local+L" -- --url=https://host",output)==Rewrite::Unchanged);
 assert(StripUploadUrl(local+L" --urlish=https://host --annotation=--url=https://host",output)==Rewrite::Unchanged);
 assert(StripUploadUrl(local+L" --annotation --url=annotation-data",output)==Rewrite::Unchanged);
 assert(StripUploadUrl(local+L" --attachment \"--url=https://filename\" --url=real",output)==Rewrite::Changed);
 assert(output==local+L" --attachment \"--url=https://filename\" ");
 assert(ExecutableArgument(L"\"C:\\Users\\A User\\Spotify\\crashpad_handler.exe\" --url=x")==path);
 assert(ExecutableArgument(L"crashpad_handler.exe --url=x")==L"crashpad_handler.exe");
 std::cout << "crash command tests passed\n";
}
