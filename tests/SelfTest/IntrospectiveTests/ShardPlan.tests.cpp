
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/internal/catch_shard_plan.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace {
    // Owns the created TestCaseInfos, so that handles to them stay valid
    struct FakeTestRegistry {
        std::vector<Catch::Detail::unique_ptr<Catch::TestCaseInfo>> infos;

        void add( std::string const& name,
                  std::string const& tags = "",
                  std::string const& file = "fake.cpp",
                  std::size_t line = 1 ) {
            infos.push_back( Catch::makeTestCaseInfo(
                "", { name, tags }, { file.c_str(), line } ) );
        }

        std::vector<Catch::TestCaseHandle> handles() {
            std::vector<Catch::TestCaseHandle> result;
            result.reserve( infos.size() );
            for ( auto& info : infos ) {
                result.emplace_back( info.get(), nullptr );
            }
            return result;
        }
    };

    std::vector<Catch::TestCaseHandle const*>
    handlePtrs( std::vector<Catch::TestCaseHandle>& handles ) {
        std::vector<Catch::TestCaseHandle const*> result;
        result.reserve( handles.size() );
        for ( auto& handle : handles ) {
            result.push_back( &handle );
        }
        return result;
    }

    std::vector<std::vector<std::string>>
    shardNames( std::vector<Catch::ShardPlan> const& shards ) {
        std::vector<std::vector<std::string>> names;
        for ( auto const& shard : shards ) {
            names.emplace_back();
            for ( auto const* test : shard.tests ) {
                names.back().push_back( test->getTestCaseInfo().name );
            }
        }
        return names;
    }
} // namespace

TEST_CASE( "Shard plan balances shards using historical durations",
           "[sharding][shard-plan][approvals]" ) {
    FakeTestRegistry registry;
    registry.add( "a" );
    registry.add( "b" );
    registry.add( "c" );
    registry.add( "d" );
    auto handles = registry.handles();
    auto const tests = handlePtrs( handles );

    const Catch::TestTimingMap timings{
        { "a", 8 }, { "b", 7 }, { "c", 4 }, { "d", 3 },
    };

    const auto shards = Catch::createShardPlan( tests, timings, 2, 1.0 );

    REQUIRE( shards.size() == 2 );
    CHECK( shards[0].totalDuration == Catch::Approx( 11 ) );
    CHECK( shards[1].totalDuration == Catch::Approx( 11 ) );
    CHECK( shardNames( shards ) ==
           std::vector<std::vector<std::string>>{ { "a", "d" },
                                                  { "b", "c" } } );
}

TEST_CASE( "Shard plan does not depend on registration or history order",
           "[sharding][shard-plan][approvals]" ) {
    auto makePlanNames = []( std::vector<std::string> const& registrationOrder,
                             std::string const& history ) {
        FakeTestRegistry registry;
        for ( auto const& name : registrationOrder ) {
            registry.add( name );
        }
        auto handles = registry.handles();
        auto const tests = handlePtrs( handles );
        std::istringstream historyStream( history );
        const auto timings = Catch::parseTestTimings( historyStream );
        return shardNames( Catch::createShardPlan( tests, timings, 3, 1.0 ) );
    };

    const std::vector<std::string> names{ "alpha", "beta",  "gamma",
                                          "delta", "epsilon", "zeta" };
    const std::string history = "9 alpha\n"
                                "1 beta\n"
                                "5 gamma\n"
                                "2 delta\n"
                                "4 epsilon\n"
                                "3 zeta\n";

    const auto plan = makePlanNames( names, history );

    auto reversedNames = names;
    std::reverse( reversedNames.begin(), reversedNames.end() );
    const std::string reversedHistory = "3 zeta\n"
                                        "4 epsilon\n"
                                        "2 delta\n"
                                        "5 gamma\n"
                                        "1 beta\n"
                                        "9 alpha\n";
    const auto reversedPlan = makePlanNames( reversedNames, reversedHistory );

    CHECK( plan == reversedPlan );
}

TEST_CASE( "Shard plan keeps tests with shared shard group tag together",
           "[sharding][shard-plan][approvals]" ) {
    FakeTestRegistry registry;
    registry.add( "grouped 1", "[shard-group:db]" );
    registry.add( "grouped 2", "[shard-group:db]" );
    registry.add( "solo" );
    auto handles = registry.handles();
    auto const tests = handlePtrs( handles );

    const Catch::TestTimingMap timings{
        { "grouped 1", 6 }, { "grouped 2", 6 }, { "solo", 8 },
    };

    const auto shards = Catch::createShardPlan( tests, timings, 2, 1.0 );

    REQUIRE( shards.size() == 2 );
    // The group (12s in total) must not be split, even though splitting
    // it would balance the shards better (6+8 vs 6).
    CHECK( shardNames( shards ) ==
           std::vector<std::vector<std::string>>{ { "grouped 1", "grouped 2" },
                                                  { "solo" } } );
    CHECK( shards[0].totalDuration == Catch::Approx( 12 ) );
    CHECK( shards[1].totalDuration == Catch::Approx( 8 ) );
}

TEST_CASE( "Shard plan merges transitively shared shard groups",
           "[sharding][shard-plan][approvals]" ) {
    FakeTestRegistry registry;
    registry.add( "a", "[shard-group:x]" );
    registry.add( "b", "[shard-group:x][shard-group:y]" );
    registry.add( "c", "[shard-group:y]" );
    registry.add( "d" );
    auto handles = registry.handles();
    auto const tests = handlePtrs( handles );

    const Catch::TestTimingMap timings{
        { "a", 1 }, { "b", 1 }, { "c", 1 }, { "d", 4 },
    };

    const auto shards = Catch::createShardPlan( tests, timings, 2, 1.0 );

    REQUIRE( shards.size() == 2 );
    // a, b and c are all part of the same group through b's two tags
    CHECK( shardNames( shards ) ==
           std::vector<std::vector<std::string>>{ { "d" },
                                                  { "a", "b", "c" } } );
}

TEST_CASE( "Shard plan uses explicit default duration for tests missing from history",
           "[sharding][shard-plan][approvals]" ) {
    FakeTestRegistry registry;
    registry.add( "a" );
    registry.add( "b" );
    registry.add( "c" );
    registry.add( "d" );
    auto handles = registry.handles();
    auto const tests = handlePtrs( handles );

    SECTION( "no history at all" ) {
        const auto shards =
            Catch::createShardPlan( tests, Catch::TestTimingMap{}, 2, 2.0 );

        REQUIRE( shards.size() == 2 );
        CHECK( shards[0].totalDuration == Catch::Approx( 4 ) );
        CHECK( shards[1].totalDuration == Catch::Approx( 4 ) );
        CHECK( shardNames( shards ) ==
               std::vector<std::vector<std::string>>{ { "a", "c" },
                                                      { "b", "d" } } );
    }
    SECTION( "partial history" ) {
        const Catch::TestTimingMap timings{ { "a", 10 } };
        const auto shards = Catch::createShardPlan( tests, timings, 2, 1.0 );

        REQUIRE( shards.size() == 2 );
        CHECK( shardNames( shards ) ==
               std::vector<std::vector<std::string>>{ { "a" },
                                                      { "b", "c", "d" } } );
        CHECK( shards[0].totalDuration == Catch::Approx( 10 ) );
        CHECK( shards[1].totalDuration == Catch::Approx( 3 ) );
    }
}

TEST_CASE( "Shard plan can contain empty shards", "[sharding][shard-plan][approvals]" ) {
    FakeTestRegistry registry;
    registry.add( "a" );
    registry.add( "b" );
    auto handles = registry.handles();
    auto const tests = handlePtrs( handles );

    const Catch::TestTimingMap timings{ { "a", 5 }, { "b", 5 } };

    const auto shards = Catch::createShardPlan( tests, timings, 4, 1.0 );

    REQUIRE( shards.size() == 4 );
    CHECK( shardNames( shards ) ==
           std::vector<std::vector<std::string>>{
               { "a" }, { "b" }, {}, {} } );
    CHECK( shards[2].totalDuration == Catch::Approx( 0 ) );
    CHECK( shards[3].totalDuration == Catch::Approx( 0 ) );
}

TEST_CASE( "Shard plan handles duplicate test names from different source locations",
           "[sharding][shard-plan][approvals]" ) {
    auto makePlanNames = []( bool reversedRegistration ) {
        FakeTestRegistry registry;
        registry.add( "dup", "", "a.cpp", 10 );
        registry.add( "dup", "", "b.cpp", 20 );
        registry.add( "other" );
        if ( reversedRegistration ) {
            std::reverse( registry.infos.begin(), registry.infos.end() );
        }
        auto handles = registry.handles();
        auto const tests = handlePtrs( handles );
        const Catch::TestTimingMap timings{ { "dup", 4 }, { "other", 8 } };
        return shardNames( Catch::createShardPlan( tests, timings, 2, 1.0 ) );
    };

    const auto plan = makePlanNames( false );
    const auto reversedPlan = makePlanNames( true );

    // The plan is deterministic even with duplicate test names
    CHECK( plan == reversedPlan );

    // All 3 tests were placed
    std::size_t totalTests = 0;
    for ( auto const& shard : plan ) {
        totalTests += shard.size();
    }
    CHECK( totalTests == 3 );
}

TEST_CASE( "stableTestOrderLess provides a registration-independent order",
           "[sharding][shard-plan][approvals]" ) {
    auto tcA = Catch::makeTestCaseInfo( "", { "same", "" }, { "a.cpp", 1 } );
    auto tcB = Catch::makeTestCaseInfo( "", { "same", "" }, { "b.cpp", 1 } );
    auto tcC = Catch::makeTestCaseInfo( "", { "same", "" }, { "b.cpp", 2 } );
    auto tcD = Catch::makeTestCaseInfo( "", { "zzz", "" }, { "a.cpp", 1 } );

    // Duplicate names are disambiguated by the source location
    CHECK( Catch::stableTestOrderLess( *tcA, *tcB ) );
    CHECK( Catch::stableTestOrderLess( *tcB, *tcC ) );
    CHECK( !Catch::stableTestOrderLess( *tcB, *tcA ) );
    CHECK( !Catch::stableTestOrderLess( *tcC, *tcB ) );
    // Irreflexive
    CHECK( !Catch::stableTestOrderLess( *tcA, *tcA ) );
    // Name dominates the ordering
    CHECK( !Catch::stableTestOrderLess( *tcD, *tcA ) );
    CHECK( Catch::stableTestOrderLess( *tcA, *tcD ) );
}

TEST_CASE( "parseTestTimings parses test duration history files",
           "[sharding][shard-plan][approvals]" ) {
    SECTION( "well formed input" ) {
        std::istringstream input( "# a comment\n"
                                  "\n"
                                  "8 test with spaces\n"
                                  "0.5   extra whitespace  \n"
                                  "1e2 scientific notation\n" );
        const auto timings = Catch::parseTestTimings( input );

        REQUIRE( timings.size() == 3 );
        CHECK( timings.at( "test with spaces" ) == Catch::Approx( 8 ) );
        CHECK( timings.at( "extra whitespace" ) == Catch::Approx( 0.5 ) );
        CHECK( timings.at( "scientific notation" ) == Catch::Approx( 100 ) );
    }
    SECTION( "empty input" ) {
        std::istringstream input( "" );
        CHECK( Catch::parseTestTimings( input ).empty() );
    }
    SECTION( "line without test name is an error" ) {
        std::istringstream input( "1.5\n" );
        CHECK_THROWS( Catch::parseTestTimings( input ) );
    }
    SECTION( "line without duration is an error" ) {
        std::istringstream input( "testname\n" );
        CHECK_THROWS( Catch::parseTestTimings( input ) );
    }
    SECTION( "non-numeric duration is an error" ) {
        std::istringstream input( "abc test\n" );
        CHECK_THROWS( Catch::parseTestTimings( input ) );
    }
    SECTION( "negative duration is an error" ) {
        std::istringstream input( "-1 test\n" );
        CHECK_THROWS( Catch::parseTestTimings( input ) );
    }
    SECTION( "duplicate entries are an error" ) {
        std::istringstream input( "1 test\n"
                                  "2 test\n" );
        CHECK_THROWS( Catch::parseTestTimings( input ) );
    }
}
