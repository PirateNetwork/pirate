mingw64_CC=x86_64-w64-mingw32-gcc-posix
mingw64_CXX=x86_64-w64-mingw32-g++-posix
mingw64_CFLAGS=-pipe -std=c17
mingw64_CXXFLAGS=-pipe -std=c++17

BASE64_TOOL = base64 --decode # Define BASE64_TOOL for MinGW

mingw64_release_CFLAGS=-g -O2
mingw64_release_CXXFLAGS=$(mingw64_CXXFLAGS) $(mingw64_release_CFLAGS)

mingw64_debug_CFLAGS=-O1
mingw64_debug_CXXFLAGS=$(mingw64_debug_CFLAGS)

mingw64_debug_CFLAGS=-g -O0
mingw64_debug_CPPFLAGS=-D_GLIBCXX_DEBUG -D_GLIBCXX_DEBUG_PEDANTIC
mingw64_debug_CXXFLAGS=$(mingw64_CXXFLAGS) $(mingw64_debug_CFLAGS) 