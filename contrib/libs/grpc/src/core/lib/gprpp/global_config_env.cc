//
//
// Copyright 2019 gRPC authors.
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
//
//

#include <grpc/support/port_platform.h>

#include "src/core/lib/gprpp/global_config_env.h"

#include <ctype.h>
#include <stdlib.h>

#include <initializer_list>
#include <memory>
#include <util/generic/string.h>
#include <util/string/cast.h>
#include <type_traits>

#include "y_absl/strings/str_format.h"
#include "y_absl/types/optional.h"

#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/env.h"

namespace grpc_core {

namespace {

void DefaultGlobalConfigEnvErrorFunction(const char* error_message) {
  gpr_log(GPR_ERROR, "%s", error_message);
}

GlobalConfigEnvErrorFunctionType g_global_config_env_error_func =
    DefaultGlobalConfigEnvErrorFunction;

void LogParsingError(const char* name, const char* value) {
  TString error_message = y_absl::StrFormat(
      "Illegal value '%s' specified for environment variable '%s'", value,
      name);
  (*g_global_config_env_error_func)(error_message.c_str());
}

}  // namespace

void SetGlobalConfigEnvErrorFunction(GlobalConfigEnvErrorFunctionType func) {
  g_global_config_env_error_func = func;
}

UniquePtr<char> GlobalConfigEnv::GetValue() {
  auto env = GetEnv(GetName());
  return UniquePtr<char>(env.has_value() ? gpr_strdup(env.value().c_str())
                                         : nullptr);
}

void GlobalConfigEnv::SetValue(const char* value) { SetEnv(GetName(), value); }

void GlobalConfigEnv::Unset() { UnsetEnv(GetName()); }

char* GlobalConfigEnv::GetName() {
  // This makes sure that name_ is in a canonical form having uppercase
  // letters. This is okay to be called serveral times.
  for (char* c = name_; *c != 0; ++c) {
    *c = toupper(*c);
  }
  return name_;
}
static_assert(std::is_trivially_destructible<GlobalConfigEnvBool>::value,
              "GlobalConfigEnvBool needs to be trivially destructible.");

bool GlobalConfigEnvBool::Get() {
  UniquePtr<char> str = GetValue();
  if (str == nullptr) {
    return default_value_;
  }
  // parsing given value string.
  bool result = false;
  if (!gpr_parse_bool_value(str.get(), &result)) {
    LogParsingError(GetName(), str.get());
    result = default_value_;
  }
  return result;
}

void GlobalConfigEnvBool::Set(bool value) {
  SetValue(value ? "true" : "false");
}

static_assert(std::is_trivially_destructible<GlobalConfigEnvInt32>::value,
              "GlobalConfigEnvInt32 needs to be trivially destructible.");

int32_t GlobalConfigEnvInt32::Get() {
  UniquePtr<char> str = GetValue();
  if (str == nullptr) {
    return default_value_;
  }
  // parsing given value string.
  char* end = str.get();
  long result = strtol(str.get(), &end, 10);
  if (*end != 0) {
    LogParsingError(GetName(), str.get());
    result = default_value_;
  }
  return static_cast<int32_t>(result);
}

void GlobalConfigEnvInt32::Set(int32_t value) {
  char buffer[GPR_LTOA_MIN_BUFSIZE];
  gpr_ltoa(value, buffer);
  SetValue(buffer);
}

static_assert(std::is_trivially_destructible<GlobalConfigEnvString>::value,
              "GlobalConfigEnvString needs to be trivially destructible.");

UniquePtr<char> GlobalConfigEnvString::Get() {
  UniquePtr<char> str = GetValue();
  if (str == nullptr) {
    return UniquePtr<char>(gpr_strdup(default_value_));
  }
  return str;
}

void GlobalConfigEnvString::Set(const char* value) { SetValue(value); }

}  // namespace grpc_core
