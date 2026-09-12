#include "agent/skill_registry.hpp"

#include <fstream>
#include <sstream>

namespace agent {

namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

}  // namespace

std::optional<Skill> SkillRegistry::load_skill(const std::filesystem::path& file) {
    std::ifstream input(file);
    if (!input) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string content = buffer.str();

    std::istringstream lines(content);
    std::string line;
    if (!std::getline(lines, line) || trim(line) != "---") {
        return std::nullopt;
    }

    Skill skill;
    while (std::getline(lines, line)) {
        if (trim(line) == "---") {
            break;
        }
        const auto separator = line.find(':');
        if (separator == std::string::npos) {
            continue;
        }
        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        if (key == "name") {
            skill.name = value;
        } else if (key == "description") {
            skill.description = value;
        }
    }

    std::ostringstream instructions;
    instructions << lines.rdbuf();
    skill.instructions = trim(instructions.str());
    skill.root = file.parent_path();

    if (skill.name.empty() || skill.description.empty()) {
        return std::nullopt;
    }
    return skill;
}

std::size_t SkillRegistry::load_directory(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
        return 0;
    }

    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_directory()) {
            continue;
        }
        if (auto skill = load_skill(entry.path() / "SKILL.md")) {
            skills_.insert_or_assign(skill->name, std::move(*skill));
            ++loaded;
        }
    }
    return loaded;
}

std::vector<Skill> SkillRegistry::list() const {
    std::vector<Skill> result;
    result.reserve(skills_.size());
    for (const auto& [name, skill] : skills_) {
        (void)name;
        result.push_back(skill);
    }
    return result;
}

std::optional<Skill> SkillRegistry::find(const std::string& name) const {
    const auto iterator = skills_.find(name);
    if (iterator == skills_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

}  // namespace agent

