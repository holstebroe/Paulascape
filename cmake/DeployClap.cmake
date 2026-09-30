# DeployClap.cmake - run in script mode (cmake -DCLAP_FILE=<file> -P DeployClap.cmake).
# Copies the built .clap into the folder named by the CLAPTEST environment variable.

if(NOT CLAP_FILE)
    message(FATAL_ERROR "DeployClap: CLAP_FILE is not set")
endif()

if(NOT DEFINED ENV{CLAPTEST} OR "$ENV{CLAPTEST}" STREQUAL "")
    message(FATAL_ERROR "DeployClap: set the CLAPTEST environment variable to the folder the .clap should be copied to")
endif()

file(MAKE_DIRECTORY "$ENV{CLAPTEST}")
get_filename_component(_name "${CLAP_FILE}" NAME)
file(COPY_FILE "${CLAP_FILE}" "$ENV{CLAPTEST}/${_name}" ONLY_IF_DIFFERENT RESULT _result)
if(NOT _result STREQUAL "0")
    message(FATAL_ERROR "DeployClap: could not copy ${_name} to $ENV{CLAPTEST} (${_result}). Is a host holding the file open?")
endif()
message(STATUS "Deployed ${_name} to $ENV{CLAPTEST}")
