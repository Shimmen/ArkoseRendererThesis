#pragma once

#define NON_COPYABLE(Typename)    \
    Typename(Typename&) = delete; \
    Typename& operator=(Typename&) = delete;
