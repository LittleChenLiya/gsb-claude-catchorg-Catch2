
//              Copyright Catch2 Authors
// Distributed under the Boost Software License, Version 1.0.
//   (See accompanying file LICENSE.txt or copy at
//        https://www.boost.org/LICENSE_1_0.txt)

// SPDX-License-Identifier: BSL-1.0

#include <catch2/internal/catch_compiler_capabilities.hpp>
#include <catch2/internal/catch_parse_numbers.hpp>
#include <catch2/internal/catch_string_manip.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace Catch {

    Optional<unsigned int> parseUInt(std::string const& input, int base) {
        auto trimmed = trim( input );
        // std::stoull is annoying and accepts numbers starting with '-',
        // it just negates them into unsigned int
        if ( trimmed.empty() || trimmed[0] == '-' ) {
            return {};
        }

        CATCH_TRY {
            size_t pos = 0;
            const auto ret = std::stoull( trimmed, &pos, base );

            // We did not consume the whole input, so there is an issue
            // This can be bunch of different stuff, like multiple numbers
            // in the input, or invalid digits/characters and so on. Either
            // way, we do not want to return the partially parsed result.
            if ( pos != trimmed.size() ) {
                return {};
            }
            // Too large
            if ( ret > std::numeric_limits<unsigned int>::max() ) {
                return {};
            }
            return static_cast<unsigned int>(ret);
        }
        CATCH_CATCH_ANON( std::invalid_argument const& ) {
            // no conversion could be performed
        }
        CATCH_CATCH_ANON( std::out_of_range const& ) {
            // the input does not fit into an unsigned long long
        }
        return {};
    }

    Optional<double> parseDouble(std::string const& input) {
        auto trimmed = trim( input );
        if ( trimmed.empty() ) {
            return {};
        }

        CATCH_TRY {
            size_t pos = 0;
            const auto ret = std::stod( trimmed, &pos );

            // We did not consume the whole input, so there is an issue
            if ( pos != trimmed.size() ) {
                return {};
            }
            // Non-finite values (inf, nan) are not accepted
            if ( !std::isfinite( ret ) ) {
                return {};
            }
            return ret;
        }
        CATCH_CATCH_ANON( std::invalid_argument const& ) {
            // no conversion could be performed
        }
        CATCH_CATCH_ANON( std::out_of_range const& ) {
            // the input does not fit into a double
        }
        return {};
    }

} // namespace Catch
