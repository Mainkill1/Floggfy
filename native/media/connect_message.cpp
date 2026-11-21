#include "connect_message.h"
#include <limits>
#include <utility>
namespace history {
namespace {
class Reader {
    std::string_view input;
    std::size_t position=0;
    unsigned values=0;
    ConnectMessage message;
    bool command_seen=false,endpoint_seen=false,sender_seen=false;
    void Space() noexcept {
        while(position<input.size()&&(input[position]==' '||input[position]=='\r'||input[position]=='\n'||input[position]=='\t'))++position;
    }
    bool Take(char c) noexcept {
        Space();if(position==input.size()||input[position]!=c)return false;
        ++position;return true;
    }
    bool Hex(unsigned& value) noexcept {
        value=0;
        for(unsigned i=0;i<4;++i) {
            if(position==input.size())return false;
            const auto c=input[position++];unsigned digit;
            if(c>='0'&&c<='9')digit=unsigned(c-'0');
            else if(c>='a'&&c<='f')digit=unsigned(c-'a'+10);
            else if(c>='A'&&c<='F')digit=unsigned(c-'A'+10);
            else return false;
            value=value*16+digit;
        }
        return true;
    }
    static bool Append(std::string* out,unsigned value) {
        if(!out)return true;
        if(value<0x80)out->push_back(char(value));
        else if(value<0x800) {
            out->push_back(char(0xc0|(value>>6)));out->push_back(char(0x80|(value&63)));
        } else if(value<0x10000) {
            out->push_back(char(0xe0|(value>>12)));out->push_back(char(0x80|((value>>6)&63)));out->push_back(char(0x80|(value&63)));
        } else {
            out->push_back(char(0xf0|(value>>18)));out->push_back(char(0x80|((value>>12)&63)));
            out->push_back(char(0x80|((value>>6)&63)));out->push_back(char(0x80|(value&63)));
        }
        return out->size()<=256;
    }
    bool String(std::string* out) {
        if(!Take('"'))return false;
        if(out)out->clear();
        while(position<input.size()) {
            unsigned c=static_cast<unsigned char>(input[position++]);
            if(c=='"')return true;
            if(c<32)return false;
            if(c!='\\') {
                if(out){out->push_back(char(c));if(out->size()>256)return false;}
                continue;
            }
            if(position==input.size())return false;
            c=static_cast<unsigned char>(input[position++]);
            switch(c) {
                case '"':case '\\':case '/':break;
                case 'b':c=8;break;case 'f':c=12;break;case 'n':c=10;break;case 'r':c=13;break;case 't':c=9;break;
                case 'u': {
                    if(!Hex(c))return false;
                    if(c>=0xd800&&c<=0xdbff) {
                        if(input.size()-position<2||input[position]!='\\'||input[position+1]!='u')return false;
                        position+=2;unsigned low=0;
                        if(!Hex(low)||low<0xdc00||low>0xdfff)return false;
                        c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
                    }else if(c>=0xdc00&&c<=0xdfff)return false;
                    if(!Append(out,c))return false;
                    continue;
                }
                default:return false;
            }
            if(!Append(out,c))return false;
        }
        return false;
    }
    bool Number(bool id=false) noexcept {
        Space();const auto begin=position;
        if(position<input.size()&&input[position]=='-')++position;
        if(position==input.size())return false;
        if(input[position]=='0')++position;
        else {
            if(input[position]<'1'||input[position]>'9')return false;
            do{++position;}while(position<input.size()&&input[position]>='0'&&input[position]<='9');
        }
        if(position<input.size()&&input[position]=='.') {
            ++position;const auto digits=position;
            while(position<input.size()&&input[position]>='0'&&input[position]<='9')++position;
            if(position==digits)return false;
        }
        if(position<input.size()&&(input[position]=='e'||input[position]=='E')) {
            ++position;if(position<input.size()&&(input[position]=='+'||input[position]=='-'))++position;
            const auto digits=position;
            while(position<input.size()&&input[position]>='0'&&input[position]<='9')++position;
            if(position==digits)return false;
        }
        if(id) {
            std::uint64_t value=0;
            if(position-begin>10)return false;
            for(auto i=begin;i<position;++i) {
                if(input[i]<'0'||input[i]>'9')return false;
                value=value*10+unsigned(input[i]-'0');
            }
            if(value>std::numeric_limits<std::uint32_t>::max())return false;
            message.message_id=std::uint32_t(value);message.has_message_id=true;
        }
        return position>begin;
    }
    bool Skip(unsigned depth) {
        Space();if(depth>32||++values>4096||position==input.size())return false;
        if(input[position]=='{')return Object(depth,false,false);
        if(input[position]=='[') {
            ++position;if(Take(']'))return true;
            do{if(!Skip(depth+1))return false;if(Take(']'))return true;}while(Take(','));
            return false;
        }
        if(input[position]=='"')return String(nullptr);
        for(const auto literal:{std::string_view("true"),std::string_view("false"),std::string_view("null")}) {
            if(input.substr(position,literal.size())==literal){position+=literal.size();return true;}
        }
        return Number();
    }
    bool Object(unsigned depth,bool root,bool command) {
        if(depth>32||!Take('{'))return false;
        if(Take('}'))return true;
        do {
            std::string key;
            if(!String(root||command?&key:nullptr)||!Take(':'))return false;
            if(root&&key=="message_id") {
                if(message.has_message_id||!Number(true))return false;
            }else if(root&&key=="sent_by_device_id") {
                if(sender_seen||!String(&message.sender))return false;
                sender_seen=true;
            }else if(root&&key=="command") {
                if(command_seen||!Object(depth+1,false,true))return false;
                command_seen=true;
            }else if(command&&key=="endpoint") {
                std::string value;
                if(endpoint_seen||!String(&value))return false;
                endpoint_seen=true;
                if(value=="pause")message.action=ConnectAction::Pause;
                else if(value=="resume")message.action=ConnectAction::Resume;
                else if(value=="play")message.action=ConnectAction::Play;
                else if(value=="seek_to")message.action=ConnectAction::Seek;
                else if(value=="skip_next")message.action=ConnectAction::SkipNext;
                else if(value=="skip_prev")message.action=ConnectAction::SkipPrevious;
                else if(value=="transfer")message.action=ConnectAction::Transfer;
            }else if(!Skip(depth+1))return false;
            if(Take('}'))return true;
        }while(Take(','));
        return false;
    }
public:
    explicit Reader(std::string_view value):input(value){}
    bool Read(ConnectMessage& out) {
        if(input.empty()||input.size()>max_connect_message||!Object(0,true,false))return false;
        Space();if(position!=input.size()||!command_seen||!endpoint_seen)return false;
        out=std::move(message);return true;
    }
};
}
bool SummarizeConnectMessage(std::string_view json,ConnectMessage& out) noexcept {
    out={};
    try{return Reader(json).Read(out);}catch(...){out={};return false;}
}
const char* ConnectActionName(ConnectAction action) noexcept {
    switch(action) {
        case ConnectAction::Pause:return "pause";case ConnectAction::Resume:return "resume";
        case ConnectAction::Play:return "play";case ConnectAction::Seek:return "seek_to";
        case ConnectAction::SkipNext:return "skip_next";case ConnectAction::SkipPrevious:return "skip_prev";
        case ConnectAction::Transfer:return "transfer";default:return "other";
    }
}
}
