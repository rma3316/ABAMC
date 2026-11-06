// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace PackCode {

class Error : public std::runtime_error {
   public:
    explicit Error(const std::string& message) : std::runtime_error(message) {}
};

enum class ModLoader { Fabric, NeoForge };

struct ModRef {
    std::string projectId;
    std::optional<std::string> versionId;
};

struct ServerRef {
    std::string address;
    std::optional<std::string> name;
};

struct Preset {
    std::string name;
    std::string minecraftVersion;
    ModLoader loader = ModLoader::Fabric;
    std::vector<ModRef> mods;
    std::optional<ServerRef> server;
    std::optional<std::string> loaderVersion;

    [[nodiscard]] Preset normalized() const;
    [[nodiscard]] std::string instanceDirectoryName() const;
};

[[nodiscard]] std::string loaderToJson(ModLoader loader);
[[nodiscard]] ModLoader loaderFromJson(const std::string& value);
[[nodiscard]] std::string normalizeAddress(const std::string& address);
[[nodiscard]] std::string canonicalJson(const Preset& preset);
[[nodiscard]] Preset parsePresetJson(const std::string& json);
[[nodiscard]] std::string encode(const Preset& preset);
[[nodiscard]] Preset decode(const std::string& code);

[[nodiscard]] bool minecraftAtLeast121(const std::string& version);

[[nodiscard]] std::vector<unsigned char> writeServersDat(const std::vector<ServerRef>& servers);
[[nodiscard]] std::vector<ServerRef> readServersDat(const std::vector<unsigned char>& gzipped);

}  // namespace PackCode
