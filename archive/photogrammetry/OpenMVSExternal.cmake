include_guard(GLOBAL)

function(kestrel_add_openmvs target)
    if (NOT KESTREL_BUILD_OPENMVS)
        return()
    endif()
    if (CMAKE_VERSION VERSION_LESS 3.24)
        message(FATAL_ERROR
            "KESTREL_BUILD_OPENMVS requires CMake 3.24 or newer (OpenMVS 2.4 requirement).")
    endif()
    if (NOT TARGET "${target}")
        message(FATAL_ERROR "kestrel_add_openmvs requires an existing target")
    endif()

    include(ExternalProject)
    find_package(Git REQUIRED)

    set(_openmvs_root "${CMAKE_BINARY_DIR}/_deps/openmvs")
    set(_vcpkg_source "${CMAKE_BINARY_DIR}/_deps/vcpkg-src")
    set(_openmvs_source "${_openmvs_root}/src")
    set(_openmvs_install "${_openmvs_root}/install")

    if (TARGET kestrel_openmvs)
        add_dependencies("${target}" kestrel_openmvs)
        file(TO_CMAKE_PATH "${_openmvs_install}/bin/OpenMVS"
            _openmvs_runtime_dir)
        target_compile_definitions("${target}" PRIVATE
            KESTREL_OPENMVS_DEFAULT_BIN_DIR="${_openmvs_runtime_dir}")
        return()
    endif()

    set(_openmvs_generator_arguments)
    set(_openmvs_runtime_arguments)
    if (KESTREL_OPENMVS_VCPKG_TRIPLET)
        set(_openmvs_triplet "${KESTREL_OPENMVS_VCPKG_TRIPLET}")
    elseif (WIN32)
        # OpenMVS is invoked out-of-process, so its compiler ABI does not need
        # to match Kestrel's.  Building the helper tools natively with MSVC
        # avoids vcpkg ports whose autotools DESTDIR handling is unreliable
        # for MinGW builds rooted at an absolute Windows path (notably GMP).
        # The static triplet also avoids requiring users to install the MSVC
        # runtime alongside a MinGW Kestrel build.
        set(_openmvs_triplet "x64-windows-static")
    elseif (APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(_openmvs_triplet "arm64-osx")
    elseif (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|AARCH64)$")
        set(_openmvs_triplet "arm64-linux")
    else()
        set(_openmvs_triplet "x64-linux")
    endif()
    # A CMake binary directory cannot switch generators in place. Keeping the
    # triplet in the path lets a MinGW override, the native Windows default,
    # and other platform builds coexist without corrupting each other's cache.
    set(_openmvs_binary "${_openmvs_root}/build-${_openmvs_triplet}")

    if (WIN32 AND NOT _openmvs_triplet MATCHES "mingw")
        set(_vswhere
            "$ENV{ProgramFiles}/../Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe")
        if (NOT EXISTS "${_vswhere}")
            message(FATAL_ERROR
                "Building OpenMVS on Windows requires Visual Studio 2022 with the Desktop development with C++ workload")
        endif()
        execute_process(
            COMMAND "${_vswhere}" -latest -products *
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64
                -property installationPath
            OUTPUT_VARIABLE _openmvs_vs_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            COMMAND_ERROR_IS_FATAL ANY)
        file(GLOB _openmvs_msvc_toolset_paths LIST_DIRECTORIES TRUE
            "${_openmvs_vs_path}/VC/Tools/MSVC/*")
        if (NOT _openmvs_msvc_toolset_paths)
            message(FATAL_ERROR
                "Visual Studio 2022 was found, but no MSVC C++ toolset is installed")
        endif()
        list(SORT _openmvs_msvc_toolset_paths
            COMPARE NATURAL ORDER DESCENDING)
        list(GET _openmvs_msvc_toolset_paths 0 _openmvs_msvc_toolset_path)
        get_filename_component(_openmvs_msvc_toolset
            "${_openmvs_msvc_toolset_path}" NAME)
        set(_openmvs_generator_arguments
            CMAKE_GENERATOR "Visual Studio 17 2022"
            CMAKE_GENERATOR_PLATFORM x64)
        # The x64-windows-static vcpkg libraries are compiled with /MT. CMake
        # otherwise gives OpenMVS's Visual Studio targets the /MD default,
        # producing hundreds of LNK2038 RuntimeLibrary mismatches at the final
        # executable links. Only Release is built by this external project.
        set(_openmvs_runtime_arguments
            "-DCMAKE_POLICY_DEFAULT_CMP0091=NEW"
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded"
            # Some Visual Studio installations retain an older v143 default
            # even though vcpkg selects the newest installed compiler. Set the
            # project-global compiler directory explicitly so the OpenMVS and
            # dependency objects use the same STL implementation.
            "-DCMAKE_VS_GLOBALS=VCToolsVersion=${_openmvs_msvc_toolset}")
    endif()

    if (WIN32)
        set(_vcpkg_bootstrap
            cmd /c "<SOURCE_DIR>/bootstrap-vcpkg.bat" -disableMetrics)
    else()
        set(_vcpkg_bootstrap
            sh "<SOURCE_DIR>/bootstrap-vcpkg.sh" -disableMetrics)
    endif()

    # This is the vcpkg revision used by OpenMVS 2.4's own CI. Keeping both
    # revisions fixed makes first-time dependency resolution reproducible.
    ExternalProject_Add(kestrel_vcpkg
        PREFIX "${CMAKE_BINARY_DIR}/_deps/vcpkg"
        SOURCE_DIR "${_vcpkg_source}"
        GIT_REPOSITORY https://github.com/microsoft/vcpkg.git
        GIT_TAG ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0
        GIT_SHALLOW FALSE
        UPDATE_DISCONNECTED TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ${_vcpkg_bootstrap}
        INSTALL_COMMAND "")

    set(_openmvs_cmake_arguments
        "-DCMAKE_TOOLCHAIN_FILE=${_vcpkg_source}/scripts/buildsystems/vcpkg.cmake"
        "-DVCPKG_TARGET_TRIPLET=${_openmvs_triplet}"
        ${_openmvs_runtime_arguments}
        "-DCMAKE_INSTALL_PREFIX=${_openmvs_install}"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DBUILD_SHARED_LIBS=OFF"
        "-DOpenMVS_BUILD_TOOLS=ON"
        "-DOpenMVS_BUILD_VIEWER=OFF"
        "-DOpenMVS_USE_CUDA=${KESTREL_OPENMVS_ENABLE_CUDA}"
        "-DOpenMVS_USE_CERES=OFF"
        "-DOpenMVS_USE_PYTHON=OFF"
        "-DOpenMVS_USE_SIFTGPU=OFF"
        "-DOpenMVS_USE_BREAKPAD=OFF"
        "-DOpenMVS_ENABLE_TESTS=OFF"
        "-DOpenMVS_ENABLE_IPO=OFF")

    ExternalProject_Add(kestrel_openmvs
        PREFIX "${_openmvs_root}/external"
        SOURCE_DIR "${_openmvs_source}"
        BINARY_DIR "${_openmvs_binary}"
        INSTALL_DIR "${_openmvs_install}"
        GIT_REPOSITORY https://github.com/cdcseacave/openMVS.git
        GIT_TAG v2.4.0
        GIT_SHALLOW TRUE
        GIT_SUBMODULES_RECURSE TRUE
        UPDATE_DISCONNECTED TRUE
        DEPENDS kestrel_vcpkg
        ${_openmvs_generator_arguments}
        CMAKE_ARGS ${_openmvs_cmake_arguments}
        BUILD_COMMAND
            "${CMAKE_COMMAND}" --build <BINARY_DIR> --config Release --parallel
        INSTALL_COMMAND
            "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release)

    add_dependencies("${target}" kestrel_openmvs)
    file(TO_CMAKE_PATH "${_openmvs_install}/bin/OpenMVS"
        _openmvs_runtime_dir)
    target_compile_definitions("${target}" PRIVATE
        KESTREL_OPENMVS_DEFAULT_BIN_DIR="${_openmvs_runtime_dir}")

    include(GNUInstallDirs)
    install(DIRECTORY "${_openmvs_runtime_dir}/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/openmvs"
        COMPONENT KestrelRuntime OPTIONAL USE_SOURCE_PERMISSIONS
        FILES_MATCHING
            PATTERN "InterfaceCOLMAP*"
            PATTERN "DensifyPointCloud*"
            PATTERN "*.dll")
    set(_kestrel_openmvs_license_dir
        "${CMAKE_INSTALL_DATADIR}/licenses/Kestrel/OpenMVS")
    install(FILES
        "${_openmvs_source}/LICENSE"
        "${_openmvs_source}/COPYRIGHT.md"
        DESTINATION "${_kestrel_openmvs_license_dir}"
        COMPONENT KestrelRuntime OPTIONAL)
    # vcpkg places every dependency's redistributable notice in a file named
    # share/<port>/copyright. Retain that directory structure in the package.
    install(DIRECTORY
        "${_openmvs_binary}/vcpkg_installed/${_openmvs_triplet}/share/"
        DESTINATION "${_kestrel_openmvs_license_dir}/dependencies"
        COMPONENT KestrelRuntime OPTIONAL
        FILES_MATCHING PATTERN "copyright")

    set(KESTREL_OPENMVS_RUNTIME_DIR "${_openmvs_runtime_dir}"
        PARENT_SCOPE)
    message(STATUS
        "OpenMVS 2.4 will be built from source using vcpkg triplet ${_openmvs_triplet}")
    if (_openmvs_msvc_toolset)
        message(STATUS
            "OpenMVS and its vcpkg dependencies will use MSVC ${_openmvs_msvc_toolset}")
    endif()
endfunction()
