
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#ifndef CATCH_SHARD_PLAN_HPP_INCLUDED
#define CATCH_SHARD_PLAN_HPP_INCLUDED

#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace Catch {

    class TestCaseHandle;
    struct TestCaseInfo;

    /**
     * Maps test names to their historical run duration, in seconds.
     *
     * Tests that are not present in the map are assumed to take
     * a user-provided default duration instead.
     */
    using TestTimingMap = std::map<std::string, double>;

    /**
     * The duration assumed for tests that are missing from the test
     * timing history, unless the user explicitly provides a different
     * default duration.
     */
    constexpr double kDefaultShardPlanTestDuration = 1.0;

    /**
     * Tags with this prefix mark tests as belonging to an explicit
     * shard group.
     *
     * All tests sharing the same group tag (e.g. `[shard-group:db]`)
     * are guaranteed to be placed into the same shard by
     * `createShardPlan`.
     */
    constexpr char shardGroupTagPrefix[] = "shard-group:";

    /**
     * One shard of a shard plan: the tests assigned to the shard and
     * their total expected duration.
     */
    struct ShardPlan {
        double totalDuration = 0;
        std::vector<TestCaseHandle const*> tests;
    };

    /**
     * Strict total order over test cases that is independent of the
     * test registration order.
     *
     * Orders by name, then class name, then tags, and finally by source
     * location, so that even tests with duplicate names (registered from
     * different source locations) are ordered deterministically.
     */
    bool stableTestOrderLess( TestCaseInfo const& lhs,
                              TestCaseInfo const& rhs );

    /**
     * Parses a test timing history file into a TestTimingMap.
     *
     * The expected format is one test per line:
     *
     * ```
     * <duration in seconds> <test name>
     * ```
     *
     * Empty lines and lines starting with `#` are ignored. The test
     * name is the rest of the line after the duration, so test names
     * containing whitespace are supported. It is an error for a line
     * to be malformed, for a duration to be negative, or for a test to
     * have multiple entries (so that the resulting plan does not depend
     * on the order of lines in the file).
     *
     * Throws on malformed input.
     */
    TestTimingMap parseTestTimings( std::istream& input );

    /**
     * Distributes `tests` into `shardCount` shards, balancing the
     * expected total duration of each shard.
     *
     * The tests are distributed greedily: tests are considered in order
     * of decreasing expected duration (ties are broken by the stable
     * test order, see `stableTestOrderLess`), and each test is placed
     * into the shard with the currently smallest total duration (ties
     * are broken by shard index). Tests without an entry in `timings`
     * are assumed to take `defaultDuration` seconds.
     *
     * Tests that share an explicit shard group tag (a tag starting with
     * `shardGroupTagPrefix`) are treated as a single unit with the
     * summed duration of the whole group, and are thus always placed
     * into the same shard.
     *
     * The result is fully deterministic: it depends neither on the
     * order of `tests`, nor on the order of entries in the timing
     * history the `timings` map was built from.
     */
    std::vector<ShardPlan> createShardPlan(
        std::vector<TestCaseHandle const*> const& tests,
        TestTimingMap const& timings,
        std::size_t shardCount,
        double defaultDuration );

} // end namespace Catch

#endif // CATCH_SHARD_PLAN_HPP_INCLUDED
