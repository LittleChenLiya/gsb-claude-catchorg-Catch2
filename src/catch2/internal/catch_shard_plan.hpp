
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#ifndef CATCH_SHARD_PLAN_HPP_INCLUDED
#define CATCH_SHARD_PLAN_HPP_INCLUDED

#include <catch2/internal/catch_move_and_forward.hpp>
#include <catch2/internal/catch_optional.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace Catch {

    class IConfig;
    class TestCaseHandle;
    struct TestCaseInfo;

    //! Duration assigned to tests that have no historical timing data.
    constexpr double defaultShardTestDuration = 1.0;

    //! Tests tagged with a `[shard-group:<id>]` tag are kept in one shard.
    constexpr const char* shardGroupTagPrefix = "shard-group:";

    //! Maps test names to their historical durations.
    using ShardDurationMap = std::map<std::string, double>;

    /**
     * A single test to be placed into a shard by `createShardPlan`.
     *
     * `stableId` must identify the test deterministically, independently
     * of registration order; it is used to break ties between tests with
     * the same duration. `groupId` is empty for tests that may be placed
     * freely; tests sharing a non-empty `groupId` are always placed into
     * the same shard. An empty `duration` means the test has no
     * historical timing data and the default duration is used instead.
     */
    struct ShardPlanTest {
        std::string stableId;
        std::string groupId;
        Catch::Optional<double> duration;
    };

    //! The tests assigned to a single shard, and their total duration.
    struct ShardPlanShard {
        double totalDuration = 0;
        //! Indices into the input test vector, sorted by stable id.
        std::vector<std::size_t> tests;
    };

    /**
     * Distributes `tests` into `shardCount` shards, greedily placing the
     * longest test (or same-shard group of tests) into the currently
     * least-loaded shard.
     *
     * Tests with unknown duration are assigned `defaultDuration`. Ties
     * between tests are broken by their stable ids, ties between shards
     * are broken towards the shard with the lower index, so the resulting
     * plan does not depend on the order of the input.
     */
    inline std::vector<ShardPlanShard>
    createShardPlan( std::vector<ShardPlanTest> const& tests,
                     std::size_t shardCount,
                     double defaultDuration = defaultShardTestDuration ) {
        assert( shardCount > 0 );

        // A placement unit is either a single test, or all tests sharing
        // an explicit same-shard group tag (those must not be split up).
        struct PlacementUnit {
            std::string sortId;
            double totalDuration = 0;
            std::vector<std::size_t> members;
        };

        std::vector<PlacementUnit> units;
        // Group id -> member indices. std::map keeps the iteration order
        // deterministic regardless of the input order.
        std::map<std::string, std::vector<std::size_t>> groups;

        auto const durationOf = [&]( std::size_t idx ) {
            return tests[idx].duration ? *tests[idx].duration
                                       : defaultDuration;
        };

        for ( std::size_t i = 0; i < tests.size(); ++i ) {
            if ( tests[i].groupId.empty() ) {
                PlacementUnit unit;
                unit.sortId = tests[i].stableId;
                unit.totalDuration = durationOf( i );
                unit.members.push_back( i );
                units.push_back( CATCH_MOVE( unit ) );
            } else {
                groups[tests[i].groupId].push_back( i );
            }
        }
        for ( auto const& group : groups ) {
            PlacementUnit unit;
            for ( auto member : group.second ) {
                if ( unit.sortId.empty() ||
                     tests[member].stableId < unit.sortId ) {
                    unit.sortId = tests[member].stableId;
                }
                unit.totalDuration += durationOf( member );
                unit.members.push_back( member );
            }
            units.push_back( CATCH_MOVE( unit ) );
        }

        // Longest first; ties broken by the stable id so that the plan is
        // independent of registration/history file ordering.
        std::sort( units.begin(),
                   units.end(),
                   []( PlacementUnit const& lhs, PlacementUnit const& rhs ) {
                       if ( lhs.totalDuration > rhs.totalDuration ) {
                           return true;
                       }
                       if ( lhs.totalDuration < rhs.totalDuration ) {
                           return false;
                       }
                       return lhs.sortId < rhs.sortId;
                   } );

        std::vector<ShardPlanShard> shards( shardCount );
        for ( auto const& unit : units ) {
            // min_element returns the first minimizer, so ties between
            // equally loaded shards go to the shard with the lower index.
            auto target = std::min_element(
                shards.begin(),
                shards.end(),
                []( ShardPlanShard const& lhs, ShardPlanShard const& rhs ) {
                    return lhs.totalDuration < rhs.totalDuration;
                } );
            target->totalDuration += unit.totalDuration;
            target->tests.insert(
                target->tests.end(), unit.members.begin(), unit.members.end() );
        }

        // Deterministic listing order within each shard.
        for ( auto& shard : shards ) {
            std::sort( shard.tests.begin(),
                       shard.tests.end(),
                       [&]( std::size_t lhs, std::size_t rhs ) {
                           if ( tests[lhs].stableId != tests[rhs].stableId ) {
                               return tests[lhs].stableId < tests[rhs].stableId;
                           }
                           return lhs < rhs;
                       } );
        }

        return shards;
    }

    /**
     * Parses a test duration history file.
     *
     * The expected format is one test per line, each line consisting of
     * the historical duration followed by whitespace and the test name,
     * e.g. `1.25 my test name`. Empty lines and lines starting with `#`
     * are ignored. Malformed lines are an error.
     */
    ShardDurationMap parseShardDurations( std::istream& in );

    /**
     * Filters the registered tests with the config's test spec, using the
     * same rules as a normal test run (hidden tests are excluded unless
     * explicitly requested, tag aliases are applied, ...), but without
     * applying the ordinary contiguous sharding.
     */
    std::vector<TestCaseHandle> filterTestsForShardPlan( IConfig const& config );

    /**
     * Computes the shard plan inputs for the given tests: stable ids
     * (disambiguating tests that share a name via their source location),
     * same-shard group ids from `[shard-group:...]` tags, and historical
     * durations looked up by test name.
     */
    std::vector<ShardPlanTest>
    makeShardPlanTests( std::vector<TestCaseHandle> const& tests,
                        ShardDurationMap const& durations );

    //! Prints a human-readable shard plan.
    void printShardPlan( std::ostream& out,
                         std::vector<TestCaseHandle> const& tests,
                         std::vector<ShardPlanShard> const& shards );

} // namespace Catch

#endif // CATCH_SHARD_PLAN_HPP_INCLUDED
