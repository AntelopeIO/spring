configure_file(${CMAKE_CURRENT_SOURCE_DIR}/llvm-gcov.sh ${CMAKE_CURRENT_BINARY_DIR}/llvm-gcov.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/ctestwrapper.sh ${CMAKE_CURRENT_BINARY_DIR}/ctestwrapper.sh COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/validate_reflection.py ${CMAKE_CURRENT_BINARY_DIR}/validate_reflection.py COPYONLY)
configure_file(net-util.py net-util.py COPYONLY)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
   add_executable(fsgsbase-enabled fsgsbase-enabled.c)
endif()
