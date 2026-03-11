#include <boost/ut.hpp>
#include <cli/cli.hpp>

auto main() -> int {
  using namespace boost::ut;

  auto make_argv = [](auto &&...args) -> auto {
    return std::vector<const char *>{args...};
  };

  "basic string int flag"_test = [&] -> void {
    constexpr uint8_t def_count = 10;
    cli::Parser       parser("test", "Basic test");
    parser.option<std::string>("--output").alias("-o").help("Output file");
    parser.option<int>("--count").alias("-n").help("Count").default_value(
      def_count);
    parser.flag("--verbose").alias("-v").help("Verbose mode");

    auto argv = make_argv("test", "--output", "file.txt", "-v", "--count=5");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(ret->get<std::string>("--output") == "file.txt");
    expect(ret->get<bool>("--verbose"));
    expect(ret->get<int>("--count") == 5_i);
  };

  "default values"_test = [&] -> void {
    constexpr uint8_t def_count = 42;
    cli::Parser       parser("test");
    parser.option<int>("--count").default_value(def_count);
    parser.flag("--verbose");

    auto argv = make_argv("test");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(ret->get<int>("--count") == 42_i);
    expect(!ret->get<bool>("--verbose"));
  };

  "missing required"_test = [&] -> void {
    cli::Parser parser("test");
    parser.option<std::string>("--output").required();

    auto argv = make_argv("test");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(!ret.has_value());
    expect(ret.error().kind == cli::ParseErrorKind::MissingRequired);
  };

  "unknown option"_test = [&] -> void {
    cli::Parser parser("test");

    auto argv = make_argv("test", "--unknown");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(!ret.has_value());
    expect(ret.error().kind == cli::ParseErrorKind::UnknownOption);
  };

  "positional arguments"_test = [&] -> void {
    cli::Parser parser("test");
    parser.flag("--verbose");

    auto argv = make_argv("test", "input.txt", "--verbose", "output.txt");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    auto pos = ret->positional();
    expect(pos.size() == 2_ul);
    expect(pos[0] == "input.txt");
    expect(pos[1] == "output.txt");
  };

  "-- separator"_test = [&] -> void {
    cli::Parser parser("test");
    parser.flag("--verbose");

    auto argv = make_argv("test", "--", "--not-a-flag", "-x");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    auto pos = ret->positional();
    expect(pos.size() == 2_ul);
    expect(pos[0] == "--not-a-flag");
    expect(pos[1] == "-x");
    expect(!ret->get<bool>("--verbose"));
  };

  "--key=value inline syntax"_test = [&] -> void {
    constexpr double ratio_one = 3.13;
    constexpr double ratio_two = 3.15;
    cli::Parser      parser("test");
    parser.option<std::string>("--file");
    parser.option<double>("--ratio");

    auto argv = make_argv("test", "--file=hello world", "--ratio=3.14");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(ret->get<std::string>("--file") == "hello world");
    expect(ret->get<double>("--ratio") > ratio_one);
    expect(ret->get<double>("--ratio") < ratio_two);
  };

  "missing value for option"_test = [&] -> void {
    cli::Parser parser("test");
    parser.option<std::string>("--output");

    auto argv = make_argv("test", "--output");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(!ret.has_value());
    expect(ret.error().kind == cli::ParseErrorKind::MissingValue);
  };

  "invalid numeric value"_test = [&] -> void {
    cli::Parser parser("test");
    parser.option<int>("--count");

    auto argv = make_argv("test", "--count", "abc");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(!ret.has_value());
    expect(ret.error().kind == cli::ParseErrorKind::InvalidValue);
  };

  "get_optional returns nullopt when absent"_test = [&] -> void {
    cli::Parser parser("test");
    parser.option<std::string>("--output");

    auto argv = make_argv("test");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(!ret->get_optional<std::string>("--output").has_value());
  };

  "print_help smoke test"_test = [&] -> void {
    cli::Parser parser("my-app", "A demo application.");
    parser.option<std::string>("--output")
      .alias("-o")
      .help("Output file")
      .required();
    parser.option<int>("--jobs")
      .alias("-j")
      .help("Parallelism")
      .default_value(4);
    parser.flag("--verbose").alias("-v").help("Verbose output");

    // Must not throw or crash.
    parser.print_help();
    expect(true);
  };

  // ---- subcommand tests -------------------------------------------------------

  "subcommand basic dispatch"_test = [&] -> void {
    cli::Parser parser("app", "Test app");
    parser.flag("--verbose").alias("-v");
    auto &commit = parser.subcommand("commit", "Record changes");
    commit.option<std::string>("--message").alias("-m").required();
    commit.flag("--amend");

    auto argv = make_argv("app", "--verbose", "commit", "--message", "init");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(ret->get<bool>("--verbose"));
    expect(ret->subcommand_name() == std::optional<std::string_view>{"commit"});
    auto *sub = ret->subcommand_result();
    expect(sub != nullptr);
    expect(sub->get<std::string>("--message") == "init");
    expect(!sub->get<bool>("--amend"));
  };

  "subcommand own options independent of global"_test = [&] -> void {
    cli::Parser parser("app");
    auto &push = parser.subcommand("push", "Upload refs");
    push.option<std::string>("--remote").default_value(std::string{"origin"});
    push.flag("--force");

    auto argv = make_argv("app", "push", "--force");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(!ret->subcommand_name().has_value() == false);
    auto *sub = ret->subcommand_result();
    expect(sub != nullptr);
    expect(sub->get<bool>("--force"));
    expect(sub->get<std::string>("--remote") == "origin");
  };

  "subcommand required missing"_test = [&] -> void {
    cli::Parser parser("app");
    parser.subcommand("commit", "Record changes");
    parser.required_subcommand();

    auto argv = make_argv("app", "--");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(!ret.has_value());
    expect(ret.error().kind == cli::ParseErrorKind::MissingSubcommand);
  };

  "no subcommand token leaves result empty"_test = [&] -> void {
    cli::Parser parser("app");
    parser.subcommand("run", "Run something");

    auto argv = make_argv("app", "positional-only");
    auto ret  = parser.parse(static_cast<int>(argv.size()),
                             const_cast<char **>(argv.data()));

    expect(ret.has_value());
    expect(!ret->subcommand_name().has_value());
    expect(ret->subcommand_result() == nullptr);
    expect(ret->positional().size() == 1_ul);
  };

  "subcommand with print_help smoke test"_test = [&] -> void {
    cli::Parser parser("git", "Fake git.");
    auto &commit = parser.subcommand("commit", "Record changes");
    commit.option<std::string>("--message").alias("-m").help("Commit message").required();
    auto &push = parser.subcommand("push", "Upload refs");
    push.flag("--force").help("Force push");

    parser.print_help();
    expect(true);
  };
}
