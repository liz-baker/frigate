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

add_executable(frigate_joint_draw_list_tests JointDrawListTest.cpp ${CMAKE_SOURCE_DIR}/src/model/JointDrawList.cpp
                                             ${CMAKE_SOURCE_DIR}/src/model/ModelWalk.cpp
                                             ${CMAKE_SOURCE_DIR}/src/model/Animation.cpp
                                             ${CMAKE_SOURCE_DIR}/src/model/Quaternion.cpp)
target_include_directories(frigate_joint_draw_list_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME JointDrawListTest COMMAND frigate_joint_draw_list_tests)

add_executable(frigate_effect_quads_tests EffectQuadsTest.cpp ${CMAKE_SOURCE_DIR}/src/model/EffectQuads.cpp
                                          ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_include_directories(frigate_effect_quads_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME EffectQuadsTest COMMAND frigate_effect_quads_tests)

add_executable(frigate_random_tests RandomTest.cpp ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_include_directories(frigate_random_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME RandomTest COMMAND frigate_random_tests)
