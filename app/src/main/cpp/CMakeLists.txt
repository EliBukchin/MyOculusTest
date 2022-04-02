# Copyright 2022 Eli Bukchin
#
#   Licensed under the Apache License, Version 2.0 (the "License");
#   you may not use this file except in compliance with the License.
#   You may obtain a copy of the License at
#
#       http:#www.apache.org/licenses/LICENSE-2.0
#
#   Unless required by applicable law or agreed to in writing, software
#   distributed under the License is distributed on an "AS IS" BASIS,
#   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#   See the License for the specific language governing permissions and
#   limitations under the License.

project(myoculustest)
cmake_minimum_required(VERSION 3.18.1)

add_library(native_app_glue STATIC ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c17")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -u ANativeActivity_onCreate")
add_library(myoculustest SHARED main.c)
target_include_directories(myoculustest PRIVATE ${ANDROID_NDK}/sources/android/native_app_glue)

add_library(openxr_loader SHARED IMPORTED)
include_directories("$ENV{OCULUS_OPENXR_MOBILE_SDK}/OpenXR/Include")
set_target_properties(
        openxr_loader
        PROPERTIES
        IMPORTED_LOCATION
        $ENV{OCULUS_OPENXR_MOBILE_SDK}/OpenXR/Libs/Android/${ANDROID_ABI}/${CMAKE_BUILD_TYPE}/libopenxr_loader.so
)

target_link_libraries(myoculustest
    openxr_loader
    android
    native_app_glue
    vulkan
    log)