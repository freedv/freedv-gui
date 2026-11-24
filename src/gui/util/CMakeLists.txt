add_library(fdv_gui_util STATIC
    AppIcon.cpp
    GridSquare.cpp
    LabelOverrideAccessible.cpp
    NameOverrideAccessible.cpp
    SerialPortList.cpp
    TabLayoutSerializer.cpp
    wxListViewComboPopup.cpp
    WindowPlacement.cpp
    WindowPositionRestore.cpp)

target_compile_options(fdv_gui_util PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})
target_link_libraries(fdv_gui_util PUBLIC fdv_util)

# The icon's images, for the windows (see AppIcon.cpp). Windows has them in
# its resources, and macOS in the app bundle.
if(UNIX AND NOT APPLE)
    include(${CMAKE_SOURCE_DIR}/cmake/EmbedFiles.cmake)
    set(FREEDV_ICON_IMAGES)
    foreach(size 16 24 32 48)
        list(APPEND FREEDV_ICON_IMAGES ${CMAKE_SOURCE_DIR}/contrib/icon/Windows/${size}.png)
    endforeach(size)
    foreach(size 64 96 128 256)
        list(APPEND FREEDV_ICON_IMAGES ${CMAKE_SOURCE_DIR}/contrib/icon/Linux/${size}.png)
    endforeach(size)
    freedv_embed_files(${CMAKE_CURRENT_BINARY_DIR}/freedv_icon_images.h FREEDV_ICON_IMAGES ${FREEDV_ICON_IMAGES})
    target_include_directories(fdv_gui_util PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endif(UNIX AND NOT APPLE)

if(BOOTSTRAP_WXWIDGETS)
    add_dependencies(fdv_gui_util wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
    target_compile_definitions(fdv_gui_util PRIVATE ${WXBUILD_BUILD_DEFS})
    target_include_directories(fdv_gui_util PRIVATE ${WXBUILD_INCLUDES})
endif(BOOTSTRAP_WXWIDGETS)

if(UNITTEST)
# Header-only; only needs what HamlibRigController.h itself needs.
DefineFreeDVUnitTest(gui_util FrequencyOpsTest fdv_util)
if(HAMLIB_ADD_DEPENDENCY)
    add_dependencies(FrequencyOpsTest hamlib)
endif(HAMLIB_ADD_DEPENDENCY)

# Header-only.
DefineFreeDVUnitTest(gui_util FilterFrequencyTest fdv_util)

DefineFreeDVUnitTest(gui_util GridSquareTest fdv_gui_util ${FREEDV_LINK_LIBS})
FreeDVUnitTestUsesWxWidgets(GridSquareTest)

DefineFreeDVUnitTest(gui_util SerialPortListTest fdv_gui_util ${FREEDV_LINK_LIBS})
FreeDVUnitTestUsesWxWidgets(SerialPortListTest)

DefineFreeDVUnitTest(gui_util WindowPlacementTest fdv_gui_util ${FREEDV_LINK_LIBS})
FreeDVUnitTestUsesWxWidgets(WindowPlacementTest)

DefineFreeDVUnitTest(gui_util TabLayoutSerializerTest fdv_gui_util ${FREEDV_LINK_LIBS})
target_include_directories(TabLayoutSerializerTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
FreeDVUnitTestUsesWxWidgets(TabLayoutSerializerTest wx::aui)
endif(UNITTEST)
