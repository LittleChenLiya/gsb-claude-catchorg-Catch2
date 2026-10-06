
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_config.hpp>
#include <catch2/catch_tag_alias_autoregistrar.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/internal/catch_commandline.hpp>
#include <catch2/internal/catch_shard_plan.hpp>
#include <catch2/internal/catch_sharding.hpp>
#include <catch2/internal/catch_test_registry.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    std::vector<std::string>
    shardMemberIds( std::vector<Catch::ShardPlanTest> const& tests,
                    Catch::ShardPlanShard const& shard ) {
        std::vector<std::string> ids;
        for ( auto index : shard.tests ) {
            ids.push_back( tests[index].stableId );
        }
        return ids;
    }
} // namespace

TEST_CASE("Sharding Function", "[approvals]") {
    std::vector<int> testContainer = { 0, 1, 2, 3, 4, 5, 6 };
    std::unordered_map<int, std::vector<std::size_t>> expectedShardSizes = {
        {1, {7}},
        {2, {4, 3}},
        {3, {3, 2, 2}},
        {4, {2, 2, 2, 1}},
        {5, {2, 2, 1, 1, 1}},
        {6, {2, 1, 1, 1, 1, 1}},
        {7, {1, 1, 1, 1, 1, 1, 1}},
    };

    auto shardCount = GENERATE(range(1, 7));
    auto shardIndex = GENERATE_COPY(filter([=](int i) { return i < shardCount; }, range(0, 6)));

    std::vector<int> result = Catch::createShard(testContainer, shardCount, shardIndex);

    auto& sizes = expectedShardSizes[shardCount];
    REQUIRE(result.size() == sizes[shardIndex]);

    std::size_t startIndex = 0;
    for(int i = 0; i < shardIndex; i++) {
        startIndex += sizes[i];
    }

    for(std::size_t i = 0; i < sizes[shardIndex]; i++) {
        CHECK(result[i] == testContainer[i + startIndex]);
    }
}

TEST_CASE("Duration-based shard plan balances the shards", "[shard-plan][approvals]") {
    std::vector<Catch::ShardPlanTest> tests = {
        { "test a", "", 8.0 },
        { "test b", "", 7.0 },
        { "test c", "", 4.0 },
        { "test d", "", 3.0 },
    };

    auto const shards = Catch::createShardPlan( tests, 2 );

    REQUIRE( shards.size() == 2 );
    CHECK( shards[0].totalDuration == Catch::Approx( 11.0 ) );
    CHECK( shards[1].totalDuration == Catch::Approx( 11.0 ) );
    CHECK( shardMemberIds( tests, shards[0] ) ==
           std::vector<std::string>{ "test a", "test d" } );
    CHECK( shardMemberIds( tests, shards[1] ) ==
           std::vector<std::string>{ "test b", "test c" } );
}

TEST_CASE("Duration-based shard plan does not depend on input order",
          "[shard-plan][approvals]") {
    std::vector<Catch::ShardPlanTest> tests = {
        { "test a", "", 8.0 },
        { "test b", "", 7.0 },
        { "test c", "", 4.0 },
        { "test d", "", 3.0 },
        { "test e", "", 5.0 },
        { "test f", "", 6.0 },
    };

    auto const reference = Catch::createShardPlan( tests, 3 );

    std::vector<std::size_t> permutation = { 0, 1, 2, 3, 4, 5 };
    do {
        std::vector<Catch::ShardPlanTest> shuffled;
        for ( auto index : permutation ) {
            shuffled.push_back( tests[index] );
        }

        auto const shards = Catch::createShardPlan( shuffled, 3 );
        REQUIRE( shards.size() == reference.size() );
        for ( std::size_t i = 0; i < shards.size(); ++i ) {
            CHECK( shards[i].totalDuration == Catch::Approx( reference[i].totalDuration ) );
            CHECK( shardMemberIds( shuffled, shards[i] ) ==
                   shardMemberIds( tests, reference[i] ) );
        }
    } while ( std::next_permutation( permutation.begin(), permutation.end() ) );
}

TEST_CASE("Duration-based shard plan breaks duration ties by stable id",
          "[shard-plan][approvals]") {
    std::vector<Catch::ShardPlanTest> tests = {
        { "test b", "", 5.0 },
        { "test a", "", 5.0 },
    };

    auto const shards = Catch::createShardPlan( tests, 2 );
    REQUIRE( shards.size() == 2 );
    // "test a" sorts before "test b", so it is placed first, into shard 0
    CHECK( shardMemberIds( tests, shards[0] ) ==
           std::vector<std::string>{ "test a" } );
    CHECK( shardMemberIds( tests, shards[1] ) ==
           std::vector<std::string>{ "test b" } );
}

TEST_CASE("Duration-based shard plan keeps grouped tests together",
          "[shard-plan][approvals]") {
    std::vector<Catch::ShardPlanTest> tests = {
        { "test a", "", 8.0 },
        { "test b", "", 7.0 },
        { "test c", "group", 4.0 },
        { "test d", "group", 3.0 },
    };

    auto const shards = Catch::createShardPlan( tests, 2 );
    REQUIRE( shards.size() == 2 );

    // The group has a combined duration of 7 and must not be split up
    auto const inShard0 = [&]( std::size_t testIndex ) {
        return std::find( shards[0].tests.begin(), shards[0].tests.end(),
                          testIndex ) != shards[0].tests.end();
    };
    CHECK( inShard0( 2 ) == inShard0( 3 ) );
    CHECK( shards[0].totalDuration + shards[1].totalDuration == Catch::Approx( 22.0 ) );
}

TEST_CASE("Duration-based shard plan uses default duration for unknown tests",
          "[shard-plan][approvals]") {
    std::vector<Catch::ShardPlanTest> tests = {
        { "test a", "", {} },
        { "test b", "", 3.0 },
    };

    SECTION( "built-in default" ) {
        auto const shards = Catch::createShardPlan( tests, 2 );
        REQUIRE( shards.size() == 2 );
        CHECK( shards[0].totalDuration == Catch::Approx( 3.0 ) );
        CHECK( shards[1].totalDuration == Catch::Approx( Catch::defaultShardTestDuration ) );
    }
    SECTION( "explicit default" ) {
        auto const shards = Catch::createShardPlan( tests, 2, 10.0 );
        REQUIRE( shards.size() == 2 );
        CHECK( shards[0].totalDuration == Catch::Approx( 10.0 ) );
        CHECK( shards[1].totalDuration == Catch::Approx( 3.0 ) );
    }
}

TEST_CASE("Duration-based shard plan supports empty shards", "[shard-plan][approvals]") {
    std::vector<Catch::ShardPlanTest> tests = {
        { "test a", "", 2.0 },
        { "test b", "", 1.0 },
    };

    auto const shards = Catch::createShardPlan( tests, 4 );
    REQUIRE( shards.size() == 4 );
    CHECK( shards[0].totalDuration == Catch::Approx( 2.0 ) );
    CHECK( shards[1].totalDuration == Catch::Approx( 1.0 ) );
    CHECK( shards[2].totalDuration == Catch::Approx( 0.0 ) );
    CHECK( shards[3].totalDuration == Catch::Approx( 0.0 ) );
    CHECK( shards[2].tests.empty() );
    CHECK( shards[3].tests.empty() );
}

TEST_CASE("Shard durations file parsing", "[shard-plan][approvals]") {
    SECTION( "well-formed file" ) {
        std::istringstream in(
            "# a comment\n"
            "\n"
            "8 test a\n"
            "7.5 test b with spaces\n"
            "  3\ttest c  \n" );
        auto const durations = Catch::parseShardDurations( in );
        REQUIRE( durations.size() == 3 );
        CHECK( durations.at( "test a" ) == Catch::Approx( 8.0 ) );
        CHECK( durations.at( "test b with spaces" ) == Catch::Approx( 7.5 ) );
        CHECK( durations.at( "test c" ) == Catch::Approx( 3.0 ) );
    }
    SECTION( "empty file" ) {
        std::istringstream in( "" );
        CHECK( Catch::parseShardDurations( in ).empty() );
    }
    SECTION( "malformed lines are rejected" ) {
        std::istringstream missingName( "8\n" );
        CHECK_THROWS( Catch::parseShardDurations( missingName ) );

        std::istringstream badDuration( "not-a-number test a\n" );
        CHECK_THROWS( Catch::parseShardDurations( badDuration ) );

        std::istringstream negativeDuration( "-1 test a\n" );
        CHECK_THROWS( Catch::parseShardDurations( negativeDuration ) );
    }
}

TEST_CASE("Shard plan inputs from registered test cases", "[shard-plan][approvals]") {
    using Catch::SourceLineInfo;
    using Catch::TestCaseHandle;
    using Catch::TestCaseInfo;

    TestCaseInfo testA( "", { "test a", "[shard-group:g1]" },
                        SourceLineInfo( "file.cpp", 10 ) );
    TestCaseInfo testB( "", { "test b", "[shard-group:g1]" },
                        SourceLineInfo( "file.cpp", 20 ) );
    TestCaseInfo testC( "", { "duplicate", "" },
                        SourceLineInfo( "file.cpp", 30 ) );
    TestCaseInfo testD( "", { "duplicate", "[different]" },
                        SourceLineInfo( "other.cpp", 40 ) );

    std::vector<TestCaseHandle> tests = {
        { &testA, nullptr }, { &testB, nullptr },
        { &testC, nullptr }, { &testD, nullptr },
    };

    Catch::ShardDurationMap durations = {
        { "test a", 8.0 }, { "duplicate", 5.0 },
    };

    auto const planTests = Catch::makeShardPlanTests( tests, durations );
    REQUIRE( planTests.size() == 4 );

    // Same-group tags keep tests in one placement group
    CHECK( planTests[0].groupId == planTests[1].groupId );
    CHECK( !planTests[0].groupId.empty() );
    CHECK( planTests[2].groupId.empty() );

    // Historical durations are matched by test name
    REQUIRE( planTests[0].duration );
    CHECK( *planTests[0].duration == Catch::Approx( 8.0 ) );
    CHECK( !planTests[1].duration );

    // Tests with duplicate names get disambiguated stable ids
    CHECK( planTests[2].stableId != planTests[3].stableId );
    CHECK( planTests[2].stableId.find( "duplicate" ) == 0 );
    CHECK( planTests[3].stableId.find( "duplicate" ) == 0 );
}

TEST_CASE("Shard plan filtering follows the normal test spec rules",
          "[shard-plan][approvals]") {
    SECTION( "filters select the matching tests" ) {
        Catch::ConfigData data;
        data.testsOrTags = { "Duration-based shard plan balances the shards" };
        Catch::Config const config( data );

        auto const tests = Catch::filterTestsForShardPlan( config );
        REQUIRE( tests.size() == 1 );
        CHECK( tests[0].getTestCaseInfo().name ==
               "Duration-based shard plan balances the shards" );
    }
    SECTION( "tag aliases are applied to the filters" ) {
        Catch::ConfigData aliasedData;
        aliasedData.testsOrTags = { "[@shard-plan-tests]" };
        Catch::Config const aliasedConfig( aliasedData );
        auto const aliasedTests = Catch::filterTestsForShardPlan( aliasedConfig );

        Catch::ConfigData directData;
        directData.testsOrTags = { "[shard-plan]" };
        Catch::Config const directConfig( directData );
        auto const directTests = Catch::filterTestsForShardPlan( directConfig );

        // The alias must select exactly the tests the aliased tag selects
        REQUIRE( aliasedTests.size() == directTests.size() );
        REQUIRE( !aliasedTests.empty() );
        for ( auto const& test : aliasedTests ) {
            CHECK( test.getTestCaseInfo().tagsAsString().find(
                       "[shard-plan]" ) != std::string::npos );
        }
    }
    SECTION( "hidden tests are excluded unless explicitly requested" ) {
        Catch::ConfigData unfiltered;
        Catch::Config const unfilteredConfig( unfiltered );
        auto const defaultTests =
            Catch::filterTestsForShardPlan( unfilteredConfig );
        for ( auto const& test : defaultTests ) {
            CHECK( !test.getTestCaseInfo().isHidden() );
        }
    }
}

CATCH_REGISTER_TAG_ALIAS( "[@shard-plan-tests]", "[shard-plan]" )

// Fixtures used to verify that same-shard group tags are honoured by the
// --shard-plan mode end to end. They are tagged [approvals] so that they
// do not take part in the approval test runs.
TEST_CASE("Shard plan grouping fixture A",
          "[shard-group:shard-plan-fixture][approvals]") {
    SUCCEED();
}
TEST_CASE("Shard plan grouping fixture B",
          "[shard-group:shard-plan-fixture][approvals]") {
    SUCCEED();
}

TEST_CASE("Parsing shard-planning cli flags", "[shard-plan][approvals]") {
    using namespace Catch::Matchers;

    Catch::ConfigData config;
    auto cli = Catch::makeCommandLineParser( config );

    SECTION( "shard-plan" ) {
        CHECK( cli.parse( { "test", "--shard-plan" } ) );
        REQUIRE( config.shardPlan == true );
    }
    SECTION( "shard-durations" ) {
        CHECK( cli.parse( { "test", "--shard-durations=timings.txt" } ) );
        REQUIRE( config.shardDurationsFile == "timings.txt" );
    }
    SECTION( "shard-default-duration" ) {
        CHECK( cli.parse( { "test", "--shard-default-duration=2.5" } ) );
        REQUIRE( config.shardDefaultDuration == Catch::Approx( 2.5 ) );
    }
    SECTION( "negative shard default duration reports error" ) {
        auto result = cli.parse( { "test", "--shard-default-duration=-1" } );
        CHECK_FALSE( result );
        REQUIRE_THAT( result.errorMessage(),
                      ContainsSubstring(
                          "Could not parse '-1' as shard default duration" ) );
    }
    SECTION( "non-numeric shard default duration reports error" ) {
        auto result = cli.parse( { "test", "--shard-default-duration=abc" } );
        CHECK_FALSE( result );
        REQUIRE_THAT( result.errorMessage(),
                      ContainsSubstring(
                          "Could not parse 'abc' as shard default duration" ) );
    }
}
