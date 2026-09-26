cmake_minimum_required(VERSION 3.21)

project(PocketHello VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(appPocketHello
    main.cpp
)

qt_add_qml_module(appPocketHello
    URI PocketHello
    VERSION 1.0
    QML_FILES
        Main.qml
        ShoppingListPage.qml
        SavedItemsPage.qml
)

set_target_properties(appPocketHello PROPERTIES
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(appPocketHello PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
)