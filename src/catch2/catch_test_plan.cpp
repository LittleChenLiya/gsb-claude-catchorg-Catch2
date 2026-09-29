
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#include <catch2/catch_test_plan.hpp>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/catch_test_spec.hpp>
#include <catch2/internal/catch_move_and_forward.hpp>
#include <catch2/internal/catch_reusable_string_stream.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace Catch {

    namespace {
        std::vector<TestDependency>& dependencyStore() {
            static std::vector<TestDependency> store;
            return store;
        }
    } // end unnamed namespace

    void registerTestDependency( StringRef dependent, StringRef prerequisite ) {
        dependencyStore().push_back(
            { static_cast<std::string>( dependent ),
              static_cast<std::string>( prerequisite ) } );
    }

    std::vector<TestDependency> const& getRegisteredTestDependencies() {
        return dependencyStore();
    }

    TestDependencyAutoReg::TestDependencyAutoReg( StringRef dependent,
                                                  StringRef prerequisite ) {
        registerTestDependency( dependent, prerequisite );
    }

    TestPlan planTestRun( std::vector<TestCaseInfo const*> const& allTests,
                          TestSpec const& testSpec,
                          std::vector<TestDependency> const& dependencies ) {
        TestPlan plan;

        // Index all known tests by name, keeping declaration order
        std::map<std::string, std::vector<TestCaseInfo const*>> testsByName;
        for ( auto const* test : allTests ) {
            testsByName[test->name].push_back( test );
        }

        // Drop duplicated declarations, e.g. from a dependency registered
        // in a header included into multiple TUs
        std::vector<TestDependency> uniqueDependencies;
        for ( auto const& dep : dependencies ) {
            const auto it = std::find_if(
                uniqueDependencies.begin(),
                uniqueDependencies.end(),
                [&]( TestDependency const& kept ) {
                    return kept.dependent == dep.dependent &&
                           kept.prerequisite == dep.prerequisite;
                } );
            if ( it == uniqueDependencies.end() ) {
                uniqueDependencies.push_back( dep );
            }
        }

        // Validate the declarations. A dependency has to unambiguously
        // refer to registered test cases, and must not be trivially cyclic.
        for ( auto const& dep : uniqueDependencies ) {
            if ( dep.dependent == dep.prerequisite ) {
                ReusableStringStream rss;
                rss << "Test case '" << dep.dependent
                    << "' depends on itself";
                plan.errors.push_back( rss.str() );
                continue;
            }
            for ( auto const& name : { dep.dependent, dep.prerequisite } ) {
                const auto it = testsByName.find( name );
                if ( it == testsByName.end() ) {
                    ReusableStringStream rss;
                    rss << "Test dependency refers to unknown test case '"
                        << name << '\'';
                    plan.errors.push_back( rss.str() );
                } else if ( it->second.size() > 1 ) {
                    ReusableStringStream rss;
                    rss << "Test dependency refers to ambiguous test case '"
                        << name << "': " << it->second.size()
                        << " test cases share this name";
                    plan.errors.push_back( rss.str() );
                }
            }
        }
        if ( !plan.errors.empty() ) {
            return plan;
        }

        // Adjacency lookup: test -> its prerequisites
        std::map<TestCaseInfo const*, std::vector<TestCaseInfo const*>>
            prerequisites;
        for ( auto const& dep : uniqueDependencies ) {
            prerequisites[testsByName[dep.dependent].front()].push_back(
                testsByName[dep.prerequisite].front() );
        }

        // The initial selection mirrors what filtering with the test spec
        // would select: with no filters all non-hidden tests are selected,
        // otherwise the tests matching the spec's filters.
        std::vector<TestCaseInfo const*> selected;
        for ( auto const* test : allTests ) {
            if ( testSpec.hasFilters() ? testSpec.matches( *test )
                                       : !test->isHidden() ) {
                selected.push_back( test );
            }
        }

        // Close the selection over the declared dependencies. Required
        // prerequisites are pulled in even if they are hidden, but a
        // prerequisite that the test spec explicitly excludes is a
        // conflict and is reported instead of being silently added back.
        std::set<TestCaseInfo const*> inPlan( selected.begin(),
                                              selected.end() );
        std::vector<TestCaseInfo const*> worklist = selected;
        while ( !worklist.empty() ) {
            auto const* test = worklist.back();
            worklist.pop_back();
            const auto it = prerequisites.find( test );
            if ( it == prerequisites.end() ) {
                continue;
            }
            for ( auto const* prerequisite : it->second ) {
                if ( inPlan.count( prerequisite ) != 0 ) {
                    continue;
                }
                if ( testSpec.isExplicitlyExcluded( *prerequisite ) ) {
                    ReusableStringStream rss;
                    rss << "Test case '" << test->name << "' requires '"
                        << prerequisite->name
                        << "', but it is excluded by the test spec";
                    plan.errors.push_back( rss.str() );
                    continue;
                }
                inPlan.insert( prerequisite );
                worklist.push_back( prerequisite );
            }
        }
        if ( !plan.errors.empty() ) {
            return plan;
        }

        // Layer the planned tests so that every test's prerequisites are
        // in strictly earlier layers. Tests within a layer keep their
        // relative order from allTests, keeping the plan stable.
        std::set<TestCaseInfo const*> placed;
        std::vector<TestCaseInfo const*> remaining;
        for ( auto const* test : allTests ) {
            if ( inPlan.count( test ) != 0 ) {
                remaining.push_back( test );
            }
        }
        while ( !remaining.empty() ) {
            std::vector<TestCaseInfo const*> layer;
            std::vector<TestCaseInfo const*> notReady;
            for ( auto const* test : remaining ) {
                bool ready = true;
                const auto it = prerequisites.find( test );
                if ( it != prerequisites.end() ) {
                    for ( auto const* prerequisite : it->second ) {
                        if ( inPlan.count( prerequisite ) != 0 &&
                             placed.count( prerequisite ) == 0 ) {
                            ready = false;
                            break;
                        }
                    }
                }
                ( ready ? layer : notReady ).push_back( test );
            }
            if ( layer.empty() ) {
                std::vector<std::string> names;
                names.reserve( notReady.size() );
                for ( auto const* test : notReady ) {
                    names.push_back( test->name );
                }
                std::sort( names.begin(), names.end() );
                names.erase(
                    std::unique( names.begin(), names.end() ),
                    names.end() );
                ReusableStringStream rss;
                rss << "Dependency cycle detected between test cases: ";
                bool first = true;
                for ( auto const& name : names ) {
                    if ( !first ) {
                        rss << ", ";
                    }
                    rss << '\'' << name << '\'';
                    first = false;
                }
                plan.errors.push_back( rss.str() );
                plan.layers.clear();
                return plan;
            }
            for ( auto const* test : layer ) {
                placed.insert( test );
            }
            plan.layers.push_back( CATCH_MOVE( layer ) );
            remaining = CATCH_MOVE( notReady );
        }

        return plan;
    }

    TestPlan planTestRun( std::vector<TestCaseInfo const*> const& allTests,
                          TestSpec const& testSpec ) {
        return planTestRun(
            allTests, testSpec, getRegisteredTestDependencies() );
    }

} // namespace Catch
