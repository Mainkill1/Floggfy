#include "../native/cef_identity.h"

#include <cassert>

int main() {
    assert(cef_compat::IsSupported({146, 0, 10, 3504}));
    assert(cef_compat::IsSupported({151, 3, 18, 3578}));
    assert(!cef_compat::IsSupported({145, 0, 10, 3504}));
    assert(!cef_compat::IsSupported({146, 1, 10, 3504}));
    assert(!cef_compat::IsSupported({146, 0, 11, 3504}));
    assert(!cef_compat::IsSupported({146, 0, 10, 3503}));
    assert(!cef_compat::IsSupported({151, 3, 18, 3577}));
    assert(!cef_compat::IsSupported({151, 3, 19, 3578}));
}
