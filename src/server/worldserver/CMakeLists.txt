# This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
#
# This file is free software; as a special exception the author gives
# unlimited permission to copy and/or distribute it, with or without
# modifications, as long as this notice is preserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

CollectSourceFiles(
  ${CMAKE_CURRENT_SOURCE_DIR}
  PRIVATE_SOURCES
  # Exclude
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders)

list(APPEND PRIVATE_SOURCES ${sources_windows})

if(WIN32)
  if(MSVC)
    list(APPEND PRIVATE_SOURCES worldserver.rc)
  endif()
endif()

if(USE_COREPCH)
  set(PRIVATE_PCH_HEADER PrecompiledHeaders/worldPCH.h)
endif()

GroupSources(${CMAKE_CURRENT_SOURCE_DIR})

add_executable(worldserver
  ${PRIVATE_SOURCES}
)

if(NOT WIN32)
  target_compile_definitions(worldserver PRIVATE
    _TRINITY_CORE_CONFIG="${CONF_DIR}/worldserver.conf"
    _TRINITY_CORE_CONFIG_DIR="${CONF_DIR}/worldserver.conf.d"
  )
endif()

target_link_libraries(worldserver
  PRIVATE
    trinity-core-interface
  PUBLIC
    scripts
    game
    plugins
    gsoap
    readline)

CollectIncludeDirectories(
  ${CMAKE_CURRENT_SOURCE_DIR}
  PUBLIC_INCLUDES
  # Exclude
  ${CMAKE_CURRENT_SOURCE_DIR}/PrecompiledHeaders)

target_include_directories(worldserver
  PUBLIC
    ${PUBLIC_INCLUDES}
  PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR})

set_target_properties(worldserver
    PROPERTIES
      FOLDER
        "server")

# Add all dynamic projects as dependency to the worldserver
if(WORLDSERVER_DYNAMIC_SCRIPT_MODULES_DEPENDENCIES)
  add_dependencies(worldserver ${WORLDSERVER_DYNAMIC_SCRIPT_MODULES_DEPENDENCIES})
endif()

if(UNIX)
  install(TARGETS worldserver DESTINATION bin)
  if(COPY_CONF)
    add_custom_command(TARGET worldserver
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/worldserver.conf.dist ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/../etc/worldserver.conf.dist
    )
    install(FILES worldserver.conf.dist DESTINATION ${CONF_DIR})
  endif()
elseif(WIN32)
  install(TARGETS worldserver DESTINATION "${CMAKE_INSTALL_PREFIX}")
  if(COPY_CONF)
    add_custom_command(TARGET worldserver
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/worldserver.conf.dist ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
    )
    install(FILES worldserver.conf.dist DESTINATION "${CMAKE_INSTALL_PREFIX}")
  endif()
endif()

# Generate precompiled header
if(USE_COREPCH)
  add_cxx_pch(worldserver ${PRIVATE_PCH_HEADER})
endif()
