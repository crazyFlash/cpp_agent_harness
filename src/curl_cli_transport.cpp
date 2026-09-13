#include "agent/curl_cli_transport.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <exception>
#include <functional>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace agent {

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
#ifndef _WIN32
        std::string pattern = "/tmp/cpp-agent-http-XXXXXX";
        std::vector<char> writable(pattern.begin(), pattern.end());
        writable.push_back('\0');
        const char* created = ::mkdtemp(writable.data());
        if (created == nullptr) {
            throw std::runtime_error(
                std::string{"cannot create HTTP temporary directory: "} +
                std::strerror(errno));
        }
        path_ = created;
#else
        throw std::runtime_error("CurlCliTransport is not implemented on Windows");
#endif
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

void reject_line_breaks(std::string_view value, std::string_view field) {
    if (value.find('\n') != std::string_view::npos ||
        value.find('\r') != std::string_view::npos ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " contains invalid characters");
    }
}

std::string curl_config_quote(std::string_view value) {
    std::string output{"\""};
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            output += '\\';
        }
        output += character;
    }
    output += '"';
    return output;
}

void write_private_file(const std::filesystem::path& path, std::string_view content) {
#ifndef _WIN32
    const int descriptor = ::open(
        path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        throw std::runtime_error(
            "cannot create private HTTP file: " + std::string{std::strerror(errno)});
    }

    std::size_t written = 0;
    while (written < content.size()) {
        const auto count = ::write(
            descriptor, content.data() + written, content.size() - written);
        if (count < 0) {
            const int error = errno;
            ::close(descriptor);
            throw std::runtime_error(
                "cannot write private HTTP file: " +
                std::string{std::strerror(error)});
        }
        written += static_cast<std::size_t>(count);
    }
    if (::close(descriptor) != 0) {
        throw std::runtime_error(
            "cannot close private HTTP file: " + std::string{std::strerror(errno)});
    }
#else
    (void)path;
    (void)content;
    throw std::runtime_error("private temporary files are not implemented on Windows");
#endif
}

std::string read_bounded_file(const std::filesystem::path& path,
                              std::size_t max_bytes,
                              std::string_view description) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error(
            "cannot inspect " + std::string{description} + ": " + error.message());
    }
    if (size > max_bytes) {
        throw std::runtime_error(
            std::string{description} + " exceeds configured size limit");
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + std::string{description});
    }
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

#ifndef _WIN32
int spawn_curl(const CurlCliConfig& config,
               const std::filesystem::path& curl_config,
               const std::filesystem::path& status_output,
               const std::filesystem::path& error_output) {
    posix_spawn_file_actions_t actions;
    int result = ::posix_spawn_file_actions_init(&actions);
    if (result != 0) {
        throw std::runtime_error(
            "cannot initialize curl process actions: " +
            std::string{std::strerror(result)});
    }

    auto destroy_actions = [&actions]() { ::posix_spawn_file_actions_destroy(&actions); };
    result = ::posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (result == 0) {
        result = ::posix_spawn_file_actions_addopen(
            &actions,
            STDOUT_FILENO,
            status_output.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC,
            0600);
    }
    if (result == 0) {
        result = ::posix_spawn_file_actions_addopen(
            &actions,
            STDERR_FILENO,
            error_output.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC,
            0600);
    }
    if (result != 0) {
        destroy_actions();
        throw std::runtime_error(
            "cannot configure curl process files: " +
            std::string{std::strerror(result)});
    }

    std::vector<std::string> arguments_storage{
        config.executable, "--disable", "--config", curl_config.string()};
    std::vector<char*> arguments;
    arguments.reserve(arguments_storage.size() + 1);
    for (auto& argument : arguments_storage) {
        arguments.push_back(argument.data());
    }
    arguments.push_back(nullptr);

    pid_t process = 0;
    result = ::posix_spawnp(
        &process,
        config.executable.c_str(),
        &actions,
        nullptr,
        arguments.data(),
        environ);
    destroy_actions();
    if (result != 0) {
        throw std::runtime_error(
            "cannot start curl executable '" + config.executable + "': " +
            std::string{std::strerror(result)});
    }

    int status = 0;
    while (::waitpid(process, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error(
                "cannot wait for curl process: " + std::string{std::strerror(errno)});
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        throw std::runtime_error(
            "curl process terminated by signal " + std::to_string(WTERMSIG(status)));
    }
    throw std::runtime_error("curl process ended in an unknown state");
}

int wait_for_process(pid_t process) {
    int status = 0;
    while (::waitpid(process, &status, 0) < 0) {
        if (errno != EINTR) {
            throw std::runtime_error(
                "cannot wait for curl process: " + std::string{std::strerror(errno)});
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        throw std::runtime_error(
            "curl process terminated by signal " + std::to_string(WTERMSIG(status)));
    }
    throw std::runtime_error("curl process ended in an unknown state");
}

int parse_last_http_status(std::string_view headers) {
    int status = 0;
    std::size_t position = 0;
    while ((position = headers.find("HTTP/", position)) != std::string_view::npos) {
        const auto space = headers.find(' ', position);
        if (space != std::string_view::npos && space + 4 <= headers.size()) {
            const auto digits = headers.substr(space + 1, 3);
            if (digits[0] >= '0' && digits[0] <= '9' &&
                digits[1] >= '0' && digits[1] <= '9' &&
                digits[2] >= '0' && digits[2] <= '9') {
                status = (digits[0] - '0') * 100 +
                         (digits[1] - '0') * 10 + (digits[2] - '0');
            }
        }
        position += 5;
    }
    if (status < 100 || status > 599) {
        throw std::runtime_error("curl returned invalid HTTP response headers");
    }
    return status;
}
#endif

}  // namespace

CurlCliTransport::CurlCliTransport(CurlCliConfig config)
    : config_(std::move(config)) {
    if (config_.executable.empty()) {
        throw std::invalid_argument("curl executable cannot be empty");
    }
    if (config_.max_response_bytes == 0) {
        throw std::invalid_argument("max response size must be greater than zero");
    }
}

HttpResponse CurlCliTransport::send(const HttpRequest& request) {
#ifdef _WIN32
    (void)request;
    throw std::runtime_error("CurlCliTransport is not implemented on Windows");
#else
    if (request.url.empty()) {
        throw std::invalid_argument("HTTP request URL cannot be empty");
    }
    if (request.timeout.count() <= 0) {
        throw std::invalid_argument("HTTP timeout must be greater than zero");
    }
    reject_line_breaks(request.method, "HTTP method");
    reject_line_breaks(request.url, "HTTP URL");

    TemporaryDirectory temporary;
    const auto request_body = temporary.path() / "request-body";
    const auto response_body = temporary.path() / "response-body";
    const auto status_output = temporary.path() / "status";
    const auto error_output = temporary.path() / "stderr";
    const auto curl_config = temporary.path() / "curl.conf";

    write_private_file(request_body, request.body);

    std::ostringstream configuration;
    configuration << "silent\nshow-error\n"
                  << "noproxy = \"localhost,127.0.0.1,::1\"\n"
                  << "request = " << curl_config_quote(request.method) << '\n'
                  << "url = " << curl_config_quote(request.url) << '\n';
    for (const auto& [name, value] : request.headers) {
        reject_line_breaks(name, "HTTP header name");
        reject_line_breaks(value, "HTTP header value");
        configuration << "header = "
                      << curl_config_quote(name + ": " + value) << '\n';
    }
    configuration << "data-binary = "
                  << curl_config_quote("@" + request_body.string()) << '\n'
                  << "output = " << curl_config_quote(response_body.string()) << '\n'
                  << "write-out = \"%{http_code}\"\n"
                  << "max-time = \"" << std::fixed << std::setprecision(3)
                  << static_cast<double>(request.timeout.count()) / 1000.0 << "\"\n";
    write_private_file(curl_config, configuration.str());

    const int exit_code = spawn_curl(
        config_, curl_config, status_output, error_output);
    const auto error_text = read_bounded_file(error_output, 64 * 1024, "curl stderr");
    if (exit_code != 0) {
        throw std::runtime_error(
            "curl failed with exit code " + std::to_string(exit_code) +
            (error_text.empty() ? std::string{} : ": " + error_text));
    }

    const auto status_text = read_bounded_file(status_output, 32, "HTTP status");
    std::size_t consumed = 0;
    int status_code = 0;
    try {
        status_code = std::stoi(status_text, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error("curl returned an invalid HTTP status: " + status_text);
    }
    if (consumed != status_text.size() || status_code < 100 || status_code > 599) {
        throw std::runtime_error("curl returned an invalid HTTP status: " + status_text);
    }

    return {
        status_code,
        {},
        read_bounded_file(
            response_body, config_.max_response_bytes, "HTTP response body"),
    };
#endif
}

HttpResponse CurlCliTransport::send_stream(
    const HttpRequest& request,
    const BodyChunkCallback& on_chunk) {
#ifdef _WIN32
    (void)request;
    (void)on_chunk;
    throw std::runtime_error("CurlCliTransport is not implemented on Windows");
#else
    if (!on_chunk) {
        throw std::invalid_argument("stream callback cannot be empty");
    }
    if (request.url.empty() || request.timeout.count() <= 0) {
        throw std::invalid_argument("HTTP stream request is invalid");
    }
    reject_line_breaks(request.method, "HTTP method");
    reject_line_breaks(request.url, "HTTP URL");

    TemporaryDirectory temporary;
    const auto request_body = temporary.path() / "request-body";
    const auto response_headers = temporary.path() / "response-headers";
    const auto error_output = temporary.path() / "stderr";
    const auto curl_config = temporary.path() / "curl.conf";
    write_private_file(request_body, request.body);

    std::ostringstream configuration;
    configuration << "silent\nshow-error\nno-buffer\n"
                  << "noproxy = \"localhost,127.0.0.1,::1\"\n"
                  << "request = " << curl_config_quote(request.method) << '\n'
                  << "url = " << curl_config_quote(request.url) << '\n';
    for (const auto& [name, value] : request.headers) {
        reject_line_breaks(name, "HTTP header name");
        reject_line_breaks(value, "HTTP header value");
        configuration << "header = "
                      << curl_config_quote(name + ": " + value) << '\n';
    }
    configuration << "data-binary = "
                  << curl_config_quote("@" + request_body.string()) << '\n'
                  << "dump-header = " << curl_config_quote(response_headers.string())
                  << '\n'
                  << "max-time = \"" << std::fixed << std::setprecision(3)
                  << static_cast<double>(request.timeout.count()) / 1000.0 << "\"\n";
    write_private_file(curl_config, configuration.str());

    int body_pipe[2];
    if (::pipe(body_pipe) != 0) {
        throw std::runtime_error(
            "cannot create curl stream pipe: " + std::string{std::strerror(errno)});
    }

    posix_spawn_file_actions_t actions;
    int result = ::posix_spawn_file_actions_init(&actions);
    if (result == 0) result = ::posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (result == 0) result = ::posix_spawn_file_actions_adddup2(
        &actions, body_pipe[1], STDOUT_FILENO);
    if (result == 0) result = ::posix_spawn_file_actions_addclose(
        &actions, body_pipe[0]);
    if (result == 0) result = ::posix_spawn_file_actions_addclose(
        &actions, body_pipe[1]);
    if (result == 0) result = ::posix_spawn_file_actions_addopen(
        &actions, STDERR_FILENO, error_output.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (result != 0) {
        ::posix_spawn_file_actions_destroy(&actions);
        ::close(body_pipe[0]);
        ::close(body_pipe[1]);
        throw std::runtime_error(
            "cannot configure curl stream process: " +
            std::string{std::strerror(result)});
    }

    std::vector<std::string> argument_storage{
        config_.executable, "--disable", "--config", curl_config.string()};
    std::vector<char*> arguments;
    for (auto& argument : argument_storage) arguments.push_back(argument.data());
    arguments.push_back(nullptr);
    pid_t process = 0;
    result = ::posix_spawnp(&process, config_.executable.c_str(), &actions,
                            nullptr, arguments.data(), environ);
    ::posix_spawn_file_actions_destroy(&actions);
    ::close(body_pipe[1]);
    if (result != 0) {
        ::close(body_pipe[0]);
        throw std::runtime_error(
            "cannot start curl executable '" + config_.executable + "': " +
            std::string{std::strerror(result)});
    }

    std::size_t received = 0;
    std::exception_ptr callback_error;
    char buffer[4096];
    while (true) {
        const auto count = ::read(body_pipe[0], buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            callback_error = std::make_exception_ptr(std::runtime_error(
                "cannot read curl stream: " + std::string{std::strerror(errno)}));
            break;
        }
        received += static_cast<std::size_t>(count);
        if (received > config_.max_response_bytes && !callback_error) {
            callback_error = std::make_exception_ptr(
                std::runtime_error("HTTP response body exceeds configured size limit"));
        }
        if (!callback_error) {
            try {
                on_chunk(std::string_view{buffer, static_cast<std::size_t>(count)});
            } catch (...) {
                callback_error = std::current_exception();
            }
        }
    }
    ::close(body_pipe[0]);
    const int exit_code = wait_for_process(process);
    const auto error_text = read_bounded_file(error_output, 64 * 1024, "curl stderr");
    if (callback_error) std::rethrow_exception(callback_error);
    if (exit_code != 0) {
        throw std::runtime_error(
            "curl failed with exit code " + std::to_string(exit_code) +
            (error_text.empty() ? std::string{} : ": " + error_text));
    }
    const auto header_text = read_bounded_file(
        response_headers, 256 * 1024, "HTTP response headers");
    return {parse_last_http_status(header_text), {}, {}};
#endif
}

}  // namespace agent
