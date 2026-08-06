cmake_minimum_required(VERSION 3.20)

project(
    MyanglishIME
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Convert Windows backslashes to forward slashes.
file(
    TO_CMAKE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}"
    MYANGLISHIME_SOURCE_DIR
)

add_library(MyanglishEngine
    engine/Dictionary.cpp
    engine/MyanglishConverter.cpp
    engine/UnicodeUtils.cpp
)

target_include_directories(MyanglishEngine
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/engine"
)

target_compile_features(MyanglishEngine
    PUBLIC
        cxx_std_17
)

# PUBLIC makes the macro available to MyanglishPrototype
# and MyanglishTests because they link to MyanglishEngine.
target_compile_definitions(MyanglishEngine
    PUBLIC
        MYANGLISHIME_SOURCE_DIR="${MYANGLISHIME_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(MyanglishEngine PRIVATE /utf-8)
endif()

add_executable(MyanglishPrototype
    prototype/main.cpp
)

target_link_libraries(MyanglishPrototype
    PRIVATE
        MyanglishEngine
)

if(MSVC)
    target_compile_options(MyanglishPrototype PRIVATE /utf-8)
endif()

add_executable(MyanglishTests
    tests/ConverterTests.cpp
)

target_link_libraries(MyanglishTests
    PRIVATE
        MyanglishEngine
)

if(MSVC)
    target_compile_options(MyanglishTests PRIVATE /utf-8)
endif()

enable_testing()

add_test(
    NAME MyanglishConverterTests
    COMMAND MyanglishTests
)