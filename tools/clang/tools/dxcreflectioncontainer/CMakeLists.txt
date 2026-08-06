# Copyright (C) Microsoft Corporation. All rights reserved.
# This file is distributed under the University of Illinois Open Source License. See LICENSE.TXT for details.

add_clang_library(dxcreflectioncontainer STATIC DxcReflectionContainer.cpp DxcReflectionJson.cpp)

if(MSVC)
  target_compile_options(dxcreflectioncontainer PRIVATE /W4 /WX)
else()
  target_compile_options(dxcreflectioncontainer PRIVATE -Wall -Wextra -Werror -Wno-pedantic)
  # Modern clang (e.g. emscripten, macOS) warns on unused function templates in
  # the 3.7-era LLVM/Clang headers this includes; not actionable here.
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(dxcreflectioncontainer PRIVATE -Wno-unused-template)
  endif()
endif()
