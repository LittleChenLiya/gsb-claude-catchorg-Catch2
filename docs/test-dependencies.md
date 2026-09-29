<a id="top"></a>
# Test dependencies and execution planning

Catch2 test cases are normally independent of each other, but sometimes
a group of integration tests has to run in a specific order. Catch2
provides a **planning-only** interface for this: dependencies between
tests are declared explicitly, and `Catch::planTestRun` combines them
with a test spec to produce a stable, layered execution plan. The test
runner itself is not involved (and not modified) -- consuming the plan
is up to the caller.

## Declaring dependencies

Dependencies are declared with the **full test names** of the involved
test cases. `TEST_CASE_DEPENDENCY(dependent, prerequisite)` registers
that `dependent` may only run after `prerequisite`:

```cpp
TEST_CASE( "A" ) { /* ... */ }
TEST_CASE( "B" ) { /* ... */ }
TEST_CASE( "C" ) { /* ... */ }
TEST_CASE( "D" ) { /* ... */ }

// A depends on B and C, both B and C depend on D
TEST_CASE_DEPENDENCY( "A", "B" )
TEST_CASE_DEPENDENCY( "A", "C" )
TEST_CASE_DEPENDENCY( "B", "D" )
TEST_CASE_DEPENDENCY( "C", "D" )
```

Dependencies can also be registered programmatically with
`Catch::registerTestDependency(dependent, prerequisite)`, and
`Catch::getRegisteredTestDependencies()` returns everything registered
so far.

## Planning a test run

`Catch::planTestRun` takes all known tests, the user's test spec, and
the dependencies, and returns a `Catch::TestPlan`:

```cpp
std::vector<Catch::TestCaseInfo const*> allTests = ...;
Catch::TestSpec spec = ...;

Catch::TestPlan plan = Catch::planTestRun( allTests, spec );
if ( !plan.ok() ) {
    for ( auto const& error : plan.errors ) {
        // report the problem
    }
}
```

An overload taking an explicit
`std::vector<Catch::TestDependency> const&` exists for callers that do
not want to use the global registry.

The planning works in two steps:

1. The test spec is applied with the usual filtering semantics (with no
   filters, all non-hidden tests are selected).
2. The prerequisites of every selected test are pulled into the plan
   transitively. This also pulls in [hidden tests](test-cases-and-sections.md#special-tags),
   which are normally skipped unless explicitly selected.

The resulting `plan.layers` is a stable execution order: every test's
prerequisites are in strictly earlier layers, tests within one layer
have no dependencies between them, and the relative order of tests
within a layer follows their declaration order. With the declarations
above, selecting only `"A"` produces three layers: `D`, then `B` and
`C`, then `A`.

## Error handling

Planning fails (with `plan.ok() == false` and the reasons in
`plan.errors`) when

* a test depends on itself, or the dependencies form a cycle,
* a dependency refers to a test name that is not registered, or to a
  name shared by multiple test cases (ambiguous),
* a required prerequisite is **explicitly excluded** by the test spec
  (e.g. via `~[tag]` or `~"name"`). This is reported as a conflict --
  the prerequisite is *not* silently added back to the plan.

An empty selection is not an error; it produces an empty plan.
