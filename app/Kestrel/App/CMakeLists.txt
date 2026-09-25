target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    main.cpp
    windowcontroller.h
    windowcontroller.cpp
    ../src/application/SystemAlert.h
    ../src/application/SystemAlert.cpp
    ../src/application/CreateProjectController.h
    ../src/application/CreateProjectController.cpp
    ../src/application/PathHelper.h
    ../src/application/PathHelper.cpp
    ../src/application/FileDialogHelper.h
    ../src/application/FileDialogHelper.cpp
    ../src/project/ProjectCreator.h
    ../src/project/ProjectCreator.cpp
    ../src/project/ProjectLoader.h
    ../src/project/ProjectLoader.cpp
    ../src/processing/StitchingController.h
    ../src/processing/StitchingController.cpp
    ../src/processing/MapDetailRenderer.h
    ../src/processing/MapDetailRenderer.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml)
