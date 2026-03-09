#include <cli/cli.hpp>
#include <boost/ut.hpp>

int main() {
    using namespace boost::ut;

    // ---- helpers ------------------------------------------------------------

    auto make_argv = [](auto&&... args) {
        return std::vector<const char*>{args...};
    };

    // ---- suite --------------------------------------------------------------

    "basic string int flag"_test = [&] {
        cli::Parser parser("test", "Basic test");
        parser.option<std::string>("--output").alias("-o").help("Output file");
        parser.option<int>("--count").alias("-n").help("Count").default_value(10);
        parser.flag("--verbose").alias("-v").help("Verbose mode");

        auto argv = make_argv("test", "--output", "file.txt", "-v", "--count=5");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(r.has_value());
        expect(r->get<std::string>("--output") == "file.txt");
        expect(r->get<bool>("--verbose") == true);
        expect(r->get<int>("--count") == 5_i);
    };

    "default values"_test = [&] {
        cli::Parser parser("test");
        parser.option<int>("--count").default_value(42);
        parser.flag("--verbose");

        auto argv = make_argv("test");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(r.has_value());
        expect(r->get<int>("--count") == 42_i);
        expect(r->get<bool>("--verbose") == false);
    };

    "missing required"_test = [&] {
        cli::Parser parser("test");
        parser.option<std::string>("--output").required();

        auto argv = make_argv("test");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(!r.has_value());
        expect(r.error().kind == cli::ParseErrorKind::MissingRequired);
    };

    "unknown option"_test = [&] {
        cli::Parser parser("test");

        auto argv = make_argv("test", "--unknown");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(!r.has_value());
        expect(r.error().kind == cli::ParseErrorKind::UnknownOption);
    };

    "positional arguments"_test = [&] {
        cli::Parser parser("test");
        parser.flag("--verbose");

        auto argv = make_argv("test", "input.txt", "--verbose", "output.txt");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(r.has_value());
        auto pos = r->positional();
        expect(pos.size() == 2_ul);
        expect(pos[0] == "input.txt");
        expect(pos[1] == "output.txt");
    };

    "-- separator"_test = [&] {
        cli::Parser parser("test");
        parser.flag("--verbose");

        auto argv = make_argv("test", "--", "--not-a-flag", "-x");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(r.has_value());
        auto pos = r->positional();
        expect(pos.size() == 2_ul);
        expect(pos[0] == "--not-a-flag");
        expect(pos[1] == "-x");
        expect(r->get<bool>("--verbose") == false);
    };

    "--key=value inline syntax"_test = [&] {
        cli::Parser parser("test");
        parser.option<std::string>("--file");
        parser.option<double>("--ratio");

        auto argv = make_argv("test", "--file=hello world", "--ratio=3.14");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(r.has_value());
        expect(r->get<std::string>("--file") == "hello world");
        expect(r->get<double>("--ratio") > 3.13);
        expect(r->get<double>("--ratio") < 3.15);
    };

    "missing value for option"_test = [&] {
        cli::Parser parser("test");
        parser.option<std::string>("--output");

        auto argv = make_argv("test", "--output");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(!r.has_value());
        expect(r.error().kind == cli::ParseErrorKind::MissingValue);
    };

    "invalid numeric value"_test = [&] {
        cli::Parser parser("test");
        parser.option<int>("--count");

        auto argv = make_argv("test", "--count", "abc");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(!r.has_value());
        expect(r.error().kind == cli::ParseErrorKind::InvalidValue);
    };

    "get_optional returns nullopt when absent"_test = [&] {
        cli::Parser parser("test");
        parser.option<std::string>("--output");

        auto argv = make_argv("test");
        auto r = parser.parse(static_cast<int>(argv.size()),
                              const_cast<char**>(argv.data()));

        expect(r.has_value());
        expect(!r->get_optional<std::string>("--output").has_value());
    };

    "print_help smoke test"_test = [&] {
        cli::Parser parser("my-app", "A demo application.");
        parser.option<std::string>("--output").alias("-o").help("Output file").required();
        parser.option<int>("--jobs").alias("-j").help("Parallelism").default_value(4);
        parser.flag("--verbose").alias("-v").help("Verbose output");

        // Must not throw or crash.
        parser.print_help();
        expect(true);
    };
}
