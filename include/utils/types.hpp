#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <ostream>
#include <type_traits>

/**
    Strong type that can be used with trivial types. Offers added benefit of type-safety
    over traditional typedefs/using... declarations

    To define a type:

    struct Custom : StrongType<std::uint64_t, Custom> {
        using StrongType::StrongType;
    };

    Custom var = Custom{199};

    Now the compiler won't let the user mix other types unless explicitly requested
*/
template <typename Base, typename Tag> struct StrongType {
    static_assert(std::is_trivial_v<Base>, "The value type must be trivial");
    static_assert(std::is_trivially_copyable_v<Base>);
    static_assert(std::is_standard_layout_v<Base>);
    using value_type = Base;
    using tag_type = Tag;

public:
    explicit StrongType(Base value) noexcept : data_m(value) {}

    [[nodiscard]] constexpr auto value() const noexcept { return data_m; }
    // explicit conversion to underlying (using static_cast)
    explicit constexpr operator Base() const noexcept { return data_m; }

    friend std::ostream& operator<<(std::ostream& os, const StrongType& st) {
        return os << st.data_m;
    }

    constexpr StrongType() noexcept : data_m{} {}

    constexpr auto operator<=>(const StrongType&) const noexcept = default;
    constexpr auto operator<=>(Base other) const noexcept { return data_m <=> other; }
    constexpr bool operator==(Base other) const noexcept { return data_m == other; }
    constexpr bool operator!() const noexcept { return data_m == 0; }
    constexpr bool operator==(const StrongType& other) const noexcept {
        return other.data_m == data_m;
    }
    constexpr bool operator!=(const StrongType& other) const noexcept {
        return other.data_m != data_m;
    }

    constexpr Tag operator+(const Tag& other) const noexcept {
        return Tag{data_m + other.data_m};
    }

    constexpr Tag operator-(const Tag& other) const noexcept {
        return Tag{data_m - other.data_m};
    }

    constexpr Tag operator*(const Tag& other) const noexcept {
        return Tag{data_m * other.data_m};
    }

    constexpr Tag operator/(const Tag& other) const noexcept {
        return Tag{data_m / other.data_m};
    }

    constexpr Tag& operator+=(const Tag& other) noexcept {
        data_m += other.data_m;
        return static_cast<Tag&>(*this);
    }

    constexpr Tag& operator-=(const Tag& other) noexcept {
        data_m -= other.data_m;
        return static_cast<Tag&>(*this);
    }

    constexpr Tag& operator+=(Base value) noexcept {
        data_m += value;
        return static_cast<Tag&>(*this);
    }

    constexpr Tag& operator-=(Base value) noexcept {
        data_m -= value;
        return static_cast<Tag&>(*this);
    }

    constexpr Tag& operator++() noexcept {
        ++data_m;
        return static_cast<Tag&>(*this);
    }

    constexpr Tag operator++(int) noexcept {
        Tag temp = static_cast<Tag&>(*this);
        ++data_m;
        return temp;
    }

    constexpr Tag& operator--() noexcept {
        --data_m;
        return static_cast<Tag&>(*this);
    }

    constexpr Tag operator--(int) noexcept {
        Tag temp = static_cast<Tag&>(*this);
        --data_m;
        return temp;
    }

    [[nodiscard]] constexpr bool is_zero() const noexcept { return data_m == 0; };

protected:
    Base data_m;
};

// Concept to identify StrongType derivatives (checks for tag_type which is unique to
// StrongType)
template <typename T>
concept IsStrongType = requires {
    typename T::value_type;
    typename T::tag_type;
} && std::derived_from<T, StrongType<typename T::value_type, typename T::tag_type>>;

/*
    Specialization of formatter to be able to use the type with std::format,
    std::println, etc...
*/
template <typename Tag>
    requires IsStrongType<Tag> && std::formattable<typename Tag::value_type, char>
struct std::formatter<Tag> : std::formatter<typename Tag::value_type> {
    template <typename FormatContext>
    auto format(const Tag& st, FormatContext& ctx) const {
        return std::formatter<typename Tag::value_type>::format(st.value(), ctx);
    }
};

template <typename Strong> struct strong_hash {
    std::size_t operator()(const Strong& v) const noexcept {
        return std::hash<typename Strong::value_type>{}(v.value());
    }
};

/*
    Define strong types using the underlying and the tag (CRTP)
*/

struct Timestamp : StrongType<std::uint64_t, Timestamp> {
    using StrongType::StrongType;
};

struct Price : StrongType<std::uint64_t, Price> {
    using StrongType::StrongType;
};

struct Quantity : StrongType<std::uint64_t, Quantity> {
    using StrongType::StrongType;
};

struct OrderID : StrongType<std::uint64_t, OrderID> {
    using StrongType::StrongType;
};

struct InstrumentID : StrongType<std::uint32_t, InstrumentID> {
    using StrongType::StrongType;
};

struct TradeID : StrongType<std::uint64_t, TradeID> {
    using StrongType::StrongType;
};

struct ClientID : StrongType<std::uint64_t, ClientID> {
    using StrongType::StrongType;
};

struct EventSequenceNumber : StrongType<std::uint64_t, EventSequenceNumber> {
    using StrongType::StrongType;
};

struct Cash : StrongType<std::int64_t, Cash> {
    using StrongType::StrongType;
};