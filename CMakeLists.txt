cmake_minimum_required(VERSION 3.19)
project(CurveMaker LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

qt_add_executable(CurveMaker
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    curvewidget.cpp
    curvewidget.h
    setcurvestatecommand.h setcurvestatecommand.cpp
    themes.qrc
    resources.qrc
    app_resource.rc
    animationpreviewwidget.h animationpreviewwidget.cpp
)

target_include_directories(CurveMaker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(CurveMaker
    PRIVATE
        Qt::Core
        Qt::Widgets
)

include(GNUInstallDirs)

install(TARGETS CurveMaker
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET CurveMaker
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
