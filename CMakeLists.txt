# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
project(libsp VERSION 1.0.0 LANGUAGES C)

include(GNUInstallDirs)
include(CTest)

option(SP_WERROR "Treat warnings as errors" ON)
option(SP_SANITIZE "ASan+UBSan on library and tests" OFF)
option(SP_BUILD_FUZZ "Build the standalone fuzz driver" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SP_SRC
    src/sp_arena.c
    src/sp_str.c
    src/sp_utf8.c
    src/sp_path.c
)

add_library(sp STATIC ${SP_SRC})
add_library(sp::sp ALIAS sp)
target_include_directories(sp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if (MSVC)
    target_compile_options(sp PRIVATE /W4)
else()
    target_compile_options(sp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if (SP_WERROR)
        target_compile_options(sp PRIVATE -Werror)
    endif()
endif()

function(sp_apply_sanitize tgt)
    if (SP_SANITIZE AND NOT MSVC)
        target_compile_options(${tgt} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(${tgt} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()
sp_apply_sanitize(sp)

if (BUILD_TESTING)
    add_executable(sp_tests tests/test_sp.c)
    target_link_libraries(sp_tests PRIVATE sp)
    add_test(NAME sp_tests COMMAND sp_tests)
    sp_apply_sanitize(sp_tests)

    add_executable(sp_example examples/join_under.c)
    target_link_libraries(sp_example PRIVATE sp)
    add_test(NAME sp_example COMMAND sp_example)
    sp_apply_sanitize(sp_example)
endif()

if (SP_BUILD_FUZZ)
    add_executable(sp_fuzz fuzz/fuzz_sp.c)
    target_link_libraries(sp_fuzz PRIVATE sp)
    sp_apply_sanitize(sp_fuzz)
endif()

configure_file(libsp.pc.in libsp.pc @ONLY)
install(TARGETS sp ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/sp.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libsp.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
