#include <catch2/catch_test_macros.hpp>

TEST_CASE("Sanity Check: Catch2 Installation", "[sanity]") {
    REQUIRE(1 == 1);
    REQUIRE(true == true);
}
