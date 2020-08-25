// Copyright 2019 Open Source Robotics Foundation, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"

#include <cerrno>
#include <cinttypes>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <rcutils/allocator.h>
#include <rcutils/filesystem.h>
#include <rcutils/get_env.h>
#include <rcutils/logging.h>
#include <rcutils/process.h>
#include <rcutils/snprintf.h>
#include <rcutils/time.h>
#include <spdlog/async.h>
#include <spdlog/sinks/syslog_sink.h>
#include <sstream>
#include <utility>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

#include "rcl_logging_interface/rcl_logging_interface.h"

static std::mutex g_logger_mutex;
static std::shared_ptr<spdlog::logger> g_root_logger = nullptr;

#include <sys/types.h>
#include <sys/stat.h>

namespace helper{
/******************************************************************************
* Checks to see if a directory exists. Note: This method only checks the
* existence of the full path AND if path leaf is a dir.
*
* @return  >0 if dir exists AND is a dir,
*           0 if dir does not exist OR exists but not a dir,
*          <0 if an error occurred (errno is also set)
*****************************************************************************/
static inline int dirExists(const char* const path)
{
  struct stat info;

  int statRC = stat( path, &info );
  if( statRC != 0 )
  {
    if (errno == ENOENT)  { return 0; } // something along the path does not exist
    if (errno == ENOTDIR) { return 0; } // something in path prefix is not a dir
    return -1;
  }

  return ( info.st_mode & S_IFDIR ) ? 1 : 0;
}

static std::string getLogDirectory()
{
  std::string logDir("");
  char *      logDirChar = getenv("LOG_DIR");

  if (logDirChar == NULL) {
    std::cerr << "LOG_DIR NOT SET! Using executable directory" << std::endl;
    return logDir;
  }

  if (helper::dirExists(logDirChar) == 0) {
    int status = mkdir(logDirChar, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    std::cout << "Created log directory with status " << status << std::endl;
  }

  logDir = logDirChar;
  return logDir;
}

}

static spdlog::level::level_enum map_external_log_level_to_library_level(int external_level)
{
  spdlog::level::level_enum level = spdlog::level::level_enum::off;

  // map to the next highest level of severity
  if (external_level <= RCUTILS_LOG_SEVERITY_DEBUG) {
    level = spdlog::level::level_enum::debug;
  } else if (external_level <= RCUTILS_LOG_SEVERITY_INFO) {
    level = spdlog::level::level_enum::info;
  } else if (external_level <= RCUTILS_LOG_SEVERITY_WARN) {
    level = spdlog::level::level_enum::warn;
  } else if (external_level <= RCUTILS_LOG_SEVERITY_ERROR) {
    level = spdlog::level::level_enum::err;
  } else if (external_level <= RCUTILS_LOG_SEVERITY_FATAL) {
    level = spdlog::level::level_enum::critical;
  }
  return level;
}

std::vector<spdlog::sink_ptr> create_sinks(const std::string &             logfileName,
                                           const spdlog::level::level_enum fileLevel = spdlog::level::trace,
                                           const spdlog::level::level_enum consoleLevel = spdlog::level::trace)
{

//  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
//  console_sink->set_level(consoleLevel);

  using Clock = std::chrono::system_clock;

  std::time_t now = Clock::to_time_t(Clock::now());

  std::stringstream timeString;
  timeString << std::put_time(std::localtime(&now), "%d-%m-%Y-%X");

  std::string logFile = helper::getLogDirectory() + "/" + logfileName + "_" + timeString.str() + ".log";
  std::cout << "Setting logger up for logging to file: " << logFile << std::endl;

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFile, true);
  file_sink->set_pattern("%v");
  file_sink->set_level(fileLevel);

  auto syslog_sink = std::make_shared<spdlog::sinks::syslog_sink_mt>(logfileName, LOG_PID, LOG_USER, false);
  syslog_sink->set_level(consoleLevel);

  return {file_sink, syslog_sink};
}

rcl_logging_ret_t rcl_logging_external_initialize(
  const char * config_file,
  rcutils_allocator_t allocator)
{
  std::lock_guard<std::mutex> lk(g_logger_mutex);
  // It is possible for this to get called more than once in a process (some of
  // the tests do this implicitly by calling rclcpp::init more than once).
  // If the logger is already setup, don't do anything.
  if (g_root_logger != nullptr) {
    return RCL_LOGGING_RET_OK;
  }

  bool config_file_provided = (nullptr != config_file) && (config_file[0] != '\0');
  if (config_file_provided) {
    // TODO(clalancette): implement support for an external configuration file.
    RCUTILS_SET_ERROR_MSG(
      "spdlog logging backend doesn't currently support external configuration");
    return RCL_LOGGING_RET_ERROR;
  } else {

    // SPDLOG doesn't automatically create the log directories, so make them
    // by hand here.
    char name_buffer[4096] = {0};

    // Get the program name.
    char * basec = rcutils_get_executable_name(allocator);
    if (basec == nullptr) {
      // We couldn't get the program name, so get out of here without setting up
      // logging.
      RCUTILS_SET_ERROR_MSG("Failed to get the executable name");
      return RCL_LOGGING_RET_ERROR;
    }

    auto print_ret = rcutils_snprintf(name_buffer, sizeof(name_buffer),
                                      "%s", basec);
    allocator.deallocate(basec, allocator.state);
    if (print_ret < 0) {
      RCUTILS_SET_ERROR_MSG("Failed to create log file name string");
      return RCL_LOGGING_RET_ERROR;
    }

    if(spdlog::thread_pool() == nullptr) {
      spdlog::init_thread_pool(8192, 1);
    }
    auto sinks = create_sinks(name_buffer);
    g_root_logger = std::make_shared<spdlog::async_logger>("root",
                                                           sinks.begin(),
                                                           sinks.end(),
                                                           spdlog::thread_pool(),
                                                           spdlog::async_overflow_policy::block);

    g_root_logger->set_pattern("%v");
    spdlog::register_logger(g_root_logger);
  }

  return RCL_LOGGING_RET_OK;
}

rcl_logging_ret_t rcl_logging_external_shutdown()
{
  g_root_logger = nullptr;
  return RCL_LOGGING_RET_OK;
}

void rcl_logging_external_log(int severity, const char * name, const char * msg)
{
  (void)name;
  g_root_logger->log(map_external_log_level_to_library_level(severity), msg);
}

rcl_logging_ret_t rcl_logging_external_set_logger_level(const char * name, int level)
{
  (void)name;

  g_root_logger->set_level(map_external_log_level_to_library_level(level));

  return RCL_LOGGING_RET_OK;
}
