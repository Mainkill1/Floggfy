Public menu ABI declarations were audited against these exact CEF revisions:

- https://github.com/chromiumembedded/cef/blob/8219561/include/cef_menu_model.h
- https://github.com/chromiumembedded/cef/blob/8219561/include/cef_menu_model_delegate.h
- https://github.com/chromiumembedded/cef/blob/beff58dbc4d0fd12b3eafea8f5314ce22e649078/include/cef_menu_model.h
- https://github.com/chromiumembedded/cef/blob/beff58dbc4d0fd12b3eafea8f5314ce22e649078/include/cef_menu_model_delegate.h

The 8219561 and beff58d revisions have the same relevant method counts and order:
menu model 56, menu delegate 7, client 19, display handler 13, load handler 4,
browser 21 and frame 26. The CEF translator maps bool to int and reference
strings to cef_string_utf16_t pointers.

The menu integration uses CEF's published structure sizes as a runtime capability
contract. It requires only the prefix through `set_checked`, checks every method
it calls for executable memory, and accepts larger append-compatible structures.
It hooks the exported `cef_menu_model_create` factory and does not gate on CEF
version numbers. Metadata integration has a wider ABI surface and retains its
separate exact CEF identity table. Original delegate callbacks and references are
forwarded through a proxy without changing the original callback object.

Transferred callback arguments and factory ownership follow CEF's translator
rules: the incoming delegate's reference is consumed, the replacement is passed
with one transferred reference, and callback model arguments are consumed by
the forwarded callback or released locally when handled. Reference:

- https://github.com/chromiumembedded/cef/blob/8219561/libcef_dll/ctocpp/ctocpp_ref_counted.h
- https://github.com/chromiumembedded/cef/blob/8219561/libcef_dll/cpptoc/cpptoc_ref_counted.h

See LICENSE.txt for the CEF license. No CEF binary is redistributed.

Optional metadata bridge: public cef_client, cef_browser, cef_frame,
cef_display_handler and cef_load_handler declarations at the same revision.
The bridge preserves the original client and handler objects and intercepts
their callback function addresses with MinHook. Replacement C structures break
CEF's C++ wrapper identity when Spotify retrieves a browser's client again.
Unmodified callbacks retain their original arguments and reference transfers;
locally handled console messages consume the transferred browser reference.
Browser/load/display structure sizes are checked before interception. Callback
addresses come from the audited public method tables, without module offsets.

The metadata poll task uses the public cef_task_t/cef_post_task ABI from
https://github.com/chromiumembedded/cef/blob/8219561/include/cef_task.h .
The UI queue has at most one outstanding task, with transferred CEF ownership.
