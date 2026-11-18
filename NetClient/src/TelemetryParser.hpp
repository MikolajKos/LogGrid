#ifndef TELEMETRY_PARSER_HPP
#define TELEMETRY_PARSER_HPP

#include <charconv>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

/**
 * @brief Parses telemetry CSV lines "id,timestamp,channel,value" into
 *        fixed-point integers.
 *
 * The value is returned as value * 10^scaleDigits in an int64_t, so that
 * values can be summed exactly as integers instead of as floating point.
 * All functions are static, allocate nothing, and report every kind of
 * invalid input the same way: std::nullopt.
 */
class TelemetryParser {
public:
    /**
     * @brief Converts a decimal string to an integer scaled by 10^scaleDigits.
     *
     * Examples with scaleDigits = 7:
     *  - "0.7188446" -> 7188446
     *  - "-112.23"   -> -1122300000 (the fraction is padded with zeros)
     *  - "5"         -> 50000000
     *
     * Accepted format: optional '-', one or more digits, then optionally
     * '.' followed by one to scaleDigits digits. Fractions longer than
     * scaleDigits are rejected, not rounded.
     *
     * The accepted range is symmetric: [-INT64_MAX, INT64_MAX] after scaling.
     * INT64_MIN is not supported.
     *
     * @param text        Decimal number, no whitespace.
     * @param scaleDigits Number of fraction digits kept, 0 to 18.
     * @return The scaled value, or std::nullopt if scaleDigits is out of range,
     *         the text is not in the accepted format (e.g. "5.", ".5", "+5",
     *         "1e3"), has more than scaleDigits fraction digits, or the scaled
     *         value does not fit in int64_t.
     */
    static std::optional<int64_t> ParseScaledValue(std::string_view text, int scaleDigits) {
        // 10^scaleDigits must be in kPow10, otherwise the lookups below go out of bounds
        if (scaleDigits < 0 || static_cast<size_t>(scaleDigits) >= std::size(kPow10)) {
            return std::nullopt;
        }

        if (text.empty()) return std::nullopt;

        bool negative = false;
        if (text.front() == '-') {
            negative = true;
            text.remove_prefix(1);
        }

        size_t dot = text.find('.');

        std::string_view intPart = text;
        std::string_view fracPart;
        
        if (dot != std::string_view::npos) {
            intPart = text.substr(0, dot);
            fracPart = text.substr(dot + 1);

            // example: 5. is invalid
            if (fracPart.empty()) return std::nullopt;

        }

        if (fracPart.size() > static_cast<size_t>(scaleDigits)) {
            return std::nullopt;
        }

        uint64_t fracValue = 0;
        if (!fracPart.empty()) {
            auto parsed = ParseDigits(fracPart);
            if (!parsed) return std::nullopt;
            fracValue = *parsed;
        }

        auto intValue = ParseDigits(intPart);

        if (!intValue) return std::nullopt;

        static constexpr uint64_t kMax = INT64_MAX;
        const uint64_t scale = kPow10[scaleDigits];
        const uint64_t fracScaled = fracValue * kPow10[scaleDigits - fracPart.size()];

        if (*intValue > (kMax - fracScaled) / scale) return std::nullopt;

        const int64_t result = static_cast<int64_t>(*intValue * scale + fracScaled);

        return negative ? -result : result;
    }

    /**
     * @brief Parses a non-empty string of decimal digits as uint64_t.
     *
     * Helper for ParseScaledValue, used for the integer and the fraction part
     * after the sign has been removed. The unsigned target type makes
     * std::from_chars reject a second '-' (e.g. "--5").
     *
     * @return The value, or std::nullopt if the text is empty, contains
     *         anything other than digits, or does not fit in uint64_t.
     */
    static std::optional<uint64_t> ParseDigits(std::string_view text) {
        // unsigned, so from_chars will set error for '-', because we already read sign
        uint64_t value = 0;
        const char* end = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(text.data(), end, value);

        if (ec != std::errc{} || ptr != end) return std::nullopt;

        return value;
    }

    /**
     * @brief Parses one telemetry line "id,timestamp,channel,value".
     *
     * Takes the field at kValueColumn with GetField and converts it with
     * ParseScaledValue. The other fields are not validated.
     *
     * @param line        One CSV line without the trailing newline.
     * @param scaleDigits Passed to ParseScaledValue.
     * @return The scaled value, or std::nullopt if the line does not have
     *         exactly kFieldCount fields or the value is invalid.
     */
    static std::optional<int64_t> ParseLine(std::string_view line, int scaleDigits) {
        auto value = GetField(line, kValueColumn, kFieldCount);

        if (!value) return std::nullopt;

        return ParseScaledValue(*value, scaleDigits);
    }

    /**
     * @brief Returns one field of a comma-separated row without copying.
     *
     * The row must have exactly fieldCount fields. Quoting and escaped commas
     * are not supported. Empty fields are allowed and returned as an empty view.
     *
     * @param row        CSV row without the trailing newline.
     * @param column     Zero-based index of the field to return.
     * @param fieldCount Exact number of fields the row must have.
     * @return A view into row, or std::nullopt if column >= fieldCount or the
     *         row has a different number of fields.
     */
    static std::optional<std::string_view> GetField(std::string_view row, size_t column, size_t fieldCount) {
        if (column >= fieldCount) {
            return std::nullopt;
        }
        
        std::optional<std::string_view> result;
        size_t start = 0;
    
        for (size_t i = 0; i < fieldCount; ++i) {
            bool isLast = (i == fieldCount - 1);
            size_t comma = row.find(',', start);

            if (isLast != (comma == std::string_view::npos)) {
                return std::nullopt;
            }
            
            size_t end = isLast ? row.size() : comma;

            if (i == column) {
                result = row.substr(start, end - start);
            }
            
            start = end + 1;
        }

        return result;
    }

public:
    /// Number of fields in a telemetry line: id, timestamp, channel, value.
    static constexpr size_t kFieldCount = 4;
    /// Zero-based index of the value field.
    static constexpr size_t kValueColumn = 3;

    /// Powers of ten used for scaling; its size limits scaleDigits to 0-18.

    static constexpr int64_t kPow10[] = {
        1, 10, 100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000,
        100'000'000, 1'000'000'000, 10'000'000'000, 100'000'000'000,
        1'000'000'000'000, 10'000'000'000'000, 100'000'000'000'000,
        1'000'000'000'000'000, 10'000'000'000'000'000,
        100'000'000'000'000'000, 1'000'000'000'000'000'000
    }; // 10^0 - 10^18
};

#endif // TELEMETRY_PARSER_HPP