if(NATIVE_AUDIO_AVAILABLE)
    if(APPLE)
        set(AUDIO_ENGINE_LIBRARY_SPECIFIC_FILES
            MacAudioEngine.cpp
            MacAudioDevice.cpp
        )
    elseif(WIN32)
        set(AUDIO_ENGINE_LIBRARY_SPECIFIC_FILES
            WASAPIAudioEngine.cpp
            WASAPIAudioDevice.cpp
        )
    elseif(LINUX)
        if (DBus_FOUND)
            set(RTKIT_FILES rtkit.c)
        endif (DBus_FOUND)

        set(AUDIO_ENGINE_LIBRARY_SPECIFIC_FILES
            PulseAudioDevice.cpp
            PulseAudioEngine.cpp
	    ${RTKIT_FILES}
        )
    endif()
else()
    set(AUDIO_ENGINE_LIBRARY_SPECIFIC_FILES
        PortAudioDevice.cpp
        PortAudioEngine.cpp
        PortAudioInterface.cpp
    )
endif(NATIVE_AUDIO_AVAILABLE)

add_library(fdv_audio STATIC
    AudioDeviceSpecification.cpp
    AudioEngineFactory.cpp
    IAudioDevice.cpp
    IAudioEngine.cpp
    ${AUDIO_ENGINE_LIBRARY_SPECIFIC_FILES}
)

target_link_libraries(fdv_audio PUBLIC fdv_util)

if(APPLE AND NATIVE_AUDIO_AVAILABLE)
    target_link_libraries(fdv_audio PUBLIC
        "-framework AVFoundation"
        "-framework CoreAudio")
elseif(WIN32 AND NATIVE_AUDIO_AVAILABLE)
    target_link_libraries(fdv_audio PRIVATE uuid avrt winmm ksuser)
elseif(LINUX AND NATIVE_AUDIO_AVAILABLE AND DBus_FOUND)
    target_include_directories(fdv_audio PRIVATE ${DBus_INCLUDE_DIRS})
    target_compile_definitions(fdv_audio PRIVATE -DUSE_RTKIT)
    target_link_libraries(fdv_audio PRIVATE ${DBus_LIBRARIES})
endif()

target_compile_options(fdv_audio PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})

if(BOOTSTRAP_WXWIDGETS)
    add_dependencies(fdv_audio wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
    target_compile_definitions(fdv_audio PRIVATE ${WXBUILD_BUILD_DEFS})
    target_include_directories(fdv_audio PRIVATE ${WXBUILD_INCLUDES})
endif(BOOTSTRAP_WXWIDGETS)

if(UNITTEST AND LINUX AND NATIVE_AUDIO_AVAILABLE)
    # PulseAudioDevice and PulseAudioEngine are only built (and only used)
    # on Linux.
    DefineFreeDVUnitTest(audio PulseAudioDeviceTest fdv_audio fdv_util ${FREEDV_LINK_LIBS})
    target_include_directories(PulseAudioDeviceTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/..)
    FreeDVUnitTestUsesWxWidgets(PulseAudioDeviceTest)
endif()

if(UNITTEST AND WIN32 AND NATIVE_AUDIO_AVAILABLE)
    # The wait between WASAPI audio thread cycles (header-only).
    DefineFreeDVUnitTest(audio WASAPIRealtimeWaitTest winmm)
endif()

# The factory itself is built on every platform; only the engine it makes
# differs.
if(UNITTEST)
    DefineFreeDVUnitTest(audio AudioEngineFactoryTest fdv_audio fdv_util ${FREEDV_LINK_LIBS})
    FreeDVUnitTestUsesWxWidgets(AudioEngineFactoryTest)
endif()
