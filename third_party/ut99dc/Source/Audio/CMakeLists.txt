project(Audio CXX)

set(SRC_FILES
  "Src/Audio.cpp"
  "Src/AudioCoreLinux.cpp"
  "Src/AudioLibrary.cpp"
  "Src/AudioMixer.cpp"
  "Src/AudioSubsystem.cpp"
  "Src/FormatWAV.cpp"
)
add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

target_link_libraries(${PROJECT_NAME} Engine Render Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE AUDIO_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

