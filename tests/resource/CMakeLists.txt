add_executable(frigate_resource_type_tests ResourceTypeTest.cpp)
target_link_libraries(frigate_resource_type_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_resource_type_tests PRIVATE ${CMAKE_SOURCE_DIR}/tests/audio)

add_test(NAME ResourceTypeTest COMMAND frigate_resource_type_tests)

add_executable(frigate_text_render_tests TextRenderTest.cpp ${CMAKE_SOURCE_DIR}/src/game/TextRender.cpp)
target_link_libraries(frigate_text_render_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_text_render_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME TextRenderTest COMMAND frigate_text_render_tests)

add_executable(frigate_render_state_tests RenderStateTest.cpp)
target_link_libraries(frigate_render_state_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_render_state_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME RenderStateTest COMMAND frigate_render_state_tests)

add_executable(frigate_model_instance_tests ModelInstanceTest.cpp)
target_link_libraries(frigate_model_instance_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_model_instance_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ModelInstanceTest COMMAND frigate_model_instance_tests)

add_executable(frigate_chr_records_tests ChrRecordsTest.cpp ${CMAKE_SOURCE_DIR}/src/game/ChrRecords.cpp)
target_link_libraries(frigate_chr_records_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_chr_records_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ChrRecordsTest COMMAND frigate_chr_records_tests)

add_executable(frigate_gun_draw_tests GunDrawTest.cpp ${CMAKE_SOURCE_DIR}/src/level/GunDraw.cpp
                                      ${CMAKE_SOURCE_DIR}/src/gfx/Gu.cpp)
target_link_libraries(frigate_gun_draw_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_gun_draw_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME GunDrawTest COMMAND frigate_gun_draw_tests)

add_executable(frigate_hud_tests HudTest.cpp ${CMAKE_SOURCE_DIR}/src/level/Hud.cpp
                                 ${CMAKE_SOURCE_DIR}/src/game/TextRender.cpp ${CMAKE_SOURCE_DIR}/src/gfx/Gu.cpp)
target_link_libraries(frigate_hud_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_hud_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME HudTest COMMAND frigate_hud_tests)

add_executable(frigate_watch_tests WatchTest.cpp ${CMAKE_SOURCE_DIR}/src/level/Watch.cpp
                                   ${CMAKE_SOURCE_DIR}/src/level/WatchMenu.cpp ${CMAKE_SOURCE_DIR}/src/level/WatchMenuControl.cpp
                                   ${CMAKE_SOURCE_DIR}/src/level/WatchMenuDraw.cpp ${CMAKE_SOURCE_DIR}/src/level/WatchMenuItem.cpp
                                   ${CMAKE_SOURCE_DIR}/src/level/Hud.cpp ${CMAKE_SOURCE_DIR}/src/level/GunDraw.cpp
                                   ${CMAKE_SOURCE_DIR}/src/game/TextRender.cpp ${CMAKE_SOURCE_DIR}/src/gfx/Gu.cpp
                                   ${CMAKE_SOURCE_DIR}/src/model/Random.cpp ${CMAKE_SOURCE_DIR}/src/game/Sound.cpp)
target_link_libraries(frigate_watch_tests PRIVATE frigate_resource_types frigate_audio_driver)
target_include_directories(frigate_watch_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME WatchTest COMMAND frigate_watch_tests)

add_executable(frigate_fog_tests FogTest.cpp ${CMAKE_SOURCE_DIR}/src/level/Fog.cpp
                                 ${CMAKE_SOURCE_DIR}/src/level/EnvironmentTable.cpp)
target_link_libraries(frigate_fog_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_fog_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME FogTest COMMAND frigate_fog_tests)

add_executable(frigate_bg_apply_tests BgApplyTest.cpp ${CMAKE_SOURCE_DIR}/src/level/BgApply.cpp)
target_link_libraries(frigate_bg_apply_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_bg_apply_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME BgApplyTest COMMAND frigate_bg_apply_tests)

add_executable(frigate_light_fixture_tests LightFixturesTest.cpp ${CMAKE_SOURCE_DIR}/src/level/LightFixtures.cpp)
target_link_libraries(frigate_light_fixture_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_light_fixture_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME LightFixturesTest COMMAND frigate_light_fixture_tests)

add_executable(frigate_shot_hit_tests ShotHitTest.cpp ${CMAKE_SOURCE_DIR}/src/level/ModelHit.cpp
                                      ${CMAKE_SOURCE_DIR}/src/level/TextureHits.cpp
                                      ${CMAKE_SOURCE_DIR}/src/level/LightFixtures.cpp)
target_link_libraries(frigate_shot_hit_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_shot_hit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME ShotHitTest COMMAND frigate_shot_hit_tests)

add_executable(frigate_sky_tests SkyTest.cpp ${CMAKE_SOURCE_DIR}/src/level/Sky.cpp ${CMAKE_SOURCE_DIR}/src/level/Fog.cpp
                                 ${CMAKE_SOURCE_DIR}/src/level/EnvironmentTable.cpp ${CMAKE_SOURCE_DIR}/src/gfx/Gu.cpp)
target_link_libraries(frigate_sky_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_sky_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME SkyTest COMMAND frigate_sky_tests)

add_executable(frigate_prop_fog_tests PropFogTest.cpp ${CMAKE_SOURCE_DIR}/src/level/PropFog.cpp
                                      ${CMAKE_SOURCE_DIR}/src/level/Fog.cpp
                                      ${CMAKE_SOURCE_DIR}/src/level/EnvironmentTable.cpp)
target_link_libraries(frigate_prop_fog_tests PRIVATE frigate_resource_types)
target_include_directories(frigate_prop_fog_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)

add_test(NAME PropFogTest COMMAND frigate_prop_fog_tests)
