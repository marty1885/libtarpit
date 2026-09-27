cmake_minimum_required(VERSION 3.16)
project(libtarpit VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(EMBEDDED_CORPUS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/embedded_corpus.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/damned.txt"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedText.cmake"
)
execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/damned.txt"
        "-DOUTPUT=${EMBEDDED_CORPUS_HEADER}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedText.cmake"
    RESULT_VARIABLE EMBED_RESULT
)
if(NOT EMBED_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to generate the embedded corpus header")
endif()

add_library(tarpit tarpit.c "${EMBEDDED_CORPUS_HEADER}")
add_library(libtarpit::tarpit ALIAS tarpit)
target_include_directories(tarpit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
)
if(MSVC)
    target_compile_options(tarpit PRIVATE /W4)
else()
    target_compile_options(tarpit PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(tarpit-demo demo.c)
target_link_libraries(tarpit-demo PRIVATE tarpit)

enable_testing()
add_executable(tarpit-test test.c)
target_link_libraries(tarpit-test PRIVATE tarpit)
add_test(NAME tarpit-test COMMAND tarpit-test)

include(GNUInstallDirs)
install(TARGETS tarpit EXPORT libtarpitTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES tarpit.h DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
