// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What a number typed into HeadTracking.devcmd is allowed to look like.
//
// The rule these lock is that a value is taken only when the WHOLE of it
// parses. A prefix parse would turn `inject 1x` into inject mode 1 and
// `markpoint 1,2,3` into a point at x=1, both of which look like the command
// working. The channel drives a running game through the render detour, so a
// command that half-ran is worse than one that was refused.

#include <string>

#include "dev/dev_parse.h"
#include "test_harness.h"

namespace parse = votv_ht::dev_parse;

namespace {

void AWholeNumberIsTaken() {
    long value = -1;
    CHECK(parse::ParseInt("42", value));
    CHECK(value == 42);
    CHECK(parse::ParseInt("-7", value));
    CHECK(value == -7);
    CHECK(parse::ParseInt("0", value));
    CHECK(value == 0);
}

// A hand-typed line collects trailing blanks, and a devcmd file written from
// PowerShell can leave a carriage return on the end of one.
void TrailingBlanksAreFine() {
    long value = 0;
    CHECK(parse::ParseInt("5   ", value));
    CHECK(value == 5);
    CHECK_MSG(parse::ParseInt("5\t\r", value), "a tab and a carriage return are blanks too");
}

void AnythingElseAfterTheNumberIsRefused() {
    long value = 99;
    CHECK_MSG(!parse::ParseInt("1x", value), "a trailing letter is not a number");
    CHECK_MSG(!parse::ParseInt("1 2", value), "a second number is not one number");
    CHECK_MSG(!parse::ParseInt("", value), "an empty value is not a number");
    CHECK_MSG(!parse::ParseInt("  ", value), "blanks alone are not a number");
    CHECK_MSG(!parse::ParseInt("x", value), "a word is not a number");
    CHECK_MSG(value == 99, "a refused parse leaves the caller's value alone");
}

void ARealNumberIsTaken() {
    double value = 0.0;
    CHECK(parse::ParseDouble("1.5", value));
    CHECK_NEAR(value, 1.5, 1e-12);
    CHECK(parse::ParseDouble("-0.25 ", value));
    CHECK_NEAR(value, -0.25, 1e-12);
}

// The decimal comma is the one that bites: strtod stops at the comma, so a
// prefix parse would take "0,5" as 0 and report success.
void ADecimalCommaIsRefusedRatherThanTruncated() {
    double value = 7.0;
    CHECK_MSG(!parse::ParseDouble("0,5", value), "a decimal comma is refused, not truncated to 0");
    CHECK_MSG(!parse::ParseDouble("1.5deg", value), "a trailing unit is refused");
    CHECK_MSG(!parse::ParseDouble("", value), "an empty value is not a number");
    CHECK_MSG(value == 7.0, "a refused parse leaves the caller's value alone");
}

}  // namespace

int main() {
    AWholeNumberIsTaken();
    TrailingBlanksAreFine();
    AnythingElseAfterTheNumberIsRefused();
    ARealNumberIsTaken();
    ADecimalCommaIsRefusedRatherThanTruncated();
    return gr_test::Report();
}
