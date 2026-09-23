add_executable(frigate_model_walk_tests ModelWalkTest.cpp ${CMAKE_SOURCE_DIR}/src/model/ModelWalk.cpp
                                        ${CMAKE_SOURCE_DIR}/src/model/Animation.cpp
                                        ${CMAKE_SOURCE_DIR}/src/model/Quaternion.cpp)
target_include_directories(frigate_model_walk_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ModelWalkTest COMMAND frigate_model_walk_tests)

add_executable(frigate_animation_tests AnimationTest.cpp ${CMAKE_SOURCE_DIR}/src/model/Animation.cpp)
target_include_directories(frigate_animation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME AnimationTest COMMAND frigate_animation_tests)

add_executable(frigate_quaternion_tests QuaternionTest.cpp ${CMAKE_SOURCE_DIR}/src/model/Quaternion.cpp
                                        ${CMAKE_SOURCE_DIR}/src/model/Animation.cpp)
target_include_directories(frigate_quaternion_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME QuaternionTest COMMAND frigate_quaternion_tests)

add_executable(frigate_playback_tests PlaybackTest.cpp ${CMAKE_SOURCE_DIR}/src/model/Playback.cpp
                                      ${CMAKE_SOURCE_DIR}/src/model/RootMotion.cpp
                                      ${CMAKE_SOURCE_DIR}/src/model/Animation.cpp)
target_include_directories(frigate_playback_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME PlaybackTest COMMAND frigate_playback_tests)
