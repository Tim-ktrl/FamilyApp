cmake_minimum_required(VERSION 3.21)

project(PocketHello VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Quick
    QuickControls2
    Network
)

qt_standard_project_setup(REQUIRES 6.5)

if(NOT DEFINED FIREBASE_CPP_SDK_DIR)
    if(DEFINED ENV{FIREBASE_CPP_SDK_DIR})
        set(FIREBASE_CPP_SDK_DIR
            "$ENV{FIREBASE_CPP_SDK_DIR}")
    else()
        message(FATAL_ERROR
            "FIREBASE_CPP_SDK_DIR is not set")
    endif()
endif()

if(MSVC)
    set(MSVC_RUNTIME_MODE "MD")
endif()

add_subdirectory(
    "${FIREBASE_CPP_SDK_DIR}"
    firebase_cpp_sdk
    EXCLUDE_FROM_ALL
)

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

    SOURCES
        storagemanager.h
        storagemanager.cpp

        shoppinglistmodel.h
        shoppinglistmodel.cpp

        saveditemsmodel.h
        saveditemsmodel.cpp

        firebasemanager.h
        firebasemanager.cpp
        itemsynccoordinator.h
        itemsynccoordinator.cpp
        qtcallbackgate.h
        familydatavalidation.h
        familydatavalidation.cpp
        initialsynccoordinator.h
        initialsynccoordinator.cpp
)

set_target_properties(appPocketHello PROPERTIES
    WIN32_EXECUTABLE TRUE
)

if(ANDROID)
    qt_policy(SET QTP0002 NEW)
    if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
        message(FATAL_ERROR "FamilyApp Android packaging currently supports arm64-v8a only")
    endif()

    set(FAMILYAPP_GOOGLE_SERVICES_JSON "${CMAKE_CURRENT_SOURCE_DIR}/google-services.json"
        CACHE FILEPATH "Local Firebase Android configuration (never committed)")
    set(FAMILYAPP_ANDROID_OPENSSL_DIR "${ANDROID_SDK_ROOT}/android_openssl/ssl_3/arm64-v8a"
        CACHE PATH "Directory containing Android ARM64 libcrypto_3.so and libssl_3.so")
    foreach(required_file IN ITEMS "${FAMILYAPP_GOOGLE_SERVICES_JSON}"
            "${FAMILYAPP_ANDROID_OPENSSL_DIR}/libcrypto_3.so"
            "${FAMILYAPP_ANDROID_OPENSSL_DIR}/libssl_3.so")
        if(NOT EXISTS "${required_file}")
            message(FATAL_ERROR "Required Android packaging input missing: ${required_file}")
        endif()
    endforeach()

    # Stage local configuration with the templates, never in the source package.
    set(android_package_dir "${CMAKE_CURRENT_BINARY_DIR}/android-package")
    foreach(package_file IN ITEMS build.gradle AndroidManifest.xml res/drawable/ic_familyapp.xml)
        configure_file("android/${package_file}" "${android_package_dir}/${package_file}" COPYONLY)
    endforeach()
    configure_file("${FAMILYAPP_GOOGLE_SERVICES_JSON}"
        "${android_package_dir}/google-services.json" COPYONLY)

    set_target_properties(appPocketHello PROPERTIES
        QT_ANDROID_PACKAGE_NAME "com.timktrl.familyapp"
        QT_ANDROID_APP_NAME "FamilyApp"
        QT_ANDROID_APP_ICON "@drawable/ic_familyapp"
        QT_ANDROID_VERSION_NAME "0.1.1"
        QT_ANDROID_VERSION_CODE 2
        QT_ANDROID_MIN_SDK_VERSION 28
        QT_ANDROID_TARGET_SDK_VERSION 36
        QT_ANDROID_COMPILE_SDK_VERSION 36
        QT_ANDROID_SDK_BUILD_TOOLS_REVISION "36.0.0"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${android_package_dir}"
        QT_ANDROID_EXTRA_LIBS
            "${FAMILYAPP_ANDROID_OPENSSL_DIR}/libcrypto_3.so;${FAMILYAPP_ANDROID_OPENSSL_DIR}/libssl_3.so"
    )
endif()

target_link_libraries(appPocketHello PRIVATE
    Qt6::Core
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network

    firebase_database
    firebase_app
)

if(WIN32)
    target_link_libraries(appPocketHello PRIVATE
        advapi32
        ws2_32
        crypt32
        iphlpapi
        psapi
        userenv
    )
endif()
