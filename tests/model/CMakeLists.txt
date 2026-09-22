add_executable(frigate_model_walk_tests ModelWalkTest.cpp ${CMAKE_SOURCE_DIR}/src/model/ModelWalk.cpp)
target_include_directories(frigate_model_walk_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ModelWalkTest COMMAND frigate_model_walk_tests)
