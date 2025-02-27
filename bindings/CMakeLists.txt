# Copyright (C) 2020 Istituto Italiano di Tecnologia (IIT). All rights reserved.
# This software may be modified and distributed under the terms of the
# BSD-3-Clause license.

if(FRAMEWORK_COMPILE_PYTHON_BINDINGS)

    set(NEW_LINE "\n")

    option(FRAMEWORK_DETECT_ACTIVE_PYTHON_SITEPACKAGES
        "Do you want BLF to detect and use the active site-package directory? (it could be a system dir)"
        FALSE)

    # Install the resulting Python package for the active interpreter
    if(FRAMEWORK_DETECT_ACTIVE_PYTHON_SITEPACKAGES)
        set(PYTHON_INSTDIR ${Python3_SITELIB}/bipedal_locomotion_framework)
    else()
     execute_process(COMMAND ${Python3_EXECUTABLE} -c "from distutils import sysconfig; print(sysconfig.get_python_lib(1,0,prefix=''))"
                     OUTPUT_VARIABLE _PYTHON_INSTDIR)
     string(STRIP ${_PYTHON_INSTDIR} _PYTHON_INSTDIR_CLEAN)
     set(PYTHON_INSTDIR ${_PYTHON_INSTDIR_CLEAN}/bipedal_locomotion_framework)
    endif()

    # Folder of the Python package within the build tree.
    # It is used for the Python tests.
    set(BLF_PYTHON_PACKAGE "${CMAKE_BINARY_DIR}/bipedal_locomotion_framework")

    # Add the bindings directory
    add_subdirectory(python)

    # Create the __init__.py file
    file(GENERATE
         OUTPUT "${BLF_PYTHON_PACKAGE}/__init__.py"
         CONTENT "from .bindings import *${NEW_LINE}from . import utils${NEW_LINE}")

    # Install the __init__.py file
    install(FILES "${BLF_PYTHON_PACKAGE}/__init__.py"
            DESTINATION ${PYTHON_INSTDIR})

    # Install pip metadata files to ensure that blf installed via CMake is listed by pip list
    # See https://packaging.python.org/specifications/recording-installed-packages/
    # and https://packaging.python.org/en/latest/specifications/core-metadata/#core-metadata
    option(BLF_PYTHON_PIP_METADATA_INSTALL "Use CMake to install Python pip metadata. Set to off if some other tool already installs it." ON)
    mark_as_advanced(BLF_PYTHON_PIP_METADATA_INSTALL)
    set(BLF_PYTHON_PIP_METADATA_INSTALLER "cmake" CACHE STRING "Specify the string to identify the pip Installer. Default: cmake, change this if you are using another tool.")
    mark_as_advanced(BLF_PYTHON_PIP_METADATA_INSTALLER)
    if(BLF_PYTHON_PIP_METADATA_INSTALL)
      get_filename_component(PYTHON_METADATA_PARENT_DIR ${PYTHON_INSTDIR} DIRECTORY)
      file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/METADATA "")
      file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/METADATA "Metadata-Version: 2.1${NEW_LINE}")
      file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/METADATA "Name: bipedal_locomotion_framework${NEW_LINE}")
      file(APPEND ${CMAKE_CURRENT_BINARY_DIR}/METADATA "Version: ${BipedalLocomotionFramework_VERSION}${NEW_LINE}")
      file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/INSTALLER "${BLF_PYTHON_PIP_METADATA_INSTALLER}${NEW_LINE}")
      install(
        FILES "${CMAKE_CURRENT_BINARY_DIR}/METADATA" "${CMAKE_CURRENT_BINARY_DIR}/INSTALLER"
        DESTINATION ${PYTHON_METADATA_PARENT_DIR}/bipedal_locomotion_framework-${BipedalLocomotionFramework_VERSION}.dist-info)
    endif()

endif()
