
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <catch2/internal/catch_shard_plan.hpp>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/internal/catch_enforce.hpp>
#include <catch2/internal/catch_move_and_forward.hpp>
#include <catch2/internal/catch_parse_numbers.hpp>
#include <catch2/internal/catch_source_line_info.hpp>
#include <catch2/internal/catch_string_manip.hpp>
#include <catch2/internal/catch_stringref.hpp>

#include <algorithm>
#include <istream>
#include <utility>

namespace Catch {

    bool stableTestOrderLess( TestCaseInfo const& lhs,
                              TestCaseInfo const& rhs ) {
        if ( lhs.name != rhs.name ) { return lhs.name < rhs.name; }
        const auto classNameCmp = lhs.className.compare( rhs.className );
        if ( classNameCmp != 0 ) { return classNameCmp < 0; }
        if ( lhs.tags != rhs.tags ) { return lhs.tags < rhs.tags; }
        // Tests with duplicate names (and tags) are still ordered
        // deterministically, based on their source location.
        const StringRef lhsFile = lhs.lineInfo.file;
        const StringRef rhsFile = rhs.lineInfo.file;
        if ( lhsFile != rhsFile ) { return lhsFile < rhsFile; }
        return lhs.lineInfo.line < rhs.lineInfo.line;
    }

    namespace {
        bool handleLess( TestCaseHandle const* lhs,
                         TestCaseHandle const* rhs ) {
            return stableTestOrderLess( lhs->getTestCaseInfo(),
                                        rhs->getTestCaseInfo() );
        }

        double lookupDuration( TestCaseInfo const& test,
                               TestTimingMap const& timings,
                               double defaultDuration ) {
            const auto it = timings.find( test.name );
            return it != timings.end() ? it->second : defaultDuration;
        }

        bool isShardGroupTag( Tag const& tag ) {
            const StringRef prefix( shardGroupTagPrefix,
                                    sizeof( shardGroupTagPrefix ) - 1 );
            const StringRef original = tag.original;
            return original.size() >= prefix.size() &&
                   original.substr( 0, prefix.size() ) == prefix;
        }

        // Simple union-find over test indices, used to merge tests that
        // share a shard group tag into a single scheduling unit.
        class GroupUnionFind {
        public:
            explicit GroupUnionFind( std::size_t size ):
                m_parents( size ) {
                for ( std::size_t i = 0; i < size; ++i ) {
                    m_parents[i] = i;
                }
            }

            std::size_t find( std::size_t idx ) {
                std::size_t root = idx;
                while ( m_parents[root] != root ) { root = m_parents[root]; }
                // Path compression
                while ( m_parents[idx] != root ) {
                    const std::size_t next = m_parents[idx];
                    m_parents[idx] = root;
                    idx = next;
                }
                return root;
            }

            void unite( std::size_t lhs, std::size_t rhs ) {
                lhs = find( lhs );
                rhs = find( rhs );
                // Deterministic: always attach the larger root to the
                // smaller one.
                if ( lhs < rhs ) {
                    m_parents[rhs] = lhs;
                } else if ( rhs < lhs ) {
                    m_parents[lhs] = rhs;
                }
            }

        private:
            std::vector<std::size_t> m_parents;
        };
    } // end unnamed namespace

    TestTimingMap parseTestTimings( std::istream& input ) {
        TestTimingMap timings;
        std::string line;
        std::size_t lineNumber = 0;
        while ( std::getline( input, line ) ) {
            ++lineNumber;
            line = trim( line );
            if ( line.empty() || startsWith( line, '#' ) ) { continue; }

            const auto separator = line.find_first_of( " \t" );
            CATCH_ENFORCE( separator != std::string::npos,
                           "Malformed line "
                               << lineNumber
                               << " in test timings file: expected "
                                  "'<duration in seconds> <test name>'" );

            const std::string durationString = line.substr( 0, separator );
            const std::string testName = trim( line.substr( separator + 1 ) );
            CATCH_ENFORCE( !testName.empty(),
                           "Malformed line " << lineNumber
                                             << " in test timings file: "
                                                "missing test name" );

            const auto parsedDuration = parseDouble( durationString );
            CATCH_ENFORCE( parsedDuration && *parsedDuration >= 0,
                           "Malformed line "
                               << lineNumber
                               << " in test timings file: '"
                               << durationString
                               << "' is not a valid duration" );
            const double duration = *parsedDuration;

            const auto inserted = timings.emplace( testName, duration );
            CATCH_ENFORCE( inserted.second,
                           "Malformed test timings file: test '"
                               << testName
                               << "' has multiple duration entries" );
        }
        return timings;
    }

    std::vector<ShardPlan> createShardPlan(
        std::vector<TestCaseHandle const*> const& tests,
        TestTimingMap const& timings,
        std::size_t shardCount,
        double defaultDuration ) {
        CATCH_ENFORCE( shardCount > 0, "Shard count must be positive" );
        CATCH_ENFORCE( defaultDuration >= 0,
                       "Default test duration must not be negative" );

        // Merge tests sharing a shard group tag into scheduling units,
        // so that they can never end up in different shards.
        GroupUnionFind groups( tests.size() );
        {
            std::map<std::string, std::size_t> groupTagToTest;
            for ( std::size_t i = 0; i < tests.size(); ++i ) {
                for ( auto const& tag : tests[i]->getTestCaseInfo().tags ) {
                    if ( !isShardGroupTag( tag ) ) { continue; }
                    const std::string groupTag( tag.original.data(),
                                                tag.original.size() );
                    const auto inserted =
                        groupTagToTest.emplace( groupTag, i );
                    if ( !inserted.second ) {
                        groups.unite( inserted.first->second, i );
                    }
                }
            }
        }

        struct SchedulingUnit {
            double duration = 0;
            // Sorted according to the stable test order
            std::vector<TestCaseHandle const*> tests;
        };

        // Gather the members of each scheduling unit. Using std::map
        // keeps the iteration order deterministic.
        std::map<std::size_t, std::vector<std::size_t>> unitMembers;
        for ( std::size_t i = 0; i < tests.size(); ++i ) {
            unitMembers[groups.find( i )].push_back( i );
        }

        std::vector<SchedulingUnit> units;
        units.reserve( unitMembers.size() );
        for ( auto& members : unitMembers ) {
            SchedulingUnit unit;
            for ( const auto memberIdx : members.second ) {
                TestCaseHandle const* test = tests[memberIdx];
                unit.tests.push_back( test );
                unit.duration += lookupDuration(
                    test->getTestCaseInfo(), timings, defaultDuration );
            }
            std::sort( unit.tests.begin(), unit.tests.end(), handleLess );
            units.push_back( CATCH_MOVE( unit ) );
        }

        // Schedule the most expensive units first. Ties are broken by
        // the stable test order of the unit's first test, so that the
        // resulting plan does not depend on the registration order.
        std::sort( units.begin(),
                   units.end(),
                   []( SchedulingUnit const& lhs,
                       SchedulingUnit const& rhs ) {
                       if ( lhs.duration > rhs.duration ) { return true; }
                       if ( rhs.duration > lhs.duration ) { return false; }
                       return handleLess( lhs.tests.front(),
                                          rhs.tests.front() );
                   } );

        std::vector<ShardPlan> shards( shardCount );
        for ( auto& unit : units ) {
            // Place the unit into the shard with the currently smallest
            // total duration. Ties are broken by shard index.
            std::size_t target = 0;
            for ( std::size_t shard = 1; shard < shardCount; ++shard ) {
                if ( shards[shard].totalDuration <
                     shards[target].totalDuration ) {
                    target = shard;
                }
            }
            shards[target].totalDuration += unit.duration;
            shards[target].tests.insert( shards[target].tests.end(),
                                         unit.tests.begin(),
                                         unit.tests.end() );
        }

        // Deterministic test order within each shard
        for ( auto& shard : shards ) {
            std::sort( shard.tests.begin(), shard.tests.end(), handleLess );
        }

        return shards;
    }

} // end namespace Catch
