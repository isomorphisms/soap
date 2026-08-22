cmake_minimum_required(VERSION 3.22.1)
project(soap C)

set(NATIVE_GLUE_DIR "${CMAKE_ANDROID_NDK}/sources/android/native_app_glue")

add_library(native_app_glue STATIC
    "${NATIVE_GLUE_DIR}/android_native_app_glue.c"
)
target_include_directories(native_app_glue PUBLIC "${NATIVE_GLUE_DIR}")

add_library(soap SHARED soap.c)
target_compile_features(soap PRIVATE c_std_11)
target_compile_options(soap PRIVATE -Wall -Wextra -Wpedantic)
target_link_options(soap PRIVATE -u ANativeActivity_onCreate)
target_link_libraries(soap
    native_app_glue
    android
    log
    EGL
    GLESv3
    m
)
