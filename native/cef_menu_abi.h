#pragma once
#include "cef_menu_capability.h"

namespace cef_menu { namespace abi {
// Published x64 CEF C-API prefix. Only this adapter knows method slot numbers;
// call sites use signatures matching the public menu_model API.
struct Base {
    std::size_t size;
    void(*add_ref)(Base*);int(*release)(Base*);
    int(*has_one_ref)(Base*);int(*has_at_least_one_ref)(Base*);
};
struct String {wchar_t* str;std::size_t length;void(*dtor)(wchar_t*);};
struct Menu {Base base;void* methods[kHighestRequiredMenuSlot+1];};
class Methods {
    enum class Slot:unsigned {IsSubmenu=0,Clear=1,Count=2,Separator=3,Item=4,CheckItem=5,Submenu=7,
        IndexOf=15,Label=20,TypeAt=23,GetSubmenu=28,Visible=32,Enabled=36,Checked=40};
    template<class Function> static Function Get(Menu* menu,Slot slot) {
        const auto index=static_cast<unsigned>(slot);
        return menu && menu->base.size>=kRequiredMenuBytes ?
            reinterpret_cast<Function>(menu->methods[index]) : nullptr;
    }
public:
    using AddSubmenuFunction=Menu*(*)(Menu*,int,const String*);
    static AddSubmenuFunction SubmenuImplementation(Menu* m){return Get<AddSubmenuFunction>(m,Slot::Submenu);}
    static bool IsSubmenu(Menu* m){const auto f=Get<int(*)(Menu*)>(m,Slot::IsSubmenu);return f&&f(m)!=0;}
    static bool Clear(Menu* m){const auto f=Get<int(*)(Menu*)>(m,Slot::Clear);return f&&f(m)!=0;}
    static std::size_t Count(Menu* m){const auto f=Get<std::size_t(*)(Menu*)>(m,Slot::Count);return f?f(m):0;}
    static bool AddSeparator(Menu* m){const auto f=Get<int(*)(Menu*)>(m,Slot::Separator);return f&&f(m)!=0;}
    static bool AddItem(Menu* m,int id,const String* label){const auto f=Get<int(*)(Menu*,int,const String*)>(m,Slot::Item);return f&&f(m,id,label)!=0;}
    static bool AddCheckItem(Menu* m,int id,const String* label){const auto f=Get<int(*)(Menu*,int,const String*)>(m,Slot::CheckItem);return f&&f(m,id,label)!=0;}
    static Menu* AddSubmenu(Menu* m,int id,const String* label){const auto f=Get<Menu*(*)(Menu*,int,const String*)>(m,Slot::Submenu);return f?f(m,id,label):nullptr;}
    static int IndexOf(Menu* m,int id){const auto f=Get<int(*)(Menu*,int)>(m,Slot::IndexOf);return f?f(m,id):-1;}
    static bool SetLabel(Menu* m,int id,const String* label){const auto f=Get<int(*)(Menu*,int,const String*)>(m,Slot::Label);return f&&f(m,id,label)!=0;}
    static int TypeAt(Menu* m,std::size_t index){const auto f=Get<int(*)(Menu*,std::size_t)>(m,Slot::TypeAt);return f?f(m,index):0;}
    static Menu* GetSubmenu(Menu* m,int id){const auto f=Get<Menu*(*)(Menu*,int)>(m,Slot::GetSubmenu);return f?f(m,id):nullptr;}
    static bool SetVisible(Menu* m,int id,bool value){const auto f=Get<int(*)(Menu*,int,int)>(m,Slot::Visible);return f&&f(m,id,int(value))!=0;}
    static bool SetEnabled(Menu* m,int id,bool value){const auto f=Get<int(*)(Menu*,int,int)>(m,Slot::Enabled);return f&&f(m,id,int(value))!=0;}
    static bool SetChecked(Menu* m,int id,bool value){const auto f=Get<int(*)(Menu*,int,int)>(m,Slot::Checked);return f&&f(m,id,int(value))!=0;}
};
// Own exactly one CEF-transferred model reference. Forwarding a callback to the
// original delegate transfers it again; GetSubmenu() results are owned too.
class Reference {
    Menu* menu_;
public:
    explicit Reference(Menu* menu):menu_(menu){}
    ~Reference(){if(menu_&&menu_->base.release)menu_->base.release(&menu_->base);}
    Reference(const Reference&)=delete;
    Reference& operator=(const Reference&)=delete;
    Menu* Take(){auto* result=menu_;menu_=nullptr;return result;}
};
static_assert(sizeof(Base)==kBaseBytes&&sizeof(Menu)==kRequiredMenuBytes,"CEF menu public prefix");
}}
