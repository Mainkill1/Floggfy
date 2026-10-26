#include "../native/connect_message.h"
#include <cassert>
#include <string>
void TestConnectMessages() {
    using namespace history;ConnectMessage message;
    const std::string pause=R"({"message_id":42,"sent_by_device_id":"private-device","command":{"endpoint":"pause","logging_params":{"secret":"never log"}}})";
    assert(SummarizeConnectMessage(pause,message)&&message.action==ConnectAction::Pause&&message.has_message_id&&message.message_id==42&&message.sender=="private-device");
    assert(SummarizeConnectMessage(R"({"command":{"endpoint":"resume"},"message_id":0})",message)&&message.action==ConnectAction::Resume&&message.has_message_id&&message.message_id==0&&message.sender.empty());
    assert(SummarizeConnectMessage(R"({"command":{"endpoint":"pa\u0075se"},"sent_by_device_id":"a\u00e9"})",message)&&message.action==ConnectAction::Pause&&message.sender=="a\xc3\xa9");
    assert(SummarizeConnectMessage(R"({"irrelevant":[null,true,false,-12.3e+4,{"endpoint":"resume"}],"command":{"endpoint":"seek_to"},"message_id":4294967295})",message)&&message.action==ConnectAction::Seek&&message.message_id==UINT32_MAX);
    assert(SummarizeConnectMessage(R"({"command":{"endpoint":"private-arbitrary-value"}})",message)&&message.action==ConnectAction::Other&&std::string(ConnectActionName(message.action))=="other");
    for(const auto* invalid:{"", "[]", "{", "{}", "{\"command\":{}}", "{\"command\":{\"endpoint\":1}}",
        "{\"command\":{\"endpoint\":\"pause\",\"endpoint\":\"resume\"}}",
        "{\"command\":{\"endpoint\":\"pause\"},\"message_id\":-1}",
        "{\"command\":{\"endpoint\":\"pause\"},\"message_id\":4294967296}",
        "{\"command\":{\"endpoint\":\"pause\"},\"message_id\":1.0}",
        "{\"command\":{\"endpoint\":\"pause\"},\"message_id\":01}",
        "{\"command\":{\"endpoint\":\"pause\"},\"sent_by_device_id\":\"a\",\"sent_by_device_id\":\"b\"}",
        "{\"command\":{\"endpoint\":\"pause\"},\"command\":{\"endpoint\":\"resume\"}}",
        "{\"command\":{\"endpoint\":\"pause\"},\"other\":\"\\uD800\"}",
        "{\"command\":{\"endpoint\":\"pause\"}} trailing"}) {
        assert(!SummarizeConnectMessage(invalid,message)&&message.sender.empty()&&!message.has_message_id);
    }
    const auto oversized=std::string(max_connect_message+1,' ');assert(!SummarizeConnectMessage(oversized,message));
    assert(!SummarizeConnectMessage("{\"command\":{\"endpoint\":\"pause\"},\"sent_by_device_id\":\""+std::string(257,'x')+"\"}",message));
    assert(!SummarizeConnectMessage("{\"command\":{\"endpoint\":\"pause\"},\"x\":"+std::string(40,'[')+"null"+std::string(40,']')+"}",message));
}
#ifndef FLOGGFY_COMBINED_TEST
int main(){TestConnectMessages();return 0;}
#endif
