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

add_executable(frigate_save_editor_tests SaveEditorTest.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/menu/CheatAvailability.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveEditor.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                          ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp
                                          ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_include_directories(frigate_save_editor_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME SaveEditorTest COMMAND frigate_save_editor_tests)

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

add_executable(frigate_difficulty_tests DifficultyTest.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/DifficultyModel.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/MissionFolders.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/MissionSelectModel.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/Options007Model.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp
                                        ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_link_libraries(frigate_difficulty_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_difficulty_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME DifficultyTest COMMAND frigate_difficulty_tests)
add_executable(frigate_briefing_tests BriefingTest.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/BriefingModel.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/MissionFolders.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/MissionSelectModel.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/ObjectiveLines.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/StageName.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/StageStart.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/save/GameSettings.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                      ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp)
target_link_libraries(frigate_briefing_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_briefing_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME BriefingTest COMMAND frigate_briefing_tests)

add_executable(frigate_mission_end_tests MissionEndTest.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/MissionEndModel.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/MissionFolders.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/MissionSelectModel.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/ObjectiveLines.cpp
                                         ${CMAKE_SOURCE_DIR}/src/game/menu/StageName.cpp)
target_link_libraries(frigate_mission_end_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_mission_end_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MissionEndTest COMMAND frigate_mission_end_tests)

add_executable(frigate_cheat_menu_tests CheatMenuTest.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/CheatMenuModel.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/CheatTable.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/FileSelectModel.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/FolderScreenSpace.cpp
                                        ${CMAKE_SOURCE_DIR}/src/game/menu/MissionFolders.cpp)
target_link_libraries(frigate_cheat_menu_tests PRIVATE frigate_input_state frigate_level)
target_include_directories(frigate_cheat_menu_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME CheatMenuTest COMMAND frigate_cheat_menu_tests)

# The screens' real handlers need the platform layer they are built with.
if(TARGET frigate_game)
    add_executable(frigate_menu_flow_tests MenuFlowTest.cpp)
    target_link_libraries(frigate_menu_flow_tests PRIVATE frigate_game)

    add_test(NAME MenuFlowTest COMMAND frigate_menu_flow_tests)

    add_executable(frigate_menu_audio_tests MenuAudioTest.cpp)
    target_link_libraries(frigate_menu_audio_tests PRIVATE frigate_game)

    add_test(NAME MenuAudioTest COMMAND frigate_menu_audio_tests)
endif()


add_executable(frigate_multiplayer_settings_tests MultiplayerSettingsTest.cpp)
target_link_libraries(frigate_multiplayer_settings_tests PRIVATE frigate_level)
target_include_directories(frigate_multiplayer_settings_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MultiplayerSettingsTest COMMAND frigate_multiplayer_settings_tests)

add_executable(frigate_sound_tests SoundTest.cpp)
target_link_libraries(frigate_sound_tests PRIVATE frigate_level frigate_audio_driver)
target_include_directories(frigate_sound_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests/audio)

add_test(NAME SoundTest COMMAND frigate_sound_tests)

add_executable(frigate_multiplayer_quick_start_tests MultiplayerQuickStartTest.cpp)
target_link_libraries(frigate_multiplayer_quick_start_tests PRIVATE frigate_level)
target_include_directories(frigate_multiplayer_quick_start_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MultiplayerQuickStartTest COMMAND frigate_multiplayer_quick_start_tests)

if(TARGET frigate_platform)
    add_executable(frigate_multiplayer_harness MultiplayerHarnessTest.cpp)
    target_link_libraries(frigate_multiplayer_harness PRIVATE frigate_game frigate_platform)
    target_include_directories(frigate_multiplayer_harness PRIVATE ${CMAKE_SOURCE_DIR}/src)

    # Exit code 77 (no packed archives) is a skip, not a failure.
    add_test(NAME MultiplayerHarness COMMAND frigate_multiplayer_harness ${CMAKE_SOURCE_DIR}/archives)
    add_test(NAME MultiplayerHarnessStages COMMAND frigate_multiplayer_harness ${CMAKE_SOURCE_DIR}/archives stages)
    add_test(NAME MultiplayerHarnessWeapons COMMAND frigate_multiplayer_harness ${CMAKE_SOURCE_DIR}/archives weapons)
    set_tests_properties(MultiplayerHarness MultiplayerHarnessStages MultiplayerHarnessWeapons
                         PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(frigate_multiplayer_unlocks_tests MultiplayerUnlocksTest.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/FavouriteWeapons.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/MultiplayerUnlocks.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/save/SaveData.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/save/SaveManager.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/save/SaveProgress.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/game/save/SaveStorage.cpp
                                                 ${CMAKE_SOURCE_DIR}/src/model/Random.cpp)
target_link_libraries(frigate_multiplayer_unlocks_tests PRIVATE frigate_level)
target_include_directories(frigate_multiplayer_unlocks_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME MultiplayerUnlocksTest COMMAND frigate_multiplayer_unlocks_tests)
