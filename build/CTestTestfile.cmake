# CMake generated Testfile for 
# Source directory: /Users/davidneskrabal/Projects/mini-redis
# Build directory: /Users/davidneskrabal/Projects/mini-redis/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("smoke_test" "/Users/davidneskrabal/Projects/mini-redis/build/test_smoke")
set_tests_properties("smoke_test" PROPERTIES  _BACKTRACE_TRIPLES "/Users/davidneskrabal/Projects/mini-redis/CMakeLists.txt;63;add_test;/Users/davidneskrabal/Projects/mini-redis/CMakeLists.txt;0;")
subdirs("_deps/unity-build")
