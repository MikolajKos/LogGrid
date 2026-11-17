#ifndef TELEMETRY_PARSER_HPP
#define TELEMETRY_PARSER_HPP

#include <charconv>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string_view>

class TelemetryParser {
public:
    /**
     * @brief Converts a decimal string to an integer scaled by 10^scaleDigits.
     * "0.7188446", 7 -> 7188446;  "-112.23", 7 -> -1122300000
     * @return std::nullopt if the text is not a valid number or has more
     *         than scaleDigits digits after the dot.
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

            // for example: 5. is invalid
            if (fracPart.empty()) return std::nullopt;
        }

        if (fracPart.size() > static_cast<size_t>(scaleDigits)) {
            return std::nullopt;
        }
        
        return std::nullopt;
    }

    /**
     * @brief Extracts the value field from "id,timestamp,channel,value"
     *        and converts it with ParseScaledValue.
     * @return std::nullopt if the line does not have exactly 4 fields or the value is invalid.
     */
    static std::optional<int64_t> ParseLine(std::string_view line, int scaleDigits) {
        auto value = GetField(line, kValueColumn, kFieldCount);

        if (!value) return std::nullopt;

        return ParseScaledValue(*value, scaleDigits);
    }

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
    static constexpr size_t kFieldCount = 4;
    static constexpr size_t kValueColumn = 3;

    static constexpr int64_t kPow10[] = {
        1, 10, 100, 1'000, 10'000, 100'000, 1'000'000, 10'000'000,
        100'000'000, 1'000'000'000, 10'000'000'000, 100'000'000'000,
        1'000'000'000'000, 10'000'000'000'000, 100'000'000'000'000,
        1'000'000'000'000'000, 10'000'000'000'000'000,
        100'000'000'000'000'000, 1'000'000'000'000'000'000
    }; // 10^0 - 10^18
};

#endif // TELEMETRY_PARSER_HPP