add_executable(frigate_menu_state_machine_tests MenuStateMachineTest.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/MenuStateMachine.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/FrameTiming.cpp)
target_include_directories(frigate_menu_state_machine_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MenuStateMachineTest COMMAND frigate_menu_state_machine_tests)

add_executable(frigate_blood_animation_tests BloodAnimationTest.cpp
                                             ${CMAKE_SOURCE_DIR}/src/game/menu/BloodAnimation.cpp)
target_include_directories(frigate_blood_animation_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME BloodAnimationTest COMMAND frigate_blood_animation_tests)

add_executable(frigate_cast_data_tests CastDataTest.cpp ${CMAKE_SOURCE_DIR}/src/game/menu/CastData.cpp)
target_include_directories(frigate_cast_data_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME CastDataTest COMMAND frigate_cast_data_tests)

add_executable(frigate_file_select_tests FileSelectTest.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/MissionFolders.cpp)
target_link_libraries(frigate_file_select_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_file_select_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME FileSelectTest COMMAND frigate_file_select_tests)

add_executable(frigate_mode_select_tests ModeSelectTest.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/CheatAvailability.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/ModeSelectModel.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp
                                         ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_link_libraries(frigate_mode_select_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_mode_select_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ModeSelectTest COMMAND frigate_mode_select_tests)

add_executable(frigate_save_data_tests SaveDataTest.cpp
                                       ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                       ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_include_directories(frigate_save_data_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME SaveDataTest COMMAND frigate_save_data_tests)

add_executable(frigate_save_manager_tests SaveManagerTest.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp
                                          ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_include_directories(frigate_save_manager_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME SaveManagerTest COMMAND frigate_save_manager_tests)

add_executable(frigate_save_progress_tests SaveProgressTest.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp
                                           ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_include_directories(frigate_save_progress_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME SaveProgressTest COMMAND frigate_save_progress_tests)

add_executable(frigate_game_settings_tests GameSettingsTest.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/GameSettings.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                           ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp)
target_link_libraries(frigate_game_settings_tests PRIVATE frigate_level)
target_include_directories(frigate_game_settings_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME GameSettingsTest COMMAND frigate_game_settings_tests)

add_executable(frigate_mission_select_tests MissionSelectTest.cpp
                                            ${CMAKE_SOURCE_DIR}/src/game/menu/MissionSelectModel.cpp
                                            ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                            ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                            ${CMAKE_SOURCE_DIR}/src/game/menu/MissionFolders.cpp)
target_link_libraries(frigate_mission_select_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_mission_select_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MissionSelectTest COMMAND frigate_mission_select_tests)
