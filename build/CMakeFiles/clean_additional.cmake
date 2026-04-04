# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles\\Spektrometr_autogen.dir\\AutogenUsed.txt"
  "CMakeFiles\\Spektrometr_autogen.dir\\ParseCache.txt"
  "Spektrometr_autogen"
  )
endif()
