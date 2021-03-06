// Copyright (c) 2020 The Orbit Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ProcessList.h"

#include <absl/strings/ascii.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_format.h>

#include <filesystem>
#include <unordered_map>

#include "LinuxUtils.h"
#include "OrbitBase/Logging.h"
#include "Utils.h"

outcome::result<void, std::string> ProcessList::Refresh() {
  const auto cpu_result = LinuxUtils::GetCpuUtilization();
  if (!cpu_result) {
    return outcome::failure(
        absl::StrFormat("Unable to retrieve cpu usage of processes: %s",
                        cpu_result.error().message()));
  }
  std::unordered_map<int32_t, double> cpu_usage_map =
      std::move(cpu_result.value());

  std::vector<ProcessInfo> updated_processes;

  for (const auto& directory_entry :
       std::filesystem::directory_iterator("/proc")) {
    if (!directory_entry.is_directory()) continue;

    const std::filesystem::path path = directory_entry.path();
    std::string folder_name = path.filename().string();

    uint32_t pid;
    if (!absl::SimpleAtoi(folder_name, &pid)) continue;

    auto iter = processes_map_.find(pid);
    if (iter != processes_map_.end()) {
      ProcessInfo& process(*(iter->second));
      process.set_cpu_usage(cpu_usage_map[process.pid()]);
      updated_processes.push_back(process);
      continue;
    }

    const std::filesystem::path name_file_path = path / "comm";
    const auto name_file_result = OrbitUtils::FileToString(name_file_path);
    if (!name_file_result) {
      ERROR("Failed to read %s: %s", name_file_path.string(),
            name_file_result.error().message());
      continue;
    }
    std::string name = std::move(name_file_result.value());
    // Remove new line character.
    absl::StripTrailingAsciiWhitespace(&name);
    if (name.empty()) continue;

    ProcessInfo process;
    process.set_pid(pid);
    process.set_name(name);
    process.set_cpu_usage(cpu_usage_map[pid]);

    // "The command-line arguments appear [...] as a set of strings
    // separated by null bytes ('\0')".
    const std::filesystem::path cmdline_file_path =
        directory_entry.path() / "cmdline";
    const auto cmdline_file_result =
        OrbitUtils::FileToString(cmdline_file_path);
    if (!cmdline_file_result) {
      ERROR("Failed to read %s: %s", cmdline_file_path.string(),
            name_file_result.error().message());
      continue;
    }
    std::string cmdline = std::move(cmdline_file_result.value());
    process.set_full_path(cmdline.substr(0, cmdline.find('\0')));

    std::replace(cmdline.begin(), cmdline.end(), '\0', ' ');
    process.set_command_line(cmdline);

    const auto& is_64_bit_result = LinuxUtils::Is64Bit(pid);
    if (!is_64_bit_result) {
      ERROR("Failed to get if process \"%s\" (pid %d) is 64 bit: %s",
            name.c_str(), pid, is_64_bit_result.error().message().c_str());
      continue;
    }
    process.set_is_64_bit(is_64_bit_result.value());

    updated_processes.push_back(process);
  }

  processes_ = std::move(updated_processes);
  processes_map_.clear();
  for (auto& process : processes_) {
    processes_map_[process.pid()] = &process;
  }

  return outcome::success();
}
