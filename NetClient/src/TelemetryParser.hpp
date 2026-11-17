#ifndef TELEMETRY_PARSER_HPP
#define TELEMETRY_PARSER_HPP

#include <charconv>
#include <cstdint>
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

    }

    /**
     * @brief Extracts the value field from "id,timestamp,channel,value" and scales it.
     * @return std::nullopt if the line does not have exactly 4 fields or the value is invalid.
     */
    static std::optional<int64_t> ParseLine(std::string_view line, int scaleDigits) {

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
};

#endif // TELEMETRY_PARSER_HPP