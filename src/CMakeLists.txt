set(WARNINGS_AS_ERRORS_FLAGS -Wall -Wextra -Werror -Wno-null-pointer-subtraction)

set(FREEDV_SOURCES
    main.cpp
    topFrame.cpp
    sox_biquad.c
    util.cpp
    eq.cpp
    eq_design.cpp
    voicekeyer.cpp
    voicekeyer_cache.cpp
    playrec.cpp
    ongui.cpp
    freedv_interface.cpp
    ${CMAKE_BINARY_DIR}/generated/build_date.cpp
)

# Lets GUI test scripts (test/gui) drive FreeDV. Not for release builds.
if(UNITTEST)
    list(APPEND FREEDV_SOURCES test_agent.cpp)
    if(APPLE)
        list(APPEND FREEDV_SOURCES test_agent_osx.mm)
        # (activateIgnoringOtherApps: is deprecated from macOS 14 on, but
        # still does what's needed here.)
        set_source_files_properties(test_agent_osx.mm PROPERTIES COMPILE_OPTIONS -Wno-deprecated-declarations)
    endif(APPLE)
endif(UNITTEST)

set(FREEDV_LINK_LIBS_OSX
    "-framework AVFoundation"
)

# Configure logging
add_definitions(-DULOG_HAVE_TIME)
add_definitions(-DULOG_NO_COLOR)

# Include generated headers
include_directories(${CMAKE_BINARY_DIR}/generated)

# Builds every unit test (and only what they need), e.g. for running them
# on a machine other than the one they were built on.
if(UNITTEST AND NOT TARGET unit_tests)
    add_custom_target(unit_tests)
endif()

# Defines a unit test executable (test/<utName>.cpp) that links against the given
# libraries and registers it with CTest as <prefix>_<utName>.
macro(DefineFreeDVUnitTest prefix utName)
    add_executable(${utName} test/${utName}.cpp)
    add_dependencies(unit_tests ${utName})
    set_property(GLOBAL APPEND PROPERTY FREEDV_UNIT_TEST_TARGETS ${utName})
    target_link_libraries(${utName} PRIVATE ${ARGN})
    target_compile_options(${utName} PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})

    # Needed to find codec2/RADE when they're built as part of FreeDV.
    set_target_properties(
        ${utName}
        PROPERTIES BUILD_RPATH "${CMAKE_BUILD_RPATH};${CODEC2_BUILD_DIR}/src;${rade_BINARY_DIR}/src")

    add_test(NAME ${prefix}_${utName} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${utName})
    freedv_set_test_pass_criteria(${prefix}_${utName})
endmacro()

# Sets up a unit test that uses wxWidgets when wxWidgets is being built as part
# of FreeDV. (Otherwise, wxWidgets comes in via FREEDV_LINK_LIBS.)
macro(FreeDVUnitTestUsesWxWidgets utName)
    if(BOOTSTRAP_WXWIDGETS)
        target_compile_definitions(${utName} PRIVATE ${WXBUILD_BUILD_DEFS})
        target_include_directories(${utName} PRIVATE ${WXBUILD_INCLUDES})
        target_link_libraries(${utName} PRIVATE wx::core wx::base ${ARGN})
    endif(BOOTSTRAP_WXWIDGETS)

    if(WIN32)
        # Provides the manifest that wxWidgets needs (see the file for why).
        target_sources(${utName} PRIVATE ${CMAKE_SOURCE_DIR}/src/test/UnitTest.rc)
    endif(WIN32)
endmacro()

# Compile FreeDV components
add_subdirectory(audio)
add_subdirectory(config)
add_subdirectory(gui)
add_subdirectory(logging)
add_subdirectory(pipeline)
add_subdirectory(rig_control)
add_subdirectory(sox)

if(UNITTEST)
DefineFreeDVUnitTest(gui EqDesignTest fdv_sox fdv_util)
target_sources(EqDesignTest PRIVATE eq_design.cpp sox_biquad.c)

DefineFreeDVUnitTest(gui VoiceKeyerFileCacheTest fdv_util ${FREEDV_LINK_LIBS})
target_sources(VoiceKeyerFileCacheTest PRIVATE voicekeyer_cache.cpp)
target_include_directories(VoiceKeyerFileCacheTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
FreeDVUnitTestUsesWxWidgets(VoiceKeyerFileCacheTest)
endif(UNITTEST)

# WIN32 is needed for Windows GUI apps and is ignored for UNIX like systems.
# In addition, there are some required OSX-specific code files for platform specific handling.
if(APPLE)
    set(RES_FILES freedv.icns)
    set_source_files_properties(freedv.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # Translations go in Contents/Resources/<lang>.lproj, where wxWidgets
    # looks for them by default.
    set(FREEDV_BUNDLE_LOCALIZATIONS "<string>en</string>")
    foreach(lang ${FREEDV_LINGUAS})
        string(APPEND FREEDV_BUNDLE_LOCALIZATIONS "\n\t\t<string>${lang}</string>")
    endforeach(lang)
    foreach(mo_file ${FREEDV_MO_FILES})
        get_filename_component(mo_dir ${mo_file} DIRECTORY)
        get_filename_component(mo_dir ${mo_dir} DIRECTORY)
        get_filename_component(lang ${mo_dir} NAME)
        set_source_files_properties(${mo_file} PROPERTIES
            GENERATED TRUE
            MACOSX_PACKAGE_LOCATION "Resources/${lang}.lproj")
        list(APPEND RES_FILES ${mo_file})
    endforeach(mo_file)
    add_executable(FreeDV MACOSX_BUNDLE ${FREEDV_SOURCES} ${RES_FILES})
    set_target_properties(FreeDV PROPERTIES
        BUNDLE True
        MACOSX_BUNDLE_GUI_IDENTIFIER org.freedv.freedv
        MACOSX_BUNDLE_BUNDLE_NAME FreeDV
        MACOSX_BNUDLE_COPYRIGHT "Copyright (c) 2025 FreeDV"
        MACOSX_BUNDLE_BUNDLE_VERSION "${FreeDV_VERSION}"
        MACOSX_BUNDLE_BUNDLE_SHORT_VERSION_STRING "${FreeDV_VERSION}"
        MACOSX_BUNDLE_BUNDLE_LONG_VERSION_STRING "${FreeDV_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "freedv"
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/src/info.plist.in
    )
else()
    add_executable(freedv WIN32 ${FREEDV_SOURCES} ${RES_FILES})
endif(APPLE)

if(UNITTEST)
    if(APPLE)
        target_compile_definitions(FreeDV PRIVATE ENABLE_TEST_AGENT)
    else(APPLE)
        target_compile_definitions(freedv PRIVATE ENABLE_TEST_AGENT)
    endif(APPLE)
endif(UNITTEST)

if(TARGET freedv_translations)
    if(APPLE)
        add_dependencies(FreeDV freedv_translations)
    else(APPLE)
        add_dependencies(freedv freedv_translations)
    endif(APPLE)
endif(TARGET freedv_translations)

if(APPLE)
target_compile_options(FreeDV PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})
target_link_libraries(FreeDV fdv_audio fdv_audio_pipeline fdv_audio_pipeline_extra fdv_config fdv_gui_controls fdv_gui_dialogs fdv_gui_util fdv_logging fdv_os_wrapper fdv_rig_control fdv_reporting fdv_sox fdv_util git_version)
else(APPLE)
target_compile_options(freedv PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})
target_link_libraries(freedv fdv_audio fdv_audio_pipeline fdv_audio_pipeline_extra fdv_config fdv_gui_controls fdv_gui_dialogs fdv_gui_util fdv_logging fdv_os_wrapper fdv_rig_control fdv_reporting fdv_sox fdv_util git_version)
endif(APPLE)

# Add dependencies for other statically built libraries, if applicable.
if(FREEDV_STATIC_DEPS)
    if(APPLE)
    add_dependencies(FreeDV ${FREEDV_STATIC_DEPS})
    else(APPLE)
    add_dependencies(freedv ${FREEDV_STATIC_DEPS})
    endif(APPLE)
    add_dependencies(fdv_audio ${FREEDV_STATIC_DEPS})
    add_dependencies(fdv_audio_pipeline_extra ${FREEDV_STATIC_DEPS})
    add_dependencies(fdv_sox ${FREEDV_STATIC_DEPS})
endif(FREEDV_STATIC_DEPS)

# Link other dependencies
if(APPLE)
target_link_libraries(FreeDV ${FREEDV_LINK_LIBS} ${FREEDV_LINK_LIBS_OSX})
elseif(WIN32)
target_link_libraries(freedv ${FREEDV_LINK_LIBS} ws2_32)
else()
target_link_libraries(freedv ${FREEDV_LINK_LIBS})
endif(APPLE)

if(BOOTSTRAP_WXWIDGETS)
    if(APPLE)
        target_compile_definitions(FreeDV PRIVATE ${WXBUILD_BUILD_DEFS})
        target_include_directories(FreeDV PRIVATE ${WXBUILD_INCLUDES})
        
        add_dependencies(FreeDV wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
        target_link_libraries(FreeDV wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
    else(APPLE)
        target_compile_definitions(freedv PRIVATE ${WXBUILD_BUILD_DEFS})
        target_include_directories(freedv PRIVATE ${WXBUILD_INCLUDES})
        
        add_dependencies(freedv wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
        target_link_libraries(freedv wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
    endif(APPLE)
endif(BOOTSTRAP_WXWIDGETS)

# For older Xcode (< 9.0), bypass usage of @available.
if(APPLE)
if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0.0.900037)
add_definitions(-DAPPLE_OLD_XCODE)
endif(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0.0.900037)
endif(APPLE)

# Precompile <wx/wx.h> for the targets whose sources mostly include it;
# parsing it dominates their compile times. C and Objective-C++ sources
# are excluded.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)
    foreach(_target FreeDV freedv fdv_gui_controls fdv_gui_dialogs fdv_config fdv_audio_pipeline_extra)
        if(TARGET ${_target})
            target_precompile_headers(${_target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:<wx/wx.h$<ANGLE-R>>)
        endif(TARGET ${_target})
    endforeach()
endif(CMAKE_VERSION VERSION_GREATER_EQUAL 3.16)

# If on Linux, force static libstdc++. This is to prevent versioning
# problems when using AppImages.
if(LINUX AND STATIC_LIBGCC)
    target_link_options(freedv PRIVATE -static-libgcc -static-libstdc++)
endif(LINUX AND STATIC_LIBGCC)

# On Linux/GTK builds, make the GTK3 headers directly accessible in main.cpp
# for the button-flash suppression code.
if(LINUX)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK3 QUIET gtk+-3.0)
        if(GTK3_FOUND)
            # gtk+-3.0.pc being present only means GTK3 *headers* exist
            # somewhere on the system -- it says nothing about whether the
            # wxWidgets we're linking against was itself built against GTK3.
            # wx/defs.h unconditionally forward-declares a stand-in
            # `GdkWindow` typedef whenever __WXGTK__ is set, and picks
            # between the real GTK3 layout (struct _GdkWindow) or the
            # legacy GTK2 one (struct _GdkDrawable) based on whether
            # __WXGTK3__ is *also* defined by wx's own (toolkit-specific)
            # setup.h. If wx was actually built for GTK2 -- e.g. a system
            # with both gtk2-devel and gtk3-devel installed, where wx-config
            # resolves to a GTK2 build -- directly #include-ing <gtk/gtk.h>
            # alongside wx headers gets two conflicting GdkWindow
            # declarations in the same translation unit, regardless of
            # include order. Probe for __WXGTK3__ using wx's own headers
            # before trusting the independent pkg-config detection.
            include(CheckCXXSymbolExists)
            set(CMAKE_REQUIRED_INCLUDES ${wxWidgets_INCLUDE_DIRS})
            # wxWidgets_DEFINITIONS is a list of bare macro tokens (correct
            # for the COMPILE_DEFINITIONS property UsewxWidgets.cmake sets
            # it via) -- but CMAKE_REQUIRED_DEFINITIONS needs full "-DFOO"
            # strings. Without the prefix, check_cxx_symbol_exists's probe
            # compile passes each bare token as a raw command-line argument,
            # which the compiler treats as a (nonexistent) input filename
            # and errors out on -- silently failing the whole check closed
            # (WX_BUILT_FOR_GTK3 always false) even on a real GTK3 wx build.
            set(WX_GTK3_PROBE_DEFINITIONS ${wxWidgets_DEFINITIONS})
            list(TRANSFORM WX_GTK3_PROBE_DEFINITIONS PREPEND "-D")
            set(CMAKE_REQUIRED_DEFINITIONS ${WX_GTK3_PROBE_DEFINITIONS})
            set(CMAKE_REQUIRED_FLAGS "${wxWidgets_CXX_FLAGS}")
            check_cxx_symbol_exists(__WXGTK3__ "wx/defs.h" WX_BUILT_FOR_GTK3)
            unset(CMAKE_REQUIRED_INCLUDES)
            unset(CMAKE_REQUIRED_DEFINITIONS)
            unset(CMAKE_REQUIRED_FLAGS)

            if(WX_BUILT_FOR_GTK3)
                message(STATUS "GTK3 development files found, will implement Linux-specific GUI workarounds")
                target_include_directories(freedv PRIVATE ${GTK3_INCLUDE_DIRS})
                target_link_libraries(freedv ${GTK3_LIBRARIES})
                add_definitions(-DHAS_GTK3)

                # fdv_gui_util (added via add_subdirectory(gui) above, before this
                # point) needs these too -- WindowPositionRestore.cpp lives there,
                # not in freedv's own sources. add_definitions()/directory-scoped
                # include dirs only propagate to subdirectories added *after* this
                # point, so it has to be set on the already-created target directly.
                target_compile_definitions(fdv_gui_util PRIVATE HAS_GTK3)
                target_include_directories(fdv_gui_util PRIVATE ${GTK3_INCLUDE_DIRS})
            else(WX_BUILT_FOR_GTK3)
                message(WARNING "GTK3 development files (gtk+-3.0.pc) were found, but the detected wxWidgets does not define __WXGTK3__ (it appears to target GTK2) -- including <gtk/gtk.h> directly alongside wx headers would conflict, so Linux-specific GUI workarounds (including window position restore) will be disabled")
            endif(WX_BUILT_FOR_GTK3)
        else(GTK3_FOUND)
            message(WARNING "GTK3 development files (gtk+-3.0.pc, e.g. package libgtk-3-dev) not found -- Linux-specific GUI workarounds (including window position restore) will be disabled")
        endif(GTK3_FOUND)
    else(PkgConfig_FOUND)
        message(WARNING "pkg-config not found -- cannot detect GTK3 development files, Linux-specific GUI workarounds (including window position restore) will be disabled")
    endif(PkgConfig_FOUND)
endif(LINUX)

# If a release tag's set, pass it to the compiler.
if(FREEDV_VERSION_TAG)
add_definitions(-DFREEDV_VERSION_TAG="${FREEDV_VERSION_TAG}")
endif(FREEDV_VERSION_TAG)

# Insert source and generated header directories before other search directories.
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

if(APPLE)
install(TARGETS FreeDV
    BUNDLE DESTINATION bin
)
else(APPLE)
install(TARGETS freedv
    RUNTIME DESTINATION bin
)
endif(APPLE)

# Custom commands to build OSX images.
if(APPLE)
    if(NOT MACOS_CODESIGN_IDENTITY)
        set(MACOS_CODESIGN_IDENTITY -)
    endif(NOT MACOS_CODESIGN_IDENTITY)

    if (NOT USE_NATIVE_AUDIO)
        set(PORTAUDIO_BUNDLE_ARG -s ${portaudio_BINARY_DIR})
    endif(NOT USE_NATIVE_AUDIO)
    
    # Get folder containing lib*san so we don't need to enter it to macdylibbundler.
    if(ENABLE_ASAN)
        set(SANITIZER_LIB_NAME libclang_rt.asan_osx_dynamic.dylib)
    elseif(ENABLE_RTSAN)
        set(SANITIZER_LIB_NAME libclang_rt.rtsan_osx_dynamic.dylib)
    elseif(ENABLE_TSAN)
        set(SANITIZER_LIB_NAME libclang_rt.tsan_osx_dynamic.dylib)
    elseif(ENABLE_UBSAN)
        set(SANITIZER_LIB_NAME libclang_rt.ubsan_osx_dynamic.dylib)
    else()
        set(SANITIZER_LIB_NAME libclang_rt.asan_osx_dynamic.dylib)
    endif()

    # Get folder containing libasan so we don't need to enter it to macdylibbundler.
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -print-file-name=${SANITIZER_LIB_NAME}
        OUTPUT_VARIABLE CLANG_SANITIZER_FILE
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    
    add_custom_command(
        TARGET FreeDV
        POST_BUILD
        COMMAND rm -rf dist_tmp FreeDV.dmg || true
        COMMAND DYLD_LIBRARY_PATH=${portaudio_BINARY_DIR}:${DYLD_LIBRARY_PATH} ${CMAKE_SOURCE_DIR}/macdylibbundler/dylibbundler ARGS -od -b -x FreeDV.app/Contents/MacOS/FreeDV -d FreeDV.app/Contents/libs -p @loader_path/../libs/ -i /usr/lib ${PORTAUDIO_BUNDLE_ARG} -s ${rade_BINARY_DIR}/src -s `dirname ${CLANG_SANITIZER_FILE}` -ns
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/clean-duplicate-rpaths.sh FreeDV.app/
        COMMAND cp ARGS ${CMAKE_CURRENT_SOURCE_DIR}/freedv.icns FreeDV.app/Contents/Resources
        COMMAND rm ARGS -rf FreeDV.app/Contents/Frameworks
        COMMAND mkdir ARGS FreeDV.app/Contents/Frameworks
        
        # Codesign binary so that it can execute
        #COMMAND codesign --force --options runtime --timestamp --entitlements ${CMAKE_CURRENT_SOURCE_DIR}/entitlements.plist --sign ${MACOS_CODESIGN_IDENTITY} ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app `find ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app -name '*.so' -o -name '*.dylib'` ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app/Contents/MacOS/FreeDV 
        COMMAND codesign --force --options runtime --timestamp --entitlements ${CMAKE_CURRENT_SOURCE_DIR}/entitlements.plist --sign ${MACOS_CODESIGN_IDENTITY} ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app `find ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app -name '*.so' -o -name '*.dylib'` ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app/Contents/MacOS/FreeDV 
    )

    if (MACOS_CODESIGN_KEYCHAIN_PROFILE)
        add_custom_target(release
            COMMAND ${CMAKE_SOURCE_DIR}/cmake/macos_build_dmg.sh ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app
            COMMAND codesign --timestamp -i org.freedv.freedv --sign ${MACOS_CODESIGN_IDENTITY} FreeDV.dmg
            COMMAND ${CMAKE_SOURCE_DIR}/cmake/macos_notarize.sh ./FreeDV.dmg ${MACOS_CODESIGN_KEYCHAIN_PROFILE}
            COMMAND xcrun stapler staple ./FreeDV.dmg
            DEPENDS FreeDV)
    else(MACOS_CODESIGN_KEYCHAIN_PROFILE)
        add_custom_target(release
            COMMAND ${CMAKE_SOURCE_DIR}/cmake/macos_build_dmg.sh ${CMAKE_CURRENT_BINARY_DIR}/FreeDV.app
            DEPENDS FreeDV)
    endif (MACOS_CODESIGN_KEYCHAIN_PROFILE)
endif(APPLE)

# Custom commands for signing Windows binaries
if(SIGN_WINDOWS_BINARIES)
    # Add custom command to sign freedv.exe.
    if (PKCS11_SECONDARY_CERTIFICATE)
        add_custom_command(
            TARGET freedv 
            POST_BUILD
            COMMAND mv $<TARGET_FILE:freedv> $<TARGET_FILE:freedv>.tmp
            COMMAND ${OSSLSIGNCODE_BIN} ARGS sign -pkcs11engine ${PKCS11_ENGINE} -pkcs11module ${PKCS11_MODULE} -pkcs11cert "${PKCS11_CERTIFICATE}" -key "${PKCS11_KEY}" -h ${SIGN_HASH} -ts ${TIMESTAMP_SERVER} -comm -ac ${INTERMEDIATE_CERT_FILE} -n "FreeDV - ${PROJECT_DESCRIPTION}" -i "https://freedv.org/" -in $<TARGET_FILE:freedv>.tmp -out $<TARGET_FILE:freedv>
            COMMAND mv $<TARGET_FILE:freedv> $<TARGET_FILE:freedv>.tmp
            COMMAND ${OSSLSIGNCODE_BIN} ARGS sign -nest -pkcs11engine ${PKCS11_ENGINE} -pkcs11module ${PKCS11_MODULE} -pkcs11cert "${PKCS11_SECONDARY_CERTIFICATE}" -key "${PKCS11_SECONDARY_KEY}" -h ${SIGN_HASH} -ts ${TIMESTAMP_SERVER} -comm -ac ${INTERMEDIATE_CERT_FILE} -n "FreeDV - ${PROJECT_DESCRIPTION}" -i "https://freedv.org/" -in $<TARGET_FILE:freedv>.tmp -out $<TARGET_FILE:freedv>
            COMMAND rm $<TARGET_FILE:freedv>.tmp
            VERBATIM)
    else()
        add_custom_command(
            TARGET freedv 
            POST_BUILD
            COMMAND mv $<TARGET_FILE:freedv> $<TARGET_FILE:freedv>.tmp
            COMMAND ${OSSLSIGNCODE_BIN} ARGS sign -pkcs11engine ${PKCS11_ENGINE} -pkcs11module ${PKCS11_MODULE} -pkcs11cert "${PKCS11_CERTIFICATE}" -key "${PKCS11_KEY}" -h ${SIGN_HASH} -ts ${TIMESTAMP_SERVER} -comm -ac ${INTERMEDIATE_CERT_FILE} -n "FreeDV - ${PROJECT_DESCRIPTION}" -i "https://freedv.org/" -in $<TARGET_FILE:freedv>.tmp -out $<TARGET_FILE:freedv>
            COMMAND rm $<TARGET_FILE:freedv>.tmp
            VERBATIM)
    endif (PKCS11_SECONDARY_CERTIFICATE)
endif(SIGN_WINDOWS_BINARIES)
