# Copyright: (C) 2014 RobotCub Consortium
# Author: ---
# CopyPolicy: Released under the terms of the GNU GPL v2.0.

# cmake_minimum_required(VERSION 2.6)
cmake_minimum_required(VERSION 3.12)

project(AffordancesProject VERSION 1.1)

find_package(YARP 3.3.0 REQUIRED)
find_package(ICUB REQUIRED)
find_package(ICUBcontrib REQUIRED)
list(APPEND CMAKE_MODULE_PATH ${YARP_MODULE_PATH})
list(APPEND CMAKE_MODULE_PATH ${ICUB_MODULE_PATH})
list(APPEND CMAKE_MODULE_PATH ${ICUBCONTRIB_MODULE_PATH})

include(YarpInstallationHelpers)
include(ICUBcontribHelpers)
include(ICUBcontribOptions)
icubcontrib_set_default_prefix()

add_definitions(${YARP_DEFINES})   # add yarp definitions - this contains also -D_REENTRANT

## add modules
add_subdirectory(modules)

## then apps
add_subdirectory(app)

## Add this instruction IF AND ONLY IF the project uses libraries.
# icubcontrib_finalize_export (${PROJECT_NAME})  

## Uninstall
icubcontrib_add_uninstall_target()

