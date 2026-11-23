#include "core/exposition_check.hpp"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace vxexp {

bool ExpositionCheck::hasFamily(const std::string& name) const {
  return std::find(families.begin(), families.end(), name) != families.end();
}

namespace {

constexpr std::size_t kCommentPrefixLen = 7;  // "# HELP " / "# TYPE "

bool isNameStart(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}

bool isNameChar(char c) noexcept {
  return isNameStart(c) || (c >= '0' && c <= '9');
}

// Reads a metric or label name at pos; returns false if none is there.
bool readName(const std::string& line, std::size_t& pos, std::string& name, bool allow_colon) {
  const std::size_t start = pos;
  if (pos >= line.size() || !isNameStart(line[pos]) || (!allow_colon && line[pos] == ':')) {
    return false;
  }
  ++pos;
  while (pos < line.size() && isNameChar(line[pos]) && (allow_colon || line[pos] != ':')) {
    ++pos;
  }
  name = line.substr(start, pos - start);
  return true;
}

bool parseValue(const std::string& text, double& value) {
  if (text == "NaN" || text == "+Inf" || text == "-Inf") {
    value = 0.0;
    return true;
  }
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  value = std::strtod(text.c_str(), &end);
  return end == text.c_str() + text.size();
}

// Strips the counter/histogram suffixes a sample may add to its family name.
std::string familyOf(const std::string& sample, const std::set<std::string>& declared) {
  if (declared.count(sample) != 0) {
    return sample;
  }
  static const char* const suffixes[] = {"_total", "_bucket", "_sum", "_count", "_created"};
  for (const char* suffix : suffixes) {
    const std::string s(suffix);
    if (sample.size() > s.size() && sample.compare(sample.size() - s.size(), s.size(), s) == 0) {
      std::string base = sample.substr(0, sample.size() - s.size());
      if (declared.count(base) != 0) {
        return base;
      }
    }
  }
  return sample;
}

struct Checker {
  ExpositionCheck result;
  std::set<std::string> declared;
  std::set<std::string> closed;
  std::string current;

  bool fail(std::size_t line, const std::string& message) {
    result.ok = false;
    result.line = line;
    result.error = message;
    return false;
  }

  void enter(const std::string& family) {
    if (family != current) {
      if (!current.empty()) {
        closed.insert(current);
      }
      current = family;
    }
  }

  bool comment(const std::string& line, std::size_t n) {
    if (line.compare(0, kCommentPrefixLen, "# HELP ") != 0 &&
        line.compare(0, kCommentPrefixLen, "# TYPE ") != 0) {
      return true;  // other comments are allowed
    }
    std::size_t pos = kCommentPrefixLen;
    std::string name;
    if (!readName(line, pos, name, true)) {
      return fail(n, "missing metric name in " + line.substr(0, kCommentPrefixLen - 1));
    }
    if (closed.count(name) != 0) {
      return fail(n, "family " + name + " continues after other families");
    }
    if (line[2] == 'H') {
      enter(name);
      return true;
    }
    if (pos >= line.size() || line[pos] != ' ') {
      return fail(n, "missing type for " + name);
    }
    const std::string type = line.substr(pos + 1);
    if (type != "counter" && type != "gauge" && type != "histogram" && type != "summary" &&
        type != "untyped") {
      return fail(n, "unknown type '" + type + "'");
    }
    if (declared.count(name) != 0) {
      return fail(n, "second # TYPE for " + name);
    }
    if (current == name && !result.samples.empty() && result.samples.back().name == name) {
      return fail(n, "# TYPE for " + name + " after its samples");
    }
    declared.insert(name);
    result.families.push_back(name);
    enter(name);
    return true;
  }

  // Reads a label value after its opening quote, through the closing quote.
  bool quoted(const std::string& line, std::size_t& pos, std::size_t n, std::string& value) {
    while (pos < line.size() && line[pos] != '"') {
      if (line[pos] != '\\') {
        value += line[pos++];
        continue;
      }
      if (pos + 1 >= line.size()) {
        return fail(n, "dangling escape");
      }
      const char e = line[pos + 1];
      if (e == 'n') {
        value += '\n';
      } else if (e == '\\' || e == '"') {
        value += e;
      } else {
        return fail(n, std::string("bad escape \\") + e);
      }
      pos += 2;
    }
    if (pos >= line.size()) {
      return fail(n, "unterminated label value");
    }
    ++pos;  // closing quote
    return true;
  }

  bool labels(const std::string& line, std::size_t& pos, std::size_t n, ParsedSample& sample) {
    ++pos;  // '{'
    while (pos < line.size() && line[pos] != '}') {
      std::string label;
      if (!readName(line, pos, label, false)) {
        return fail(n, "bad label name");
      }
      if (line.compare(pos, 2, "=\"") != 0) {
        return fail(n, "expected =\" after label " + label);
      }
      pos += 2;
      std::string value;
      if (!quoted(line, pos, n, value)) {
        return false;
      }
      if (!sample.labels.emplace(label, value).second) {
        return fail(n, "duplicate label " + label);
      }
      if (pos < line.size() && line[pos] == ',') {
        ++pos;
      }
    }
    if (pos >= line.size()) {
      return fail(n, "unterminated label set");
    }
    ++pos;  // '}'
    return true;
  }

  bool sampleLine(const std::string& line, std::size_t n) {
    ParsedSample sample;
    std::size_t pos = 0;
    if (!readName(line, pos, sample.name, true)) {
      return fail(n, "bad metric name");
    }
    if (pos < line.size() && line[pos] == '{' && !labels(line, pos, n, sample)) {
      return false;
    }
    if (pos >= line.size() || line[pos] != ' ') {
      return fail(n, "expected a space before the value");
    }
    const std::string rest = line.substr(pos + 1);
    const std::size_t space = rest.find(' ');
    const std::string value_text = rest.substr(0, space);
    if (!parseValue(value_text, sample.value)) {
      return fail(n, "bad value '" + value_text + "'");
    }
    if (space != std::string::npos) {
      double timestamp = 0.0;
      if (!parseValue(rest.substr(space + 1), timestamp)) {
        return fail(n, "bad timestamp");
      }
    }
    const std::string family = familyOf(sample.name, declared);
    if (closed.count(family) != 0) {
      return fail(n, "samples of " + family + " are not together");
    }
    enter(family);
    result.samples.push_back(sample);
    return true;
  }
};

}  // namespace

ExpositionCheck checkExposition(const std::string& text) {
  Checker checker;
  checker.result.ok = true;
  std::size_t start = 0;
  std::size_t n = 0;
  while (start < text.size()) {
    std::size_t end = text.find('\n', start);
    if (end == std::string::npos) {
      checker.fail(n + 1, "last line has no newline");
      break;
    }
    ++n;
    const std::string line = text.substr(start, end - start);
    start = end + 1;
    const bool ok = line.empty()     ? true
                    : line[0] == '#' ? checker.comment(line, n)
                                     : checker.sampleLine(line, n);
    if (!ok) {
      break;
    }
  }
  return checker.result;
}

}  // namespace vxexp
