// CEF keeps wrapper identity before its public C structure. Exercise the real
// interception code and MinHook without replacing client/handler objects.
#include "../native/metadata_bridge.cpp"
#include <cassert>
#include <cstdio>

template<class T> struct CefStorage {
    unsigned long long type=0x434546434c49454eULL;
    void* object=this;
    void* wrapper=this;
    T api{};
    CefStorage(){api.base.size=sizeof(T);}
};
namespace {
CefStorage<history::Display> display;
CefStorage<history::Load> load;
CefStorage<history::Browser> browser;
history::Client* expected_client=nullptr;
history::Client* received_client=nullptr;
int console_calls=0,loading_calls=0,end_calls=0,browser_releases=0;
int ReleaseBrowser(history::Base* self){assert(self==&browser.api.base);++browser_releases;return 0;}
__attribute__((noinline)) history::Display* OriginalDisplay(history::Client* self){assert(self==expected_client);return &display.api;}
__attribute__((noinline)) history::Load* OriginalLoad(history::Client* self){assert(self==expected_client);return &load.api;}
__attribute__((noinline)) int OriginalConsole(history::Display* self,history::Browser* b,int level,const history::String* text,const history::String* source,int line){
    assert(self==&display.api&&b==&browser.api&&level==2&&text&&source==nullptr&&line==17);
    ++console_calls;b->base.release(&b->base);return 9;
}
__attribute__((noinline)) void OriginalLoading(history::Load* self,history::Browser* b,int loading,int back,int forward){
    assert(self==&load.api&&b==&browser.api&&loading==1&&back==0&&forward==1);
    ++loading_calls;b->base.release(&b->base);
}
__attribute__((noinline)) void OriginalEnd(history::Load* self,history::Browser* b,history::Frame* f,int status){
    assert(self==&load.api&&b==&browser.api&&f==nullptr&&status==200);
    ++end_calls;b->base.release(&b->base);
}
int ReceiveBrowserClient(const void*,history::Client* client,const history::String*,const void*,void*,void*){received_client=client;return 1;}
history::Browser* ReceiveSyncClient(const void*,history::Client* client,const history::String*,const void*,void*,void*){received_client=client;return &browser.api;}
void* ReceiveViewClient(history::Client* client,const history::String*,const void*,void*,void*,void*){received_client=client;return &browser.api;}
}
int main(){
    assert(MH_Initialize()==MH_OK);
    CefStorage<history::Client> storage;
    expected_client=&storage.api;
    storage.api.methods[4]=reinterpret_cast<void*>(OriginalDisplay);
    storage.api.methods[14]=reinterpret_cast<void*>(OriginalLoad);
    display.api.methods[6]=reinterpret_cast<void*>(OriginalConsole);
    load.api.methods[0]=reinterpret_cast<void*>(OriginalLoading);
    load.api.methods[2]=reinterpret_cast<void*>(OriginalEnd);
    browser.api.base.release=ReleaseBrowser;
    const auto original_client=storage.api;
    const auto original_display=display.api;
    const auto original_load=load.api;
    history::original_create=ReceiveBrowserClient;
    history::original_sync=ReceiveSyncClient;
    history::original_view=ReceiveViewClient;
    assert(history::CreateHook(nullptr,&storage.api,nullptr,nullptr,nullptr,nullptr)==1);
    assert(received_client==&storage.api&&"metadata interception replaced Spotify's CEF client identity");
    assert(history::SyncHook(nullptr,&storage.api,nullptr,nullptr,nullptr,nullptr)==&browser.api);
    assert(received_client==&storage.api);
    assert(history::ViewHook(&storage.api,nullptr,nullptr,nullptr,nullptr,nullptr)==&browser.api);
    assert(received_client==&storage.api);
    auto d=reinterpret_cast<history::Display*(*)(history::Client*)>(storage.api.methods[4])(&storage.api);
    auto l=reinterpret_cast<history::Load*(*)(history::Client*)>(storage.api.methods[14])(&storage.api);
    assert(d==&display.api&&l==&load.api);
    wchar_t ordinary[]=L"ordinary console message";history::String text{ordinary,wcslen(ordinary),nullptr};
    assert(reinterpret_cast<int(*)(history::Display*,history::Browser*,int,const history::String*,const history::String*,int)>(d->methods[6])(d,&browser.api,2,&text,nullptr,17)==9);
    reinterpret_cast<void(*)(history::Load*,history::Browser*,int,int,int)>(l->methods[0])(l,&browser.api,1,0,1);
    reinterpret_cast<void(*)(history::Load*,history::Browser*,history::Frame*,int)>(l->methods[2])(l,&browser.api,nullptr,200);
    assert(console_calls==1&&loading_calls==1&&end_calls==1&&browser_releases==3);
    assert(memcmp(&original_client,&storage.api,sizeof(original_client))==0);
    assert(memcmp(&original_display,&display.api,sizeof(original_display))==0);
    assert(memcmp(&original_load,&load.api,sizeof(original_load))==0);
    assert(storage.type==0x434546434c49454eULL&&storage.object==&storage&&storage.wrapper==&storage);
    assert(history::ObserveClient(nullptr)==nullptr);
    assert(MH_Uninitialize()==MH_OK);
    std::puts("metadata client/handler identity and original callback ownership preserved");
}
