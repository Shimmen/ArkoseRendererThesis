#!/bin/bash

# download a stable version of glfw 3.3
git submodule update --init --recursive

# download all 3rd-party code for shaderc
# (the downloaded stuff are ignored by git)
./deps/shaderc/utils/git-sync-deps
