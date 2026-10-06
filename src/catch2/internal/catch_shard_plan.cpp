
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <catch2/internal/catch_shard_plan.hpp>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_spec.hpp>
#include <catch2/interfaces/catch_interfaces_config.hpp>
#include <catch2/internal/catch_enforce.hpp>
#include <catch2/internal/catch_move_and_forward.hpp>
#include <catch2/internal/catch_string_manip.hpp>
#include <catch2/internal/catch_test_case_registry_impl.hpp>

#include <cstdlib>
#include <istream>
#include <ostream>

namespace Catch {

    ShardDurationMap parseShardDurations( std::istream& in ) {
        ShardDurationMap durations;
        std::string line;
        std::size_t lineNumber = 0;
        while ( std::getline( in, line ) ) {
            ++lineNumber;
            auto const trimmed = trim( CATCH_MOVE( line ) );
            if ( trimmed.empty() || trimmed[0] == '#' ) {
                continue;
            }

            auto const separator = trimmed.find_first_of( " \t" );
            CATCH_ENFORCE( separator != std::string::npos,
                           "Malformed line " << lineNumber
                                             << " in shard durations file: '"
                                             << trimmed << '\'' );

            auto const durationString = trimmed.substr( 0, separator );
            char* parseEnd = nullptr;
            auto const duration =
                std::strtod( durationString.c_str(), &parseEnd );
            CATCH_ENFORCE( parseEnd ==
                                   durationString.c_str() +
                                       durationString.size() &&
                               duration >= 0,
                           "Could not parse '"
                               << durationString
                               << "' as a test duration on line " << lineNumber
                               << " of shard durations file" );

            auto const name = trim( trimmed.substr( separator + 1 ) );
            CATCH_ENFORCE( !name.empty(),
                           "Missing test name on line "
                               << lineNumber << " of shard durations file" );

            durations[name] = duration;
        }
        return durations;
    }

    std::vector<TestCaseHandle>
    filterTestsForShardPlan( IConfig const& config ) {
        auto const& allTestCases = getAllTestCasesSorted( config );
        auto const& testSpec = config.testSpec();

        std::vector<TestCaseHandle const*> filtered;
        filtered.reserve( allTestCases.size() );
        if ( !testSpec.hasFilters() ) {
            for ( auto const& test : allTestCases ) {
                if ( !test.getTestCaseInfo().isHidden() ) {
                    filtered.push_back( &test );
                }
            }
        } else {
            auto const& matches =
                testSpec.matchesByFilter( allTestCases, config );
            for ( auto const& match : matches ) {
                filtered.insert(
                    filtered.end(), match.tests.begin(), match.tests.end() );
            }
            std::sort( filtered.begin(), filtered.end() );
            filtered.erase( std::unique( filtered.begin(), filtered.end() ),
                            filtered.end() );
        }

        std::vector<TestCaseHandle> result;
        result.reserve( filtered.size() );
        for ( auto const* test : filtered ) {
            result.push_back( *test );
        }
        return result;
    }

    namespace {
        // Union-find over the same-shard group tags. The representative of
        // each group is its lexicographically smallest tag, so the result
        // is independent of the order the tests were registered in.
        class ShardGroupUnionFind {
        public:
            void addGroup( std::string const& group ) {
                m_parents.emplace( group, group );
            }

            void unionGroups( std::string const& lhs,
                              std::string const& rhs ) {
                addGroup( lhs );
                addGroup( rhs );
                auto const& lhsRoot = findRoot( lhs );
                auto const& rhsRoot = findRoot( rhs );
                if ( lhsRoot == rhsRoot ) {
                    return;
                }
                if ( lhsRoot < rhsRoot ) {
                    m_parents[rhsRoot] = lhsRoot;
                } else {
                    m_parents[lhsRoot] = rhsRoot;
                }
            }

            std::string const& findRoot( std::string const& group ) {
                auto it = m_parents.find( group );
                assert( it != m_parents.end() );
                if ( it->second == it->first ) {
                    return it->second;
                }
                // Path compression, so repeated lookups stay cheap.
                auto const& root = findRoot( it->second );
                it->second = root;
                return root;
            }

        private:
            std::map<std::string, std::string> m_parents;
        };

        std::vector<StringRef> shardGroupTagsOf( TestCaseInfo const& info ) {
            std::vector<StringRef> groups;
            for ( auto const& tag : info.tags ) {
                if ( startsWith( static_cast<std::string>( tag.original ),
                                 shardGroupTagPrefix ) ) {
                    groups.push_back( tag.original );
                }
            }
            return groups;
        }
    } // namespace

    std::vector<ShardPlanTest>
    makeShardPlanTests( std::vector<TestCaseHandle> const& tests,
                        ShardDurationMap const& durations ) {
        // Tests with the same name need their source location to tell
        // them apart deterministically.
        std::map<std::string, std::size_t> nameCounts;
        for ( auto const& test : tests ) {
            ++nameCounts[test.getTestCaseInfo().name];
        }

        // Merge groups connected through tests carrying multiple group
        // tags, so that such tests can never split a group apart.
        ShardGroupUnionFind groupUnionFind;
        for ( auto const& test : tests ) {
            auto const groups = shardGroupTagsOf( test.getTestCaseInfo() );
            for ( std::size_t i = 1; i < groups.size(); ++i ) {
                groupUnionFind.unionGroups( static_cast<std::string>( groups[0] ),
                                            static_cast<std::string>( groups[i] ) );
            }
            if ( groups.size() == 1 ) {
                groupUnionFind.addGroup( static_cast<std::string>( groups[0] ) );
            }
        }

        std::vector<ShardPlanTest> planTests;
        planTests.reserve( tests.size() );
        for ( auto const& test : tests ) {
            auto const& info = test.getTestCaseInfo();

            ShardPlanTest planTest;
            planTest.stableId = info.name;
            if ( nameCounts[info.name] > 1 ) {
                planTest.stableId += " @ ";
                planTest.stableId += info.lineInfo.file;
                planTest.stableId += ':';
                planTest.stableId += std::to_string( info.lineInfo.line );
            }

            auto const groups = shardGroupTagsOf( info );
            if ( !groups.empty() ) {
                planTest.groupId =
                    groupUnionFind.findRoot( static_cast<std::string>( groups[0] ) );
            }

            auto const duration = durations.find( info.name );
            if ( duration != durations.end() ) {
                planTest.duration = duration->second;
            }

            planTests.push_back( CATCH_MOVE( planTest ) );
        }
        return planTests;
    }

    void printShardPlan( std::ostream& out,
                         std::vector<TestCaseHandle> const& tests,
                         std::vector<ShardPlanShard> const& shards ) {
        std::size_t testCount = 0;
        for ( auto const& shard : shards ) {
            testCount += shard.tests.size();
        }

        out << "Shard plan: " << shards.size() << " shard(s), " << testCount
            << " test(s)\n";
        for ( std::size_t i = 0; i < shards.size(); ++i ) {
            out << "Shard " << i
                << " (total duration: " << shards[i].totalDuration << "):\n";
            for ( auto testIndex : shards[i].tests ) {
                out << "  " << tests[testIndex].getTestCaseInfo().name << '\n';
            }
        }
        out << std::flush;
    }

} // namespace Catch
