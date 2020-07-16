/*
 * RSessionLaunchProfile.cpp
 *
 * Copyright (C) 2009-19 by RStudio, PBC
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */

#include <core/r_util/RSessionLaunchProfile.hpp>

#include <shared_core/SafeConvert.hpp>

#include <core/system/PosixSched.hpp>
#include <core/system/Crypto.hpp>

#include <core/Algorithm.hpp>

#include <core/json/JsonRpc.hpp>

namespace rstudio {
namespace core {
namespace r_util {

namespace {

json::Object contextAsJson(const SessionContext& context)
{
   json::Object scopeJson;
   scopeJson["username"] = context.username;
   scopeJson["project"] = context.scope.project();
   scopeJson["id"] = context.scope.id();
   return scopeJson;
}

Error contextFromJson(const json::Object& contextJson, SessionContext* pContext)
{
   std::string project, id;
   Error error = json::readObject(contextJson,
                           "username", &(pContext->username),
                           "project", &project,
                           "id", &id);
   if (error)
      return error;

   pContext->scope = r_util::SessionScope::fromProjectId(project, id);

   return Success();
}


Error cpuAffinityFromJson(const json::Array& affinityJson,
                          core::system::CpuAffinity* pAffinity)
{
   pAffinity->clear();

   for (const json::Value& val : affinityJson)
   {
      if (!json::isType<bool>(val))
         return Error(json::errc::ParamTypeMismatch, ERROR_LOCATION);

      pAffinity->push_back(val.getBool());
   }

   return Success();
}

json::Value toJson(RLimitType limit)
{
   try
   {
      return json::Value(boost::uint64_t(limit));
   }
   catch(...)
   {
      return json::Value(0);
   }
}

} // anonymous namespace


json::Object sessionLaunchProfileToJson(const SessionLaunchProfile& profile)
{
   json::Object profileJson;
   profileJson["context"] = contextAsJson(profile.context);
   profileJson["password"] = profile.password;
   profileJson["encryptionKey"] = profile.encryptionKey;
   profileJson["executablePath"] = profile.executablePath;
   json::Object configJson;
   configJson["args"] = json::Array(profile.config.args);
   configJson["environment"] = json::Object(profile.config.environment);
   configJson["stdInput"] = profile.config.stdInput;
   configJson["stdStreamBehavior"] = profile.config.stdStreamBehavior;
   configJson["priority"] = profile.config.limits.priority;
   configJson["memoryLimitBytes"] = toJson(profile.config.limits.memoryLimitBytes);
   configJson["stackLimitBytes"] = toJson(profile.config.limits.stackLimitBytes);
   configJson["userProcessesLimit"] = toJson(profile.config.limits.userProcessesLimit);
   configJson["cpuLimit"] = toJson(profile.config.limits.cpuLimit);
   configJson["niceLimit"] = toJson(profile.config.limits.niceLimit);
   configJson["filesLimit"] = toJson(profile.config.limits.filesLimit);
   configJson["cpuAffinity"] = json::toJsonArray(profile.config.limits.cpuAffinity);
   profileJson["config"] = configJson;
   return profileJson;
}

SessionLaunchProfile sessionLaunchProfileFromJson(const json::Object& jsonProfile)
{
   SessionLaunchProfile profile;

   // read top level fields
   json::Object configJson, contextJson;
   Error error = json::readObject(jsonProfile,
                                  "context", &contextJson,
                                  "password", &profile.password,
                                  "encryptionKey", &profile.encryptionKey,
                                  "executablePath", &profile.executablePath,
                                  "config", &configJson);
   if (error)
      LOG_ERROR(error);

   // read context object
   error = contextFromJson(contextJson, &(profile.context));
   if (error)
      LOG_ERROR(error);

   // read config object
   json::Object envJson;
   json::Array argsJson;
   std::string stdInput;
   int stdStreamBehavior = 0;
   int priority = 0;
   double memoryLimitBytes, stackLimitBytes, userProcessesLimit,
          cpuLimit, niceLimit, filesLimit;
   error = json::readObject(configJson,
                           "args", &argsJson,
                           "environment", &envJson,
                           "stdInput", &stdInput,
                           "stdStreamBehavior", &stdStreamBehavior,
                           "priority", &priority,
                           "memoryLimitBytes", &memoryLimitBytes,
                           "stackLimitBytes", &stackLimitBytes,
                           "userProcessesLimit", &userProcessesLimit,
                           "cpuLimit", &cpuLimit,
                           "niceLimit", &niceLimit,
                           "filesLimit", &filesLimit);
   if (error)
      LOG_ERROR(error);

   // read and convert cpu affinity
   core::system::CpuAffinity cpuAffinity;
   json::Array cpuAffinityJson;
   error = json::readObject(configJson,
                            "cpuAffinity", &cpuAffinityJson);
   if (error)
      LOG_ERROR(error);
   error = cpuAffinityFromJson(cpuAffinityJson, &cpuAffinity);
   if (error)
   {
      cpuAffinity.clear();
      LOG_ERROR(error);
   }

   // populate config
   profile.config.args = argsJson.toStringPairList();
   profile.config.environment = envJson.toStringPairList();
   profile.config.stdInput = stdInput;
   profile.config.stdStreamBehavior =
            static_cast<core::system::StdStreamBehavior>(stdStreamBehavior);
   profile.config.limits.priority = priority;
   profile.config.limits.memoryLimitBytes = memoryLimitBytes;
   profile.config.limits.stackLimitBytes = stackLimitBytes;
   profile.config.limits.userProcessesLimit = userProcessesLimit;
   profile.config.limits.cpuLimit = cpuLimit;
   profile.config.limits.niceLimit = niceLimit;
   profile.config.limits.filesLimit = filesLimit;
   profile.config.limits.cpuAffinity = cpuAffinity;

   // return profile
   return profile;
}

// Launcher jobs (submitted by ServerJobLauncher.cpp) cannot have plain-text passwords,
// otherwise these passwords could be exposed (including logs on disk!) and leak.
// This function will encrypt the user's password returning the encrypted password or an error.
// The password itself will be blank in the session profile and an encryption key will be there
// instead. The password itself must be passed to the launching session via any out-of-band means.
//
// IMPORTANT: For a secure implementation the encrypted password cannot be part of the enviroment,
// command arguments, or input of the launching session. Other means like file system, network, IPC
// or RPC need to be used instead to send the password to the session.
Error encryptProfilePassword(SessionLaunchProfile& profile, std::string* encryptedPassword)
{
   Error error;

   // generate a 256-bit key to encrypt the user's password, convert it to a string
   std::vector<unsigned char> passwordKey;
   error = core::system::crypto::random(32, &passwordKey);
   if (error)
   {
      return error;
   }
   std::string passwordKeyStr;
   std::copy(passwordKey.begin(), passwordKey.end(), std::back_inserter(passwordKeyStr));

   // encrypt the user's password, keep the encrypted one, drop the unencrypted
   std::string ivPasswordKey;
   error = core::system::crypto::encryptDataAsBase64EncodedString(
                                       profile.password,
                                       passwordKeyStr,
                                       &ivPasswordKey,
                                       encryptedPassword);
   if (error)
   {
      return error;
   }

   std::string base64PasswordKey;
   error = core::system::crypto::base64Encode(passwordKey.data(), passwordKey.size(), &base64PasswordKey);
   if (error)
   {
      return error;
   }

   std::string encryptionKey = core::algorithm::join({ base64PasswordKey, ivPasswordKey }, "|");
   profile.password = "";
   profile.encryptionKey = encryptionKey;
   return Success();
}

Error decryptProfilePassword(SessionLaunchProfile& profile, const std::string& encryptedPassword)
{
   Error error;

   std::string passwordKey;
   std::string ivPasswordKey;
   std::vector<std::string> passwordKeyParts = core::algorithm::split(profile.encryptionKey, "|");
   if (passwordKeyParts.size() != 2)
   {
      return unknownError("Profile password encryption key invalid format", Success(), ERROR_LOCATION);
   }
   passwordKey = passwordKeyParts[0];
   ivPasswordKey = passwordKeyParts[1];

   std::vector<unsigned char> passwordKeyBin;
   error = core::system::crypto::base64Decode(passwordKey, &passwordKeyBin);
   if (error)
   {
      return error;
   }
   std::string passwordKeyBinStr;
   std::copy(passwordKeyBin.begin(), passwordKeyBin.end(), std::back_inserter(passwordKeyBinStr));

   // decrypted the actual password with the password key+iv
   std::string decryptedPassword;
   error = core::system::crypto::decryptBase64EncodedString(
                                             encryptedPassword,
                                             passwordKeyBinStr,
                                             ivPasswordKey,
                                             &decryptedPassword);
   if (error)
   {
      return error;
   }
   profile.password = decryptedPassword;
   profile.encryptionKey = "";
   return Success();
}

} // namespace r_util
} // namespace core 
} // namespace rstudio



