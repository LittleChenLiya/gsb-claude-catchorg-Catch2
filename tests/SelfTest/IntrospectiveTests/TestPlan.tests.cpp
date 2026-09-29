
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_test_plan.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <helpers/parse_test_spec.hpp>

#include <string>
#include <utility>
#include <vector>

// Exercises the overload of planTestRun that uses globally registered
// dependencies. The names are unique to this file so that the global
// registrations cannot clash with real test cases.
CATCH_TEST_DEPENDENCY( "plan-registry-A", "plan-registry-B" )

namespace {
    auto fakeTestCase( const char* name, const char* tags = "" ) {
        return Catch::makeTestCaseInfo(
            "", { name, tags }, CATCH_INTERNAL_LINEINFO );
    }

    struct FakeTests {
        std::vector<Catch::Detail::unique_ptr<Catch::TestCaseInfo>> storage;

        Catch::TestCaseInfo const* add( const char* name,
                                        const char* tags = "" ) {
            storage.push_back( fakeTestCase( name, tags ) );
            return storage.back().get();
        }

        std::vector<Catch::TestCaseInfo const*> all() const {
            std::vector<Catch::TestCaseInfo const*> tests;
            for ( auto const& test : storage ) {
                tests.push_back( test.get() );
            }
            return tests;
        }
    };

    std::vector<std::vector<std::string>>
    layerNames( Catch::TestPlan const& plan ) {
        std::vector<std::vector<std::string>> names;
        for ( auto const& layer : plan.layers ) {
            std::vector<std::string> layerNames;
            for ( auto const* test : layer ) {
                layerNames.push_back( test->name );
            }
            names.push_back( layerNames );
        }
        return names;
    }

    Catch::TestDependency dep( std::string dependent,
                               std::string prerequisite ) {
        return { std::move( dependent ), std::move( prerequisite ) };
    }
} // end unnamed namespace

TEST_CASE( "planTestRun layers transitive dependencies",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-A" );
    tests.add( "plan-B" );
    tests.add( "plan-C" );
    tests.add( "plan-D" );

    // A depends on B and C, both B and C depend on D
    std::vector<Catch::TestDependency> dependencies{
        dep( "plan-A", "plan-B" ), dep( "plan-A", "plan-C" ),
        dep( "plan-B", "plan-D" ), dep( "plan-C", "plan-D" ) };

    SECTION( "selecting only A pulls in B, C and D in layers" ) {
        auto plan = Catch::planTestRun(
            tests.all(), Catch::parseTestSpec( "plan-A" ), dependencies );

        REQUIRE( plan.ok() );
        CHECK( plan.testCount() == 4 );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{
                   { "plan-D" }, { "plan-B", "plan-C" }, { "plan-A" } } );
    }
    SECTION( "order within a layer follows the declaration order" ) {
        FakeTests reordered;
        reordered.add( "plan-A" );
        reordered.add( "plan-C" );
        reordered.add( "plan-B" );
        reordered.add( "plan-D" );

        auto plan = Catch::planTestRun( reordered.all(),
                                        Catch::parseTestSpec( "plan-A" ),
                                        dependencies );

        REQUIRE( plan.ok() );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{
                   { "plan-D" }, { "plan-C", "plan-B" }, { "plan-A" } } );
    }
    SECTION( "selecting everything still respects the dependencies" ) {
        auto plan = Catch::planTestRun(
            tests.all(), Catch::parseTestSpec( "plan-*" ), dependencies );

        REQUIRE( plan.ok() );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{
                   { "plan-D" }, { "plan-B", "plan-C" }, { "plan-A" } } );
    }
    SECTION( "dependencies of unselected tests are not pulled in" ) {
        auto plan = Catch::planTestRun(
            tests.all(), Catch::parseTestSpec( "plan-D" ), dependencies );

        REQUIRE( plan.ok() );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{ { "plan-D" } } );
    }
}

TEST_CASE( "planTestRun without dependencies puts the selection into one layer",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-free-1" );
    tests.add( "plan-free-2" );
    tests.add( "plan-hidden", "[.]" );

    SECTION( "empty spec selects all non-hidden tests" ) {
        auto plan = Catch::planTestRun(
            tests.all(), Catch::TestSpec{}, {} );

        REQUIRE( plan.ok() );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{
                   { "plan-free-1", "plan-free-2" } } );
    }
    SECTION( "empty selection creates an empty plan" ) {
        auto plan = Catch::planTestRun(
            tests.all(), Catch::parseTestSpec( "no-such-test" ), {} );

        REQUIRE( plan.ok() );
        CHECK( plan.layers.empty() );
        CHECK( plan.testCount() == 0 );
    }
}

TEST_CASE( "planTestRun pulls in hidden dependencies",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-uses-helper" );
    tests.add( "plan-helper", "[.]" );

    std::vector<Catch::TestDependency> dependencies{
        dep( "plan-uses-helper", "plan-helper" ) };

    auto plan = Catch::planTestRun(
        tests.all(), Catch::parseTestSpec( "plan-uses-helper" ), dependencies );

    REQUIRE( plan.ok() );
    CHECK( layerNames( plan ) ==
           std::vector<std::vector<std::string>>{
               { "plan-helper" }, { "plan-uses-helper" } } );
}

TEST_CASE( "planTestRun reports conflicts with the test spec",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-A" );
    tests.add( "plan-slow", "[slow]" );
    tests.add( "plan-B" );

    std::vector<Catch::TestDependency> dependencies{
        dep( "plan-A", "plan-slow" ), dep( "plan-A", "plan-B" ) };

    SECTION( "prerequisite excluded by tag is a conflict, not re-added" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( R"("plan-A" ~[slow])" ),
            dependencies );

        REQUIRE( !plan.ok() );
        CHECK( plan.layers.empty() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "plan-slow" ) &&
                    Catch::Matchers::ContainsSubstring( "excluded" ) );
    }
    SECTION( "prerequisite excluded by name is a conflict" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( R"("plan-A",~"plan-slow")" ),
            dependencies );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "plan-slow" ) );
    }
    SECTION( "excluding an unrelated test is not a conflict" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( R"("plan-A" ~[unrelated])" ),
            dependencies );

        REQUIRE( plan.ok() );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{
                   { "plan-slow", "plan-B" }, { "plan-A" } } );
    }
}

TEST_CASE( "planTestRun detects dependency cycles",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-A" );
    tests.add( "plan-B" );
    tests.add( "plan-C" );

    SECTION( "self dependency" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-A", "plan-A" ) } );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "depends on itself" ) );
    }
    SECTION( "mutual dependency" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-A", "plan-B" ), dep( "plan-B", "plan-A" ) } );

        REQUIRE( !plan.ok() );
        CHECK( plan.layers.empty() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "cycle" ) &&
                    Catch::Matchers::ContainsSubstring( "plan-A" ) &&
                    Catch::Matchers::ContainsSubstring( "plan-B" ) );
    }
    SECTION( "longer cycle" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-A", "plan-B" ),
              dep( "plan-B", "plan-C" ),
              dep( "plan-C", "plan-A" ) } );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "plan-C" ) );
    }
    SECTION( "cycle outside of the selection does not fail the plan" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-C" ),
            { dep( "plan-A", "plan-B" ), dep( "plan-B", "plan-A" ) } );

        REQUIRE( plan.ok() );
        CHECK( layerNames( plan ) ==
               std::vector<std::vector<std::string>>{ { "plan-C" } } );
    }
}

TEST_CASE( "planTestRun validates dependency declarations",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-A" );
    tests.add( "plan-dup", "[one]" );
    tests.add( "plan-dup", "[two]" );

    SECTION( "unknown prerequisite" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-A", "plan-missing" ) } );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "unknown" ) &&
                    Catch::Matchers::ContainsSubstring( "plan-missing" ) );
    }
    SECTION( "unknown dependent" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-missing", "plan-A" ) } );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "unknown" ) );
    }
    SECTION( "ambiguous test name as prerequisite" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-A", "plan-dup" ) } );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "ambiguous" ) &&
                    Catch::Matchers::ContainsSubstring( "plan-dup" ) );
    }
    SECTION( "ambiguous test name as dependent" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-dup" ),
            { dep( "plan-dup", "plan-A" ) } );

        REQUIRE( !plan.ok() );
        REQUIRE( plan.errors.size() == 1 );
        CHECK_THAT( plan.errors[0],
                    Catch::Matchers::ContainsSubstring( "ambiguous" ) );
    }
    SECTION( "duplicated declarations are collapsed" ) {
        auto plan = Catch::planTestRun(
            tests.all(),
            Catch::parseTestSpec( "plan-A" ),
            { dep( "plan-A", "plan-A" ), dep( "plan-A", "plan-A" ) } );

        REQUIRE( !plan.ok() );
        CHECK( plan.errors.size() == 1 );
    }
}

TEST_CASE( "planTestRun can use globally registered dependencies",
           "[test-plan][approvals]" ) {
    FakeTests tests;
    tests.add( "plan-registry-A" );
    tests.add( "plan-registry-B" );

    // "plan-registry-A" -> "plan-registry-B" is registered through
    // CATCH_TEST_DEPENDENCY at the top of this file
    auto plan = Catch::planTestRun(
        tests.all(), Catch::parseTestSpec( "plan-registry-A" ) );

    REQUIRE( plan.ok() );
    CHECK( layerNames( plan ) ==
           std::vector<std::vector<std::string>>{
               { "plan-registry-B" }, { "plan-registry-A" } } );
}
