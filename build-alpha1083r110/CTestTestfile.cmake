# CMake generated Testfile for 
# Source directory: C:/Projects/myanglish
# Build directory: C:/Projects/myanglish/build-alpha1083r110
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test("MyanglishConverterTests" "C:/Projects/myanglish/build-alpha1083r110/Debug/MyanglishTests.exe")
  set_tests_properties("MyanglishConverterTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Projects/myanglish/CMakeLists.txt;190;add_test;C:/Projects/myanglish/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test("MyanglishConverterTests" "C:/Projects/myanglish/build-alpha1083r110/Release/MyanglishTests.exe")
  set_tests_properties("MyanglishConverterTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Projects/myanglish/CMakeLists.txt;190;add_test;C:/Projects/myanglish/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test("MyanglishConverterTests" "C:/Projects/myanglish/build-alpha1083r110/MinSizeRel/MyanglishTests.exe")
  set_tests_properties("MyanglishConverterTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Projects/myanglish/CMakeLists.txt;190;add_test;C:/Projects/myanglish/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test("MyanglishConverterTests" "C:/Projects/myanglish/build-alpha1083r110/RelWithDebInfo/MyanglishTests.exe")
  set_tests_properties("MyanglishConverterTests" PROPERTIES  _BACKTRACE_TRIPLES "C:/Projects/myanglish/CMakeLists.txt;190;add_test;C:/Projects/myanglish/CMakeLists.txt;0;")
else()
  add_test("MyanglishConverterTests" NOT_AVAILABLE)
endif()
