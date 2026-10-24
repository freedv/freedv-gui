add_library(fdv_config STATIC
    AudioConfiguration.cpp
    FilterConfiguration.cpp
    FreeDVConfiguration.cpp
    ReportingConfiguration.cpp
    RigControlConfiguration.cpp
    WxWidgetsConfigStore.cpp
)

target_include_directories(fdv_config PRIVATE ${CODEC2_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/..)
target_compile_options(fdv_config PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})
target_link_libraries(fdv_config PUBLIC fdv_util fdv_audio_pipeline_extra)

if(BOOTSTRAP_WXWIDGETS)
    add_dependencies(fdv_config wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
    target_compile_definitions(fdv_config PRIVATE ${WXBUILD_BUILD_DEFS})
    target_include_directories(fdv_config PRIVATE ${WXBUILD_INCLUDES})
endif(BOOTSTRAP_WXWIDGETS)

if(UNITTEST)
# Tests that need wxWidgets (but not a display).
macro(DefineConfigUnitTest utName)
    DefineFreeDVUnitTest(config ${utName} fdv_config ${FREEDV_LINK_LIBS})
    target_include_directories(${utName} PRIVATE ${CODEC2_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/..)
    FreeDVUnitTestUsesWxWidgets(${utName})
endmacro()

DefineConfigUnitTest(ConfigurationDataElementTest)
DefineConfigUnitTest(ConfigMigrationTest)
DefineConfigUnitTest(ReportingFrequencyListTest)
DefineConfigUnitTest(WxWidgetsConfigStoreTest)
DefineConfigUnitTest(RigControlConfigurationTest)
endif(UNITTEST)
