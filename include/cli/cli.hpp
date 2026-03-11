#pragma once

#include <concepts>
#include <deque>
#include <expected>
#include <format>
#include <functional>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace cli {

// Concepts

template <typename T>
concept Parseable = std::same_as<T, bool> || std::same_as<T, int> ||
                    std::same_as<T, long> || std::same_as<T, double> ||
                    std::same_as<T, float> || std::same_as<T, std::string>;

// Error codes

enum class ParseErrorKind : std::uint8_t {
  UnknownOption,
  MissingValue,
  InvalidValue,
  MissingRequired,
  MissingSubcommand,
};

struct ParseError {
  ParseErrorKind kind;
  std::string    message;

  static auto unknown_option(std::string_view opt) -> ParseError {
    return {.kind    = ParseErrorKind::UnknownOption,
            .message = std::format("unknown option: '{}'", opt)};
  }

  static auto missing_value(std::string_view opt) -> ParseError {
    return {.kind    = ParseErrorKind::MissingValue,
            .message = std::format("option '{}' requires a value", opt)};
  }

  static auto invalid_value(std::string_view opt, std::string_view val)
    -> ParseError {
    return {.kind = ParseErrorKind::InvalidValue,
            .message =
              std::format("invalid value '{}' for option '{}'", val, opt)};
  }

  static auto missing_required(std::string_view opt) -> ParseError {
    return {.kind    = ParseErrorKind::MissingRequired,
            .message = std::format("required option '{}' not provided", opt)};
  }

  static auto missing_subcommand() -> ParseError {
    return {.kind    = ParseErrorKind::MissingSubcommand,
            .message = "a subcommand is required"};
  }
};

// Value storage

using Value = std::variant<bool, int, long, double, float, std::string>;

// Internal option descriptor

struct OptionDesc {
  std::string              name;
  std::vector<std::string> aliases;
  std::string              help;
  std::string              type_name;
  bool                     is_flag  = false;
  bool                     required = false;
  std::optional<Value>     default_value;

  std::function<std::expected<Value, ParseError>(std::string_view key,
                                                 std::string_view raw)>
    parser;
};

// ParseResult

class ParseResult {
public:
  template <Parseable T>
  [[nodiscard]] auto get(std::string_view name) -> T {
    auto it = values_.find(std::string(name));
    if (it == values_.end()) {
      throw std::out_of_range(
        std::format("option '{}' not found in parse result", name));
    }
    return std::get<T>(it->second);
  }

  template <Parseable T>
  [[nodiscard]] auto get_optional(std::string_view name) -> std::optional<T> {
    auto it = values_.find(std::string(name));
    if (it == values_.end()) {
      return std::nullopt;
    }
    return std::get<T>(it->second);
  }

  [[nodiscard]] auto has(std::string_view name) -> bool {
    return values_.contains(std::string(name));
  }

  [[nodiscard]] auto positional() -> std::span<const std::string> {
    return positional_;
  }

  [[nodiscard]] auto subcommand_name() const -> std::optional<std::string_view> {
    if (subcommand_name_) {
      return std::string_view{*subcommand_name_};
    }
    return std::nullopt;
  }

  [[nodiscard]] auto subcommand_result() -> ParseResult * {
    return subcommand_result_.get();
  }

  [[nodiscard]] auto subcommand_result() const -> const ParseResult * {
    return subcommand_result_.get();
  }

private:
  std::unordered_map<std::string, Value> values_;
  std::vector<std::string>               positional_;
  std::optional<std::string>             subcommand_name_;
  std::unique_ptr<ParseResult>           subcommand_result_; // heap: ParseResult is incomplete here

  friend class Parser;
};

// OptionBuilder

// Uses an index into the parser's options deque so it is safe even if the
// deque grows during configuration of the parser.
// Stores Parser* (non-owning, never null) to avoid a reference data member.

class Parser; // forward

template <Parseable T>
class OptionBuilder {
public:
  OptionBuilder(Parser &parser, std::size_t idx) :
    parser_(&parser), idx_(idx) {}

  auto alias(std::string ali) -> OptionBuilder &;
  auto help(std::string hel) -> OptionBuilder &;
  auto required() -> OptionBuilder &;

  // Default values are not allowed for flags (bool without explicit value).
  auto default_value(T val) -> OptionBuilder &
  requires(!std::same_as<T, bool>)
  {
    desc().default_value = Value{val};
    return *this;
  }

private:
  Parser     *parser_; // non-owning, never null
  std::size_t idx_;

  auto desc() -> OptionDesc &;
};

// Parser

class Parser {
public:
  explicit Parser(std::string name, std::string description = "") :
    name_(std::move(name)), description_(std::move(description)) {}

  // Defined out-of-line (below) so that Parser is complete when
  // unique_ptr<Parser> destructor is instantiated.
  ~Parser();

  // Add a typed option that consumes the next token as its value.
  template <Parseable T>
  auto option(std::string name) -> OptionBuilder<T> {
    auto &desc     = options_.emplace_back();
    desc.name      = std::move(name);
    desc.is_flag   = false;
    desc.type_name = type_name_for<T>();
    desc.parser    = make_value_parser<T>();
    return {*this, options_.size() - 1};
  }

  // Register a subcommand. Returns a reference to the sub-parser for
  // option configuration. The reference stays valid for the lifetime of
  // this Parser (subparsers_ is a deque — stable on push_back).
  auto subcommand(std::string name, std::string description = "") -> Parser & {
    subparsers_.push_back(
      std::make_unique<Parser>(std::move(name), std::move(description)));
    return *subparsers_.back();
  }

  // Mark a subcommand as required. parse() returns MissingSubcommand if no
  // subcommand token is found.
  auto required_subcommand() -> Parser & {
    subcommand_required_ = true;
    return *this;
  }

  // Add a boolean flag that takes no value (present → true).
  auto flag(std::string name) -> OptionBuilder<bool> {
    auto &desc         = options_.emplace_back();
    desc.name          = std::move(name);
    desc.is_flag       = true;
    desc.type_name     = "flag";
    desc.default_value = Value{false};
    desc.parser = [](std::string_view,
                     std::string_view) -> std::expected<Value, ParseError> {
      return Value{true};
    };
    return {*this, options_.size() - 1};
  }

  // Primary parse: span of args not including argv[0] (the program name).
  [[nodiscard]] auto parse(std::span<char *const> args)
    -> std::expected<ParseResult, ParseError> {
    ParseResult result;

    // Seed with defaults.
    for (const auto &opt : options_) {
      if (opt.default_value) {
        result.values_[opt.name] = *opt.default_value;
      }
    }

    bool past_separator = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
      std::string_view arg{args[i]};

      // Positional: anything after "--", bare "-", or non-flag token.
      if (past_separator || arg == "-" || !arg.starts_with('-')) {
        // Before collecting as positional, check if it is a subcommand name.
        if (!past_separator) {
          if (Parser *sub = find_subcommand(arg)) {
            auto sub_res = sub->parse(args.subspan(i + 1));
            if (!sub_res) return std::unexpected(sub_res.error());
            result.subcommand_name_   = std::string{arg};
            result.subcommand_result_ = std::make_unique<ParseResult>(std::move(*sub_res));
            break;
          }
        }
        result.positional_.emplace_back(arg);
        continue;
      }

      if (arg == "--") {
        past_separator = true;
        continue;
      }

      // Split --key=value if present.
      std::string_view                key = arg;
      std::optional<std::string_view> inline_val;

      if (auto eq = arg.find('='); eq != std::string_view::npos) {
        key        = arg.substr(0, eq);
        inline_val = arg.substr(eq + 1);
      }

      const OptionDesc *opt = find_option(key);
      if (!opt) {
        return std::unexpected(ParseError::unknown_option(key));
      }

      if (opt->is_flag) {
        result.values_[opt->name] = Value{true};
        continue;
      }

      // Consume value.
      std::string_view val;
      if (inline_val) {
        val = *inline_val;
      } else if (i + 1 < args.size()) {
        val = std::string_view{args[++i]};
      } else {
        return std::unexpected(ParseError::missing_value(key));
      }

      auto parsed = opt->parser(key, val);
      if (!parsed) {
        return std::unexpected(parsed.error());
      }
      result.values_[opt->name] = *parsed;
    }

    // Validate required options.
    for (const auto &opt : options_) {
      if (opt.required && !result.values_.contains(opt.name)) {
        return std::unexpected(ParseError::missing_required(opt.name));
      }
    }

    // Validate required subcommand.
    if (subcommand_required_ && !result.subcommand_name_) {
      return std::unexpected(ParseError::missing_subcommand());
    }

    return result;
  }

  // Convenience overload: wraps main's argc/argv, skipping argv[0].
  [[nodiscard]] auto parse(int argc, char *argv[])
    -> std::expected<ParseResult, ParseError> {
    const auto all = std::span{argv, static_cast<std::size_t>(argc)};
    return parse(all.subspan(argc > 0 ? 1 : 0));
  }

  // Parse and exit with an error message on failure.
  [[nodiscard]] auto parse_or_exit(int argc, char *argv[]) -> ParseResult {
    auto result = parse(argc, argv);
    if (!result) {
      std::println(stderr, "error: {}", result.error().message);
      std::println(stderr, "Try '{} --help' for usage.", name_);
      std::exit(1);
    }
    return std::move(*result);
  }

  void print_help() const {
    if (!subparsers_.empty()) {
      std::println("Usage: {} [options] <subcommand> [subcommand options]", name_);
    } else {
      std::println("Usage: {} [options]", name_);
    }
    if (!description_.empty()) {
      std::println("\n{}", description_);
    }
    if (!subparsers_.empty()) {
      std::println("\nSubcommands:");
      constexpr std::size_t col = 30;
      for (const auto &sub : subparsers_) {
        std::string lhs = "  " + sub->name_;
        if (lhs.size() < col) {
          lhs.append(col - lhs.size(), ' ');
        } else {
          lhs += "\n" + std::string(col, ' ');
        }
        std::println("{}{}", lhs, sub->description_);
      }
    }
    std::println("\nOptions:");

    for (const auto &opt : options_) {
      // Build "  --name, -n <type>"
      std::string lhs = "  " + opt.name;
      for (const auto &a : opt.aliases) {
        lhs += ", " + a;
      }
      if (!opt.is_flag) {
        lhs += std::format(" <{}>", opt.type_name);
      }

      // Align help text at column 30.
      constexpr std::size_t col = 30;
      if (lhs.size() < col) {
        lhs.append(col - lhs.size(), ' ');
      } else {
        lhs += "\n" + std::string(col, ' ');
      }

      std::string rhs = opt.help;
      if (opt.default_value && !opt.is_flag) {
        rhs +=
          std::format(" [default: {}]", value_to_string(*opt.default_value));
      }
      if (opt.required) {
        rhs += " (required)";
      }

      std::println("{}{}", lhs, rhs);
    }
  }

private:
  std::string            name_;
  std::string            description_;
  std::deque<OptionDesc>                options_;    // deque: stable references
  std::vector<std::unique_ptr<Parser>> subparsers_; // heap: Parser is incomplete here
  bool                                 subcommand_required_ = false;

  template <Parseable T>
  friend class OptionBuilder;

  [[nodiscard]] auto find_subcommand(std::string_view name) -> Parser * {
    for (const auto &sub : subparsers_) {
      if (sub->name_ == name) return sub.get();
    }
    return nullptr;
  }

  [[nodiscard]] auto find_option(std::string_view key) -> const OptionDesc * {
    for (const auto &opt : options_) {
      if (opt.name == key) {
        return &opt;
      }
      for (const auto &a : opt.aliases) {
        if (a == key) {
          return &opt;
        }
      }
    }
    return nullptr;
  }

  template <Parseable T>
  static auto type_name_for() -> std::string {
    if constexpr (std::same_as<T, bool>) {
      return "bool";
    } else if constexpr (std::same_as<T, int>) {
      return "int";
    } else if constexpr (std::same_as<T, long>) {
      return "long";
    } else if constexpr (std::same_as<T, double>) {
      return "double";
    } else if constexpr (std::same_as<T, float>) {
      return "float";
    } else {
      return "string";
    }
  }

  template <Parseable T>
  static auto make_value_parser() {
    return [](std::string_view key,
              std::string_view val) -> std::expected<Value, ParseError> {
      if constexpr (std::same_as<T, std::string>) {
        return Value{std::string(val)};
      }
      if constexpr (std::same_as<T, bool>) {
        if (val == "true" || val == "1" || val == "yes") {
          return Value{true};
        }
        if (val == "false" || val == "0" || val == "no") {
          return Value{false};
        }
        return std::unexpected(ParseError::invalid_value(key, val));
      }
      try {
        std::size_t pos{};
        std::string str{val};
        if constexpr (std::same_as<T, int>) {
          int out = std::stoi(str, &pos);
          if (pos != val.size()) {
            return std::unexpected(ParseError::invalid_value(key, val));
          }
          return Value{out};
        } else if constexpr (std::same_as<T, long>) {
          long out = std::stol(str, &pos);
          if (pos != val.size()) {
            return std::unexpected(ParseError::invalid_value(key, val));
          }
          return Value{out};
        } else if constexpr (std::same_as<T, float>) {
          float out = std::stof(str, &pos);
          if (pos != val.size()) {
            return std::unexpected(ParseError::invalid_value(key, val));
          }
          return Value{out};
        } else if constexpr (std::same_as<T, double>) {
          double out = std::stod(str, &pos);
          if (pos != val.size()) {
            return std::unexpected(ParseError::invalid_value(key, val));
          }
          return Value{out};
        }
      } catch (...) {
        return std::unexpected(ParseError::invalid_value(key, val));
      }
      return std::unexpected(ParseError::invalid_value(key, val));
    };
  }

  static auto value_to_string(const Value &vpar) -> std::string {
    return std::visit(
      []<typename T>(const T &val) -> std::string {
        if constexpr (std::same_as<T, std::string>) {
          return val;
        } else if constexpr (std::same_as<T, bool>) {
          return val ? "true" : "false";
        } else {
          return std::to_string(val);
        }
      },
      vpar);
  }
};

// Parser destructor defined here so unique_ptr<Parser> can see a complete type.
inline Parser::~Parser() = default;

// ---- OptionBuilder method bodies (need Parser definition) -------------------

template <Parseable T>
auto OptionBuilder<T>::desc() -> OptionDesc & {
  return parser_->options_[idx_];
}

template <Parseable T>
auto OptionBuilder<T>::alias(std::string ali) -> OptionBuilder<T> & {
  desc().aliases.push_back(std::move(ali));
  return *this;
}

template <Parseable T>
auto OptionBuilder<T>::help(std::string hel) -> OptionBuilder<T> & {
  desc().help = std::move(hel);
  return *this;
}

template <Parseable T>
auto OptionBuilder<T>::required() -> OptionBuilder<T> & {
  desc().required = true;
  return *this;
}

} // namespace cli
