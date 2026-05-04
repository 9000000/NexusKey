#include "JunitXmlWriter.h"

#include <cstddef>

namespace NextKey::TestRunner::JunitXmlWriter {

namespace {

void XmlEscape(std::ostream& out, std::string_view text) {
    for (char c : text) {
        switch (c) {
            case '<':  out << "&lt;";   break;
            case '>':  out << "&gt;";   break;
            case '&':  out << "&amp;";  break;
            case '"':  out << "&quot;"; break;
            case '\'': out << "&apos;"; break;
            default:   out << c;
        }
    }
}

void WriteSeconds(std::ostream& out, uint64_t ms) {
    out << (ms / 1000) << '.';
    const auto frac = ms % 1000;
    if (frac < 100) out << '0';
    if (frac <  10) out << '0';
    out << frac;
}

}  // namespace

void Write(std::ostream& out,
           std::string_view suiteName,
           const std::vector<CaseResult>& results,
           uint64_t totalWallClockMs) {
    std::size_t failures = 0;
    for (const auto& r : results) {
        if (!r.passed) ++failures;
    }

    out << R"(<?xml version="1.0" encoding="UTF-8"?>)" << '\n';
    out << R"(<testsuites name="NextKeyTestRunner" tests=")" << results.size()
        << R"(" failures=")" << failures
        << R"(" time=")";
    WriteSeconds(out, totalWallClockMs);
    out << R"(">)" << '\n';

    out << R"(  <testsuite name=")";
    XmlEscape(out, suiteName);
    out << R"(" tests=")" << results.size()
        << R"(" failures=")" << failures
        << R"(" time=")";
    WriteSeconds(out, totalWallClockMs);
    out << R"(">)" << '\n';

    for (const auto& r : results) {
        out << R"(    <testcase name=")";
        XmlEscape(out, r.name);
        out << R"(" classname=")";
        XmlEscape(out, suiteName);
        out << R"(" time=")";
        WriteSeconds(out, r.wallClockMs);
        out << R"(")";

        if (r.passed) {
            out << R"(/>)" << '\n';
        } else {
            out << R"(>)" << '\n';
            out << R"(      <failure message="Clipboard mismatch" type="AssertionError">)";
            XmlEscape(out, r.failureMessage);
            out << R"(</failure>)" << '\n';
            out << R"(    </testcase>)" << '\n';
        }
    }

    out << R"(  </testsuite>)" << '\n';
    out << R"(</testsuites>)" << '\n';
}

}  // namespace NextKey::TestRunner::JunitXmlWriter
