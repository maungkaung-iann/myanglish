cmake_minimum_required(VERSION 3.20)

project(
    MyanglishIME
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(
    TO_CMAKE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}"
    MYANGLISHIME_SOURCE_DIR
)

# Alpha builds keep lightweight file logging enabled so TSF activation and
# edit-session failures can be diagnosed without DebugView.
option(
    MYANGLISHIME_ENABLE_DEBUG_LOG
    "Enable debug logging for MyanglishIME"
    ON
)

###########################################################
# Engine
###########################################################

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

target_compile_definitions(MyanglishEngine
    PUBLIC
        MYANGLISHIME_SOURCE_DIR="${MYANGLISHIME_SOURCE_DIR}"
)

if(MSVC)
    target_compile_options(MyanglishEngine PRIVATE /utf-8)
endif()

###########################################################
# Windows TSF IME DLL
###########################################################

if(WIN32)
    add_library(MyanglishIME SHARED
        ime/ClassFactory.cpp
        ime/CompositionManager.cpp
        ime/DllMain.cpp
        ime/Globals.cpp
        ime/Guids.cpp
        ime/KeyEventSink.cpp
        ime/Registration.cpp
        ime/TextService.cpp
        ime/MyanglishIME.def
    )

    target_include_directories(MyanglishIME
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/engine"
            "${CMAKE_CURRENT_SOURCE_DIR}/ime"
    )

    target_compile_features(MyanglishIME
        PRIVATE
            cxx_std_17
    )

    target_compile_definitions(MyanglishIME
        PRIVATE
            UNICODE
            _UNICODE
            MYANGLISHIME_SOURCE_DIR="${MYANGLISHIME_SOURCE_DIR}"
    )

    if(MYANGLISHIME_ENABLE_DEBUG_LOG)
        target_compile_definitions(
            MyanglishIME
            PRIVATE
                MYANGLISHIME_ENABLE_DEBUG_LOG=1
        )
    endif()

    # TSF COM classes live in Msctf.dll and are created with CoCreateInstance;
    # there is no msctf.lib to link here.
    target_link_libraries(MyanglishIME
        PRIVATE
            MyanglishEngine
            ole32
            oleaut32
            uuid
            advapi32
            user32
            shlwapi
    )

    if(MSVC)
        target_compile_options(MyanglishIME PRIVATE /utf-8)
    endif()

    # Keep development data beside the DLL. resolveDataRoot() checks this
    # location first, so the installed TIP does not depend on the console CWD.
    add_custom_command(
        TARGET MyanglishIME
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "$<TARGET_FILE_DIR:MyanglishIME>/data"
        COMMENT "Copying Myanglish dictionary and rule data beside the IME DLL"
    )
endif()

###########################################################
# Prototype
###########################################################

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

###########################################################
# Tests
###########################################################

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
