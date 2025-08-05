# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-src")
  file(MAKE_DIRECTORY "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-src")
endif()
file(MAKE_DIRECTORY
  "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-build"
  "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix"
  "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix/tmp"
  "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix/src/picotls-populate-stamp"
  "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix/src"
  "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix/src/picotls-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix/src/picotls-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/Users/dohyeonlim/mQUIC-1/build/_deps/picotls-subbuild/picotls-populate-prefix/src/picotls-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
