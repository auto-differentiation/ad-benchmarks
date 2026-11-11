# Not a test: links one program against every enabled AD library.
add_executable(smoke_deps smoke_deps.cpp)
target_link_libraries(smoke_deps PRIVATE adb::versions)
adb_link_ad_libraries(smoke_deps PRIVATE ${ADB_AD_LIBRARIES})
