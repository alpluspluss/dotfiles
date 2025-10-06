/* this project is licensed under the MIT license. see `LICENSE.txt` for more details */

#include <print>
#include <expected>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <span>
#include <system_error>
#include <fstream>
#include <optional>
#include <archive.h>
#include <archive_entry.h>

namespace fs = std::filesystem;

struct DesktopEntryConfig
{
    std::string name;
    std::string exec_path;
    std::string icon;
    std::string comment;
    std::string categories;
    bool terminal = false;
};

struct Config
{
    fs::path archive_file;
    fs::path install_dir = "/opt";
    fs::path bin_dir = "/usr/local/bin";
    std::string app_name;
    std::vector<std::string> link_binaries;
    bool no_link = false;
    bool force = false;
    bool create_desktop = false;
    std::optional<DesktopEntryConfig> desktop_config;
};

enum class ArchiveFormat
{
    TAR,
    TAR_GZ,
    TAR_BZ2,
    TAR_XZ,
    ZIP,
    DEB,
    RPM,
    UNKNOWN
};

template<typename... Args>
void error(std::format_string<Args...> fmt, Args&&... args)
{
    std::print(stderr, "\033[0;31m");
    std::println(stderr, fmt, std::forward<Args>(args)...);
    std::print(stderr, "\033[0m");
}

template<typename... Args>
void warn(std::format_string<Args...> fmt, Args&&... args)
{
    std::print(stderr, "\033[0;33m");
    std::println(stderr, fmt, std::forward<Args>(args)...);
    std::print(stderr, "\033[0m");
}

template<typename... Args>
void info(std::format_string<Args...> fmt, Args&&... args)
{
    std::print("\033[0;36m");
    std::println(fmt, std::forward<Args>(args)...);
    std::print("\033[0m");
}

std::expected<void, std::string> extract(const fs::path &archive_path, const fs::path &dest_path, ArchiveFormat format)
{
    archive *a = archive_read_new();
    archive *ext = archive_write_disk_new();

    if (!a || !ext)
        return std::unexpected("Failed to create archive objects");

    archive_write_disk_set_options(
        ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);

    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    auto r = archive_read_open_filename(a, archive_path.c_str(), 10240);
    if (r != ARCHIVE_OK)
    {
        auto err = std::format("Failed to open archive because: {}", archive_error_string(a));
        archive_read_free(a);
        archive_write_free(ext);
        return std::unexpected(err);
    }

    archive_entry *entry = {};
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK)
    {
        const char *current_file = archive_entry_pathname(entry);
        auto full_path = dest_path / current_file;
        archive_entry_set_pathname(entry, full_path.c_str());

        r = archive_write_header(ext, entry);
        if (r != ARCHIVE_OK)
        {
            warn("Archive write header: {}", archive_error_string(ext));
        }
        else
        {
            const void *buff;
            size_t size;
            la_int64_t offset;

            while (archive_read_data_block(a, &buff, &size, &offset) == ARCHIVE_OK)
            {
                if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK)
                    warn("Archive write data block: {}", archive_error_string(ext));
            }
        }

        archive_write_finish_entry(ext);
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return {};
}

ArchiveFormat detect_format(const fs::path &path)
{
    auto ext = path.extension().string();
    auto filename = path.filename().string();

    if (filename.ends_with(".tar.gz") || filename.ends_with(".tgz"))
        return ArchiveFormat::TAR_GZ;
    if (filename.ends_with(".tar.bz2") || filename.ends_with(".tbz2"))
        return ArchiveFormat::TAR_BZ2;
    if (filename.ends_with(".tar.xz") || filename.ends_with(".txz"))
        return ArchiveFormat::TAR_XZ;
    if (ext == ".tar")
        return ArchiveFormat::TAR;
    if (ext == ".zip")
        return ArchiveFormat::ZIP;
    if (ext == ".deb")
        return ArchiveFormat::DEB;
    if (ext == ".rpm")
        return ArchiveFormat::RPM;

    return ArchiveFormat::UNKNOWN;
}

std::string detect_app_name(const fs::path &archive_file)
{
    auto name = archive_file.stem().string();

    if (name.ends_with(".tar"))
        name = name.substr(0, name.length() - 4);

    auto dash_pos = name.find_last_of('-');
    if (dash_pos != std::string::npos)
    {
        auto version_part = name.substr(dash_pos + 1);
        if (!version_part.empty() && std::isdigit(version_part[0]))
            name = name.substr(0, dash_pos);
    }

    return name;
}

bool is_valid_executable(const fs::path &path)
{
    static const std::vector<std::string> excluded_extensions = {
        ".so", ".a", ".o", ".la", ".dylib", ".dll",
        ".sh", ".bash", ".zsh", ".fish", ".py", ".pl", ".rb",
        ".txt", ".md", ".xml", ".json", ".conf", ".cfg"
    };

    auto filename = path.filename().string();
    auto extension = path.extension().string();

    for (const auto &ext : excluded_extensions)
    {
        if (extension == ext)
            return false;
    }

    if (filename.starts_with("."))
        return false;

    return true;
}

std::vector<fs::path> find_executables(const fs::path &dir, size_t max_results = 20)
{
    std::vector<fs::path> executables;

    try
    {
        for (const auto &entry : fs::recursive_directory_iterator(dir))
        {
            if (entry.is_regular_file())
            {
                auto perms = entry.status().permissions();
                if ((perms & fs::perms::owner_exec) == fs::perms::owner_exec)
                {
                    if (is_valid_executable(entry.path()))
                    {
                        executables.push_back(entry.path());
                        if (executables.size() >= max_results)
                            break;
                    }
                }
            }
        }
    }
    catch (const fs::filesystem_error &e)
    {
        warn("Filesystem error: {}", e.what());
    }

    return executables;
}

std::optional<fs::path> find_icon(const fs::path &install_dir, const std::string &app_name)
{
    std::vector<std::string> icon_patterns = {
        "bin/" + app_name + ".svg",
        "bin/" + app_name + ".png",
        "share/icons/" + app_name + ".svg",
        "share/icons/" + app_name + ".png",
        "share/pixmaps/" + app_name + ".svg",
        "share/pixmaps/" + app_name + ".png",
        "icon.svg",
        "icon.png",
        app_name + ".svg",
        app_name + ".png"
    };

    for (const auto &pattern : icon_patterns)
    {
        auto icon_path = install_dir / pattern;
        if (fs::exists(icon_path))
            return icon_path;
    }

    return std::nullopt;
}

void create_desktop_entry(const DesktopEntryConfig &config)
{
    auto home = std::getenv("HOME");
    if (!home)
    {
        warn("Could not determine HOME directory");
        return;
    }

    auto desktop_dir = fs::path(home) / ".local" / "share" / "applications";
    fs::create_directories(desktop_dir);

    auto desktop_file = desktop_dir / (config.name + ".desktop");

    std::ofstream file(desktop_file);
    if (!file)
    {
        warn("Could not create desktop entry: {}", desktop_file.string());
        return;
    }

    file << "[Desktop Entry]\n";
    file << "Version=1.0\n";
    file << "Type=Application\n";
    file << "Name=" << config.name << "\n";

    if (!config.icon.empty())
        file << "Icon=" << config.icon << "\n";

    file << "Exec=" << config.exec_path << " %f\n";

    if (!config.comment.empty())
        file << "Comment=" << config.comment << "\n";

    if (!config.categories.empty())
        file << "Categories=" << config.categories << "\n";
    else
        file << "Categories=Application;\n";

    file << "Terminal=" << (config.terminal ? "true" : "false") << "\n";
    file << "StartupNotify=true\n";

    file.close();

    fs::permissions(desktop_file, fs::perms::owner_read | fs::perms::owner_write);

    info("Created desktop entry: {}", desktop_file.string());
}

void create_symlink(const fs::path &target, const fs::path &link)
{
    std::error_code ec;

    if (fs::exists(link))
    {
        fs::remove(link, ec);
        if (ec)
        {
            warn("Could not remove existing symlink {}: {}", link.string(), ec.message());
            return;
        }
    }

    fs::create_symlink(target, link, ec);
    if (ec)
    {
        warn("Could not create symlink {} -> {}: {}", link.string(), target.string(), ec.message());
    }
    else
    {
        info("Created symlink: {} -> {}", link.string(), target.string());
    }
}

void print_usage(std::string_view program_name)
{
    info("Usage: {} <options> <archive-file>\n", program_name);
    std::println("Install applications from various archive formats.\n");
    info("Available options:");
    std::println("    -d, --dir <path>       Installation directory. Default: /opt");
    std::println("    -b, --bin <path>       Binary symlink directory. Default: /usr/local/bin");
    std::println("    -n, --name <n>         Application name. Auto-detected if not specified");
    std::println("    -l, --link <binary>    Binary to symlink. Separate comma for multiple install");
    std::println("    --no-link              Don't create any symlinks");
    std::println("    -f, --force            Overwrite existing installation without prompting");
    std::println("    --desktop              Create desktop entry");
    std::println("    --icon <path>          Icon path for desktop entry");
    std::println("    --comment <text>       Comment for desktop entry");
    std::println("    --categories <cats>    Categories for desktop entry (e.g., Development;IDE;)");
    std::println("    --terminal             Mark desktop entry as terminal application");
    std::println("    -h, --help             Show this help message");
    std::println("    -v, --version          Show version\n");
    info("Supported formats:");
    std::println("    .tar, .tar.gz, .tgz, .tar.bz2, .tar.xz, .zip, .deb, .rpm\n");
    info("Examples:");
    std::println("    {} app-1.0.tar.gz", program_name);
    std::println("    {} -d /usr/local -n myapp app.tar.gz", program_name);
    std::println("    {} -l bin/app,bin/app-cli app.zip", program_name);
    std::println("    {} --desktop --categories \"Development;IDE;\" clion.tar.gz", program_name);
}

std::expected<Config, std::string> parse_args(std::span<char *> args)
{
    Config config;

    if (args.size() < 2)
        return std::unexpected("No archive file specified");

    for (size_t i = 1; i < args.size(); ++i)
    {
        std::string_view arg = args[i];

        if (arg == "-h" || arg == "--help")
        {
            print_usage(args[0]);
            std::exit(0);
        }
        else if (arg == "-v" || arg == "--version")
        {
            std::println("install-app v1.0.0");
            std::exit(0);
        }
        else if (arg == "-d" || arg == "--dir")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --dir");
            config.install_dir = args[++i];
        }
        else if (arg == "-b" || arg == "--bin")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --bin");
            config.bin_dir = args[++i];
        }
        else if (arg == "-n" || arg == "--name")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --name");
            config.app_name = args[++i];
        }
        else if (arg == "-l" || arg == "--link")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --link");
            std::string links = args[++i];
            size_t start = 0;
            while (start < links.size())
            {
                auto pos = links.find(',', start);
                if (pos == std::string::npos)
                {
                    config.link_binaries.push_back(links.substr(start));
                    break;
                }
                config.link_binaries.push_back(links.substr(start, pos - start));
                start = pos + 1;
            }
        }
        else if (arg == "--no-link")
        {
            config.no_link = true;
        }
        else if (arg == "-f" || arg == "--force")
        {
            config.force = true;
        }
        else if (arg == "--desktop")
        {
            config.create_desktop = true;
            if (!config.desktop_config)
                config.desktop_config = DesktopEntryConfig{};
        }
        else if (arg == "--icon")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --icon");
            if (!config.desktop_config)
                config.desktop_config = DesktopEntryConfig{};
            config.desktop_config->icon = args[++i];
        }
        else if (arg == "--comment")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --comment");
            if (!config.desktop_config)
                config.desktop_config = DesktopEntryConfig{};
            config.desktop_config->comment = args[++i];
        }
        else if (arg == "--categories")
        {
            if (i + 1 >= args.size())
                return std::unexpected("Missing argument for --categories");
            if (!config.desktop_config)
                config.desktop_config = DesktopEntryConfig{};
            config.desktop_config->categories = args[++i];
        }
        else if (arg == "--terminal")
        {
            if (!config.desktop_config)
                config.desktop_config = DesktopEntryConfig{};
            config.desktop_config->terminal = true;
        }
        else if (arg[0] == '-')
        {
            return std::unexpected(std::format("Unknown option: {}", arg));
        }
        else
        {
            config.archive_file = arg;
        }
    }

    if (config.archive_file.empty())
        return std::unexpected("No archive file specified");

    if (!fs::exists(config.archive_file))
        return std::unexpected(std::format("File not found: {}", config.archive_file.string()));

    if (config.app_name.empty())
        config.app_name = detect_app_name(config.archive_file);

    return config;
}

int main(int argc, char *argv[])
{
    auto config_result = parse_args(std::span(argv, argc));

    if (!config_result)
    {
        error("{}", config_result.error());
        print_usage(argv[0]);
        return 1;
    }

    auto config = *config_result;

    auto format = detect_format(config.archive_file);
    if (format == ArchiveFormat::UNKNOWN)
    {
        error("Unable to detect archive format for: {}", config.archive_file.string());
        return 1;
    }

    info("Detected app name: {}", config.app_name);

    auto temp_dir = fs::temp_directory_path() / std::format("install-app-{}", getpid());
    fs::create_directories(temp_dir);

    info("Extracting archive...");
    auto extract_result = extract(config.archive_file, temp_dir, format);

    if (!extract_result)
    {
        error("{}", extract_result.error());
        fs::remove_all(temp_dir);
        return 1;
    }

    fs::path source_dir = temp_dir;
    size_t entry_count = 0;
    fs::path first_dir;

    for (const auto &entry : fs::directory_iterator(temp_dir))
    {
        entry_count++;
        if (entry_count == 1 && entry.is_directory())
            first_dir = entry.path();
    }

    if (entry_count == 1 && !first_dir.empty())
        source_dir = first_dir;

    auto final_install_path = config.install_dir / config.app_name;

    if (fs::exists(final_install_path))
    {
        if (!config.force)
        {
            std::print("Installation directory already exists: {}\noverwrite? (y/N): ", final_install_path.string());
            std::string response;
            std::getline(std::cin, response);

            if (response.empty() || (response[0] != 'y' && response[0] != 'Y'))
            {
                std::println("Installation cancelled");
                fs::remove_all(temp_dir);
                return 0;
            }
        }
        fs::remove_all(final_install_path);
    }

    info("Installing to: {}", final_install_path.string());
    fs::create_directories(final_install_path.parent_path());
    fs::copy(source_dir, final_install_path, fs::copy_options::recursive);

    fs::path primary_executable;

    if (!config.no_link)
    {
        fs::create_directories(config.bin_dir);

        if (!config.link_binaries.empty())
        {
            for (const auto &binary : config.link_binaries)
            {
                auto binary_path = final_install_path / binary;
                auto binary_name = fs::path(binary).filename();

                if (fs::exists(binary_path) && fs::is_regular_file(binary_path))
                {
                    ::create_symlink(binary_path, config.bin_dir / binary_name);
                    if (primary_executable.empty())
                        primary_executable = binary_path;
                }
                else
                {
                    warn("Binary not found: {}", binary_path.string());
                }
            }
        }
        else
        {
            info("Searching for executables...");
            auto executables = find_executables(final_install_path);

            if (!executables.empty())
            {
                std::println("Found executables:");
                for (size_t i = 0; i < executables.size(); ++i)
                {
                    auto rel_path = fs::relative(executables[i], final_install_path);
                    std::println("  {}: {}", i + 1, rel_path.string());
                }

                std::print("Create symlinks for these binaries? (y/N): ");
                std::string response;
                std::getline(std::cin, response);

                if (!response.empty() && (response[0] == 'y' || response[0] == 'Y'))
                {
                    for (const auto &exe : executables)
                    {
                        ::create_symlink(exe, config.bin_dir / exe.filename());
                        if (primary_executable.empty())
                            primary_executable = exe;
                    }
                }
            }
        }
    }

    if (config.create_desktop && config.desktop_config)
    {
        auto &desktop_cfg = *config.desktop_config;

        if (desktop_cfg.name.empty())
            desktop_cfg.name = config.app_name;

        if (desktop_cfg.exec_path.empty())
        {
            if (!primary_executable.empty())
            {
                desktop_cfg.exec_path = primary_executable.string();
            }
            else
            {
                auto executables = find_executables(final_install_path, 1);
                if (!executables.empty())
                    desktop_cfg.exec_path = executables[0].string();
                else
                {
                    warn("No executable found for desktop entry");
                    fs::remove_all(temp_dir);
                    std::println("\nInstallation complete!");
                    std::println("Application installed to: {}", final_install_path.string());
                    return 0;
                }
            }
        }

        if (desktop_cfg.icon.empty())
        {
            auto found_icon = find_icon(final_install_path, config.app_name);
            if (found_icon)
                desktop_cfg.icon = found_icon->string();
        }

        create_desktop_entry(desktop_cfg);
    }

    fs::remove_all(temp_dir);

    std::println("\nInstallation complete!");
    std::println("Application installed to: {}", final_install_path.string());

    return 0;
}
