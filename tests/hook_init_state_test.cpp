#include "../native/hook_init_state.h"

#include <cassert>

int main() {
    hooks::InitController controller(1000);
    assert(controller.State() == hooks::InitState::Waiting);
    assert(controller.TryBegin(100));
    assert(controller.State() == hooks::InitState::Installing);
    assert(!controller.TryBegin(100));

    controller.Retry(100);
    assert(controller.State() == hooks::InitState::RetryableFailure);
    assert(!controller.TryBegin(1099));
    assert(controller.TryBegin(1100));

    controller.Activate();
    assert(controller.State() == hooks::InitState::Active);
    assert(!controller.TryBegin(999999));

    hooks::InitController unsupported(1000);
    assert(unsupported.TryBegin(0));
    unsupported.MarkUnsupported();
    assert(unsupported.State() == hooks::InitState::Unsupported);
    assert(!unsupported.TryBegin(999999));
}
