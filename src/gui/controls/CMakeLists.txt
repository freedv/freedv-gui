add_library(fdv_gui_controls STATIC
    plot.cpp
    plot_scalar.cpp
    plot_scatter.cpp
    plot_spectrum.cpp
    plot_waterfall.cpp
    ReportMessageRenderer.cpp
    MsgListPopup.cpp)

if(APPLE)
    target_sources(fdv_gui_controls PRIVATE plot_osx.mm plot_waterfall_osx.mm)

    # wx/defs.h names the deprecated NSOpenGL* types when compiled as Objective-C++.
    set_source_files_properties(plot_osx.mm plot_waterfall_osx.mm PROPERTIES COMPILE_OPTIONS -Wno-deprecated-declarations)
endif(APPLE)

target_include_directories(fdv_gui_controls PRIVATE ${CODEC2_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/../.. ${CMAKE_CURRENT_BINARY_DIR}/../..)
add_dependencies(fdv_gui_controls codec2)
target_compile_options(fdv_gui_controls PRIVATE ${WARNINGS_AS_ERRORS_FLAGS})
target_link_libraries(fdv_gui_controls PUBLIC fdv_util)

if(BOOTSTRAP_WXWIDGETS)
    add_dependencies(fdv_gui_controls wx::core wx::base wx::aui wx::html wx::net wx::adv wx::propgrid)
    target_compile_definitions(fdv_gui_controls PRIVATE ${WXBUILD_BUILD_DEFS})
    target_include_directories(fdv_gui_controls PRIVATE ${WXBUILD_INCLUDES})
endif(BOOTSTRAP_WXWIDGETS)


# PlotScalar draws its plot area through cairo directly when wxGTK uses it (see
# PLOT_SCALAR_CAIRO_SURFACE in plot_scalar.cpp). Unlike including GTK's own headers,
# this is safe whichever GTK version wxWidgets was built for.
if(LINUX)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CAIRO QUIET cairo)
        if(CAIRO_FOUND)
            target_compile_definitions(fdv_gui_controls PRIVATE HAS_CAIRO)
            target_include_directories(fdv_gui_controls PRIVATE ${CAIRO_INCLUDE_DIRS})

            # Link cairo explicitly rather than relying on it arriving via GTK3's
            # libraries (only linked when gtk+-3.0.pc is found); modern ld won't
            # resolve symbols through wxWidgets' own indirect dependency on it.
            target_link_libraries(fdv_gui_controls PUBLIC ${CAIRO_LINK_LIBRARIES})
        endif(CAIRO_FOUND)
    endif(PkgConfig_FOUND)
endif(LINUX)

if(UNITTEST)
    # Header-only logic, so the test needs none of the plot classes.
    DefineFreeDVUnitTest(gui_controls WaterfallScaleTest)
endif(UNITTEST)
