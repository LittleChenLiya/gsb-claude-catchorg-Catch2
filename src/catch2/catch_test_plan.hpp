
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0
#ifndef CATCH_TEST_PLAN_HPP_INCLUDED
#define CATCH_TEST_PLAN_HPP_INCLUDED

#include <catch2/internal/catch_compiler_capabilities.hpp>
#include <catch2/internal/catch_stringref.hpp>
#include <catch2/internal/catch_unique_name.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Catch {

    struct TestCaseInfo;
    class TestSpec;

    /**
     * A declared dependency between two test cases.
     *
     * Both test cases are identified by their full test names, as
     * registered through \c TEST_CASE and friends. \c dependent may only
     * run after \c prerequisite has run.
     */
    struct TestDependency {
        std::string dependent;
        std::string prerequisite;
    };

    /**
     * Registers a dependency between two test cases.
     *
     * Dependencies are declared with full test names and are used by
     * \c planTestRun to pull required prerequisite tests into the
     * execution plan.
     */
    void registerTestDependency( StringRef dependent, StringRef prerequisite );

    //! Returns all dependencies registered so far.
    std::vector<TestDependency> const& getRegisteredTestDependencies();

    /**
     * Registrar for static (registration-time) test dependency declaration.
     *
     * You should prefer the \c TEST_CASE_DEPENDENCY macro over using
     * this type directly.
     */
    struct TestDependencyAutoReg {
        TestDependencyAutoReg( StringRef dependent, StringRef prerequisite );
    };

    /**
     * The result of planning a dependency-aware test run.
     *
     * The plan is a stable, layered execution order: tests in the same
     * layer have no dependencies between them, and every test appears
     * after all of its prerequisites (which are in earlier layers).
     *
     * If \c errors is not empty, no valid plan could be created and
     * \c layers is empty.
     */
    struct TestPlan {
        //! Tests to execute, layer by layer. Layers are ordered so that
        //! a test's prerequisites are always in earlier layers.
        std::vector<std::vector<TestCaseInfo const*>> layers;
        //! Problems found while planning (cycles, unknown or ambiguous
        //! test names, conflicts with the test spec, ...).
        std::vector<std::string> errors;

        //! True if a valid plan was created.
        bool ok() const { return errors.empty(); }
        //! Total number of tests in the plan.
        std::size_t testCount() const {
            std::size_t count = 0;
            for ( auto const& layer : layers ) {
                count += layer.size();
            }
            return count;
        }
    };

    /**
     * Creates a layered execution plan from a test spec and explicit
     * test dependencies, without running any tests.
     *
     * The initial selection follows the same rules as filtering tests
     * with \c testSpec (hidden tests are only selected when matched by
     * the spec's filters). Afterwards, all prerequisites of selected
     * tests are pulled into the plan transitively -- including hidden
     * tests, which are normally skipped.
     *
     * A prerequisite that is explicitly excluded by the test spec (e.g.
     * through \c ~[tag] or \c ~"name") is *not* silently added back;
     * instead the plan fails with a conflict error.
     *
     * Tests within one layer keep the relative order in which they
     * appear in \c allTests, making the resulting plan stable.
     */
    TestPlan planTestRun( std::vector<TestCaseInfo const*> const& allTests,
                          TestSpec const& testSpec,
                          std::vector<TestDependency> const& dependencies );

    //! Overload that uses the globally registered test dependencies.
    TestPlan planTestRun( std::vector<TestCaseInfo const*> const& allTests,
                          TestSpec const& testSpec );

} // namespace Catch

#define CATCH_TEST_DEPENDENCY( dependent, prerequisite )               \
    CATCH_INTERNAL_START_WARNINGS_SUPPRESSION                          \
    CATCH_INTERNAL_SUPPRESS_GLOBALS_WARNINGS                           \
    CATCH_INTERNAL_SUPPRESS_UNUSED_VARIABLE_WARNINGS                   \
    namespace {                                                        \
        const Catch::TestDependencyAutoReg INTERNAL_CATCH_UNIQUE_NAME( \
            autoDependencyRegistrar )( dependent, prerequisite );      \
    }                                                                  \
    CATCH_INTERNAL_STOP_WARNINGS_SUPPRESSION

#if !defined( CATCH_CONFIG_PREFIX_ALL )
#    define TEST_CASE_DEPENDENCY( dependent, prerequisite ) \
        CATCH_TEST_DEPENDENCY( dependent, prerequisite )
#endif

#endif // CATCH_TEST_PLAN_HPP_INCLUDED
