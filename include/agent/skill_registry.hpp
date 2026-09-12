#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace agent {

struct Skill {
    std::string name;
    std::string description;
    std::string instructions;
    std::filesystem::path root;
};

class SkillRegistry {
public:
    std::size_t load_directory(const std::filesystem::path& directory);
    std::vector<Skill> list() const;
    std::optional<Skill> find(const std::string& name) const;

private:
    static std::optional<Skill> load_skill(const std::filesystem::path& file);
    std::map<std::string, Skill> skills_;
};

}  // namespace agent

