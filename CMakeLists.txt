cmake_minimum_required(VERSION 3.20)

project(
    MyanglishIME
    VERSION 1.0.1.0
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
        ime/CandidateWindow.cpp
        ime/ClassFactory.cpp
        ime/CompositionManager.cpp
        ime/DllMain.cpp
        ime/Globals.cpp
        ime/Guids.cpp
        ime/KeyEventSink.cpp
        ime/LanguageBarButton.cpp
        ime/PersonalDictionaryManager.cpp
        ime/Registration.cpp
        ime/TextService.cpp
        ime/ThreadMgrEventSink.cpp
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
            NOMINMAX
            WIN32_LEAN_AND_MEAN
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
            normaliz
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
# Settings app (Windows)
###########################################################

if(WIN32)
    add_executable(MyanglishSettings WIN32
        settings/SettingsMain.cpp
        settings/PackagedTsfBootstrap.cpp
        ime/Guids.cpp
    )

    target_include_directories(MyanglishSettings
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/settings"
            "${CMAKE_CURRENT_SOURCE_DIR}/ime"
    )

    target_compile_features(MyanglishSettings PRIVATE cxx_std_17)
    target_compile_definitions(MyanglishSettings PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(MyanglishSettings PRIVATE user32 ole32 uuid)

    if(MSVC)
        target_compile_options(MyanglishSettings PRIVATE /utf-8)
    endif()
endif()

###########################################################
# Elevated installer prototype (Windows)
###########################################################

if(WIN32)
    # --------------------------------------------------
    # Standalone installer payload
    # --------------------------------------------------

    set(MYANGLISH_INSTALLER_PAYLOAD_DIR
        "${CMAKE_CURRENT_BINARY_DIR}/installer_payload/$<CONFIG>"
    )

    add_custom_target(MyanglishInstallerPayload
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${MYANGLISH_INSTALLER_PAYLOAD_DIR}"

        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${MYANGLISH_INSTALLER_PAYLOAD_DIR}/data"

        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:MyanglishIME>"
            "${MYANGLISH_INSTALLER_PAYLOAD_DIR}/MyanglishIME.dll"

        COMMAND ${CMAKE_COMMAND} -E copy
            "$<TARGET_FILE:MyanglishSettings>"
            "${MYANGLISH_INSTALLER_PAYLOAD_DIR}/MyanglishSettings.exe"

        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/data"
            "${MYANGLISH_INSTALLER_PAYLOAD_DIR}/data"

        DEPENDS
            MyanglishIME
            MyanglishSettings
    )

    file(GENERATE
        OUTPUT
            "${CMAKE_CURRENT_BINARY_DIR}/InstallerResources-$<CONFIG>.rc"
        CONTENT
"#include \"${CMAKE_CURRENT_SOURCE_DIR}/installer/Resource.h\"

IDR_MYANGLISH_IME RCDATA \"${CMAKE_CURRENT_BINARY_DIR}/installer_payload/$<CONFIG>/MyanglishIME.dll\"
IDR_MYANGLISH_SETTINGS RCDATA \"${CMAKE_CURRENT_BINARY_DIR}/installer_payload/$<CONFIG>/MyanglishSettings.exe\"

IDR_DATA_1000 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/alpha08_candidates.csv\"
IDR_DATA_1001 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/dictionary.csv\"
IDR_DATA_1002 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/historical_candidates.csv\"
IDR_DATA_1003 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/imported_lexicon_alpha1083_pack1.csv\"
IDR_DATA_1004 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/imported_lexicon_alpha1083_pack2.csv\"
IDR_DATA_1005 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/lexicon/burmese_lexicon.txt\"
IDR_DATA_1006 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/lexicon/README.md\"
IDR_DATA_1007 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/myanglish_rules/ranked_suffixes_alpha07.csv\"
IDR_DATA_1008 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/myanglish_rules/ranked_suffixes_alpha08.csv\"
IDR_DATA_1009 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/myanglish_rules/rhymes_master_v1.csv\"
IDR_DATA_1010 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/myanglish_rules/suffix_families_alpha08.csv\"
IDR_DATA_1011 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/ranked_candidates_alpha0109.csv\"
IDR_DATA_1012 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/rules/finals.csv\"
IDR_DATA_1013 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/rules/marks.csv\"
IDR_DATA_1014 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/rules/medials.csv\"
IDR_DATA_1015 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/rules/rhymes.csv\"
IDR_DATA_1016 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/rules/tone_marks.csv\"
IDR_DATA_1017 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/user_dictionary.example.csv\"
IDR_DATA_1018 RCDATA \"${CMAKE_CURRENT_SOURCE_DIR}/data/vowel.csv\"
"
    )
    add_executable(MyanglishSetup WIN32
        installer/SetupMain.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/InstallerResources-$<CONFIG>.rc"
    )

    add_dependencies(MyanglishSetup MyanglishInstallerPayload)

    set_target_properties(
        MyanglishSetup
        PROPERTIES
            OUTPUT_NAME "MyanglishInstaller"
    )

    target_compile_features(MyanglishSetup PRIVATE cxx_std_17)
    target_compile_definitions(MyanglishSetup PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(MyanglishSetup PRIVATE user32 shell32)

    if(MSVC)
        target_compile_options(MyanglishSetup PRIVATE /utf-8)
        target_link_options(
            MyanglishSetup
            PRIVATE
                "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
        )
    endif()
endif()

###########################################################
# Microsoft Store launcher (Windows)
###########################################################

if(WIN32)
    add_executable(MyanglishStoreLauncher WIN32
        store_launcher/LauncherMain.cpp
    )

    target_compile_features(MyanglishStoreLauncher PRIVATE cxx_std_17)
    target_compile_definitions(MyanglishStoreLauncher PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(MyanglishStoreLauncher PRIVATE user32 shell32 urlmon)

    if(MSVC)
        target_compile_options(MyanglishStoreLauncher PRIVATE /utf-8)
        target_link_options(
            MyanglishStoreLauncher
            PRIVATE
                "/MANIFESTUAC:level='asInvoker' uiAccess='false'"
        )
    endif()
endif()

###########################################################
# Prototype
###########################################################

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/prototype/main.cpp")
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
