#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "../tooling/windows_command_line.h"
namespace privacy::crash_command {
enum class Rewrite { Unchanged, Changed, Invalid };
inline bool Space(wchar_t c) { return c==L' '||c==L'\t'; }
inline wchar_t PathCharacter(wchar_t c) {
 if(c==L'/')return L'\\';
 return c>=L'A'&&c<=L'Z'?c+(L'a'-L'A'):c;
}
// Deliberately no PATH search, basename-only match, relative path resolution,
// or dot-segment normalization. Unknown spellings keep native launch behavior.
inline bool ExactExecutable(std::wstring_view expected,std::wstring_view actual) {
 if(expected.empty()||expected.size()!=actual.size())return false;
 for(size_t i=0;i<expected.size();++i)if(PathCharacter(expected[i])!=PathCharacter(actual[i]))return false;
 return true;
}
inline std::wstring ExecutableArgument(std::wstring_view command) {return win32::command_line::Executable(command);}
struct Argument { size_t begin,end;std::wstring value; };
// Decode CRT backslash/quote rules only to identify option names. Keep all
// surviving source bytes intact instead of serializing the parsed arguments.
inline bool Arguments(std::wstring_view command,std::vector<Argument>& args) {
 size_t i=0;while(i<command.size()&&Space(command[i]))++i;
 if(i==command.size())return false;
 const size_t begin=i;bool quoted=false;
 while(i<command.size()){
  if(command[i]==L'"')quoted=!quoted;
  else if(!quoted&&Space(command[i]))break;
  ++i;
 }
 if(quoted)return false;
 args.push_back({begin,i,ExecutableArgument(command)});
 while(i<command.size()){
  while(i<command.size()&&Space(command[i]))++i;
  if(i==command.size())break;
  Argument arg{i,i,{}};quoted=false;
  while(i<command.size()&&(quoted||!Space(command[i]))){
   size_t slashes=0;while(i<command.size()&&command[i]==L'\\'){++slashes;++i;}
   if(i<command.size()&&command[i]==L'"'){
    arg.value.append(slashes/2,L'\\');
    if(slashes%2){arg.value+=L'"';++i;}
    else if(quoted&&i+1<command.size()&&command[i+1]==L'"'){arg.value+=L'"';i+=2;}
    else{quoted=!quoted;++i;}
   }else{
    arg.value.append(slashes,L'\\');
    if(i<command.size()&&(quoted||!Space(command[i])))arg.value+=command[i++];
   }
  }
  if(quoted)return false;
  arg.end=i;args.push_back(std::move(arg));
 }
 return true;
}
inline Rewrite StripUploadUrl(std::wstring_view command,std::wstring& output) {
 std::vector<Argument> args;
 if(!Arguments(command,args))return Rewrite::Invalid;
 std::vector<std::pair<size_t,size_t>> remove;
 for(size_t i=1;i<args.size();++i){
  if(args[i].value==L"--")break;
  if(args[i].value==L"--url"){
   if(i+1==args.size())return Rewrite::Invalid;
   remove.emplace_back(args[i].begin,args[i].end);
   ++i;remove.emplace_back(args[i].begin,args[i].end);
  }else if(args[i].value.compare(0,6,L"--url=")==0){
   remove.emplace_back(args[i].begin,args[i].end);
  }else if(args[i].value==L"--annotation"||args[i].value==L"--attachment"||
           args[i].value==L"--database"||args[i].value==L"--initial-client-data"||
           args[i].value==L"--metrics-dir"||args[i].value==L"--pipe-name"||
           args[i].value==L"--monitor-self-annotation"||args[i].value==L"--monitor-self-argument"){
   // A value that looks like a URL option is still data for the preceding
   // option. These are the argument-taking options in the installed handler.
   if(i+1==args.size())return Rewrite::Invalid;
   ++i;
  }
 }
 if(remove.empty())return Rewrite::Unchanged;
 output.assign(command);
 for(auto i=remove.rbegin();i!=remove.rend();++i)output.erase(i->first,i->second-i->first);
 return Rewrite::Changed;
}
}
