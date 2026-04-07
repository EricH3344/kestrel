
target_sources(${CMAKE_PROJECT_NAME} PUBLIC
    "main.cpp"
    "windowcontroller.h"
    "windowcontroller.cpp"
    "../src/SystemAlert.h"
    "../src/SystemAlert.cpp"
    "../src/CreateProjectController.h"
    "../src/CreateProjectController.cpp"
    "../src/PathHelper.h"
    "../src/PathHelper.cpp"
    "../src/FileDialogHelper.h"
    "../src/FileDialogHelper.cpp"
    "../src/ProjectCreator.h"
    "../src/ProjectCreator.cpp"
    "../src/ProjectLoader.h"
    "../src/ProjectLoader.cpp"
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Quick
    Qt${QT_VERSION_MAJOR}::Qml)