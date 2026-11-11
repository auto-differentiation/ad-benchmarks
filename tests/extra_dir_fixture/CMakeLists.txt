# Stand-in for an external ADB_EXTRA_DIR (tests/extra_fixture/check_extra_dir.cmake
# configures a separate build of the suite with -DADB_EXTRA_DIR=<this dir>).
# It adds the dummy column of tests/extra_fixture to the real bench_heston
# target of that build, the way an external dir adds its columns. Never used
# by the default build.
target_sources(bench_heston PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../extra_fixture/dummy_column.cpp)
