#include <iostream>
#include "algo/Mod.h"

enum class InputType { ExCSV, InExCSV, InCSV, PLC, TRC };

InputType parse_format(std::string_view s) {
    if (s == "ex-csv")    return InputType::ExCSV;
    if (s == "in-ex-csv") return InputType::InExCSV;
    if (s == "in-csv")    return InputType::InCSV;
    if (s == "plc")       return InputType::PLC;
    if (s == "trc")       return InputType::TRC;
    throw std::invalid_argument("unknown format: '" + std::string(s) + "'");
}

struct Args {
    std::filesystem::path input;
    InputType             format;
    double                max_frag  = 1.0;
    std::size_t           start     = 0;
    std::uint32_t         max_lives = 1;
};

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [OPTIONS] <FORMAT>\n"
        << "\n"
        << "A golden standard for dynamic storage allocation\n"
        << "\n"
        << "Positional:\n"
        << "  <FORMAT>              One of: ex-csv, in-ex-csv, in-csv\n"
        << "\n"
        << "Options:\n"
        << "  -i, --input <PATH>    Path to input\n"
        << "  -f, --max-frag <F>    Maximum fragmentation allowed (default 1.0)\n"
        << "  -s, --start <N>       Start address (default 0)\n"
        << "  -l, --max-lives <N>   Max tries to beat bootstrap heuristic (default 1)\n"
        << "  -h, --help            Print this help\n";
}

Args parse_args(int argc, char** argv) {
    Args args;
    bool have_input  = false;
    bool have_format = false;

    int i = 1;
    while (i < argc) {
        std::string a = argv[i++];

        auto take = [&](const char* name) -> std::string {
            if (i >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[i++];
        };

        if (a == "-i" || a == "--input") {
            args.input = take("--input");
            have_input = true;
        } else if (a == "-f" || a == "--max-frag") {
            args.max_frag = std::stod(take("--max-frag"));
        } else if (a == "-s" || a == "--start") {
            args.start = static_cast<std::size_t>(std::stoull(take("--start")));
        } else if (a == "-l" || a == "--max-lives") {
            args.max_lives = static_cast<std::uint32_t>(std::stoul(take("--max-lives")));
        } else if (a == "-h" || a == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (!a.empty() && a[0] == '-') {
            throw std::runtime_error("unknown option: " + a);
        } else {
            if (have_format) {
                throw std::runtime_error("unexpected positional argument: " + a);
            }
            args.format = parse_format(a);
            have_format = true;
        }
    }

    if (!have_input)  throw std::runtime_error("missing required option --input");
    if (!have_format) throw std::runtime_error("missing required positional argument <format>");

    return args;
}

int main(int argc, char** argv) {
    try {
        Args cli = parse_args(argc, argv);

        if (!std::filesystem::exists(cli.input) ||
            !std::filesystem::is_regular_file(cli.input)) {
            throw std::runtime_error("Invalid input path: " + cli.input.string());
        }
        if (cli.max_frag < 1.0) {
            throw std::runtime_error("Maximum fragmentation must be at least 1.0");
        }

        JobSet set;
        switch (cli.format) {
            case InputType::ExCSV:
                set = read_from_path<MinimallocCSVParser>(cli.input, 1);
                break;
            case InputType::InExCSV:
                set = read_from_path<IREECSVParser>(cli.input, 1);
                break;
            case InputType::InCSV:
                set = read_from_path<IREECSVParser>(cli.input, 2);
                break;
            case InputType::PLC:
                throw std::runtime_error("PLC parser not implemented in this port");
            case InputType::TRC:
                throw std::runtime_error(
                    "TRC files must first be fed to the `adapt` binary!");
        }

        idealloc(std::move(set), cli.max_frag, cli.start, cli.max_lives);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}