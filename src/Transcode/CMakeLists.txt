# MIT License
#
# Copyright (c) 2016-present The ZLMediaKit project authors. All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

file(GLOB TRANSCODE_SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# 转码模块源文件
set(TRANSCODE_SRC_LIST
    TranscodeConfig.cpp
    TranscodeSession.cpp  
    TranscodeManager.cpp
)

# 转码模块头文件
set(TRANSCODE_HEADER_LIST
    TranscodeConfig.h
    TranscodeSession.h
    TranscodeManager.h
)

# 添加到主库
list(APPEND LINK_LIB_LIST ${MK_LINK_LIBRARIES})

if(ENABLE_TRANSCODE)
    update_cached_list(MK_COMPILE_DEFINITIONS ENABLE_TRANSCODE)
    message(STATUS "Transcode module enabled")
endif()

# 设置源文件属性
foreach(SRC ${TRANSCODE_SRC_LIST})
    set_source_files_properties(${SRC} PROPERTIES COMPILE_FLAGS ${COMPILE_OPTIONS_DEFAULT})
endforeach()

# 为IDE设置文件夹
if(USE_SOLUTION_FOLDERS)
    set_target_properties(MediaKit PROPERTIES FOLDER "core")
endif()