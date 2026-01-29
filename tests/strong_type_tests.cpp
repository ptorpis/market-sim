#include <gtest/gtest.h>

#include "utils/types.hpp"

// =============================================================================
// Arithmetic Operators
// =============================================================================

TEST(StrongTypeTest, AdditionOperator) {
    EXPECT_EQ(Quantity{100} + Quantity{50}, Quantity{150});
    EXPECT_EQ(Price{1000} + Price{500}, Price{1500});
    EXPECT_EQ(Timestamp{10} + Timestamp{5}, Timestamp{15});
}

TEST(StrongTypeTest, SubtractionOperator) {
    EXPECT_EQ(Quantity{100} - Quantity{30}, Quantity{70});
    EXPECT_EQ(Price{1000} - Price{100}, Price{900});
    EXPECT_EQ(Timestamp{50} - Timestamp{20}, Timestamp{30});
}

TEST(StrongTypeTest, MultiplicationOperator) {
    EXPECT_EQ(Quantity{10} * Quantity{5}, Quantity{50});
    EXPECT_EQ(Price{100} * Price{3}, Price{300});
}

TEST(StrongTypeTest, DivisionOperator) {
    EXPECT_EQ(Quantity{100} / Quantity{5}, Quantity{20});
    EXPECT_EQ(Price{1000} / Price{4}, Price{250});
    // Integer division
    EXPECT_EQ(Quantity{10} / Quantity{3}, Quantity{3});
}

// =============================================================================
// Compound Assignment Operators
// =============================================================================

TEST(StrongTypeTest, AddAssignWithStrongType) {
    Quantity q{100};
    q += Quantity{50};
    EXPECT_EQ(q, Quantity{150});
}

TEST(StrongTypeTest, SubAssignWithStrongType) {
    Quantity q{100};
    q -= Quantity{30};
    EXPECT_EQ(q, Quantity{70});
}

TEST(StrongTypeTest, AddAssignWithBaseValue) {
    Quantity q{100};
    q += 25;
    EXPECT_EQ(q, Quantity{125});
}

TEST(StrongTypeTest, SubAssignWithBaseValue) {
    Quantity q{100};
    q -= 25;
    EXPECT_EQ(q, Quantity{75});
}

// =============================================================================
// Increment/Decrement Operators
// =============================================================================

TEST(StrongTypeTest, PreIncrement) {
    OrderID id{5};
    OrderID& ref = ++id;
    EXPECT_EQ(id, OrderID{6});
    EXPECT_EQ(&ref, &id);
}

TEST(StrongTypeTest, PostIncrement) {
    OrderID id{5};
    OrderID old = id++;
    EXPECT_EQ(old, OrderID{5});
    EXPECT_EQ(id, OrderID{6});
}

TEST(StrongTypeTest, PreDecrement) {
    OrderID id{5};
    OrderID& ref = --id;
    EXPECT_EQ(id, OrderID{4});
    EXPECT_EQ(&ref, &id);
}

TEST(StrongTypeTest, PostDecrement) {
    OrderID id{5};
    OrderID old = id--;
    EXPECT_EQ(old, OrderID{5});
    EXPECT_EQ(id, OrderID{4});
}

// =============================================================================
// Comparison Operators
// =============================================================================

TEST(StrongTypeTest, Equality) {
    EXPECT_TRUE(Price{100} == Price{100});
    EXPECT_FALSE(Price{100} == Price{200});
}

TEST(StrongTypeTest, Inequality) {
    EXPECT_TRUE(Price{100} != Price{200});
    EXPECT_FALSE(Price{100} != Price{100});
}

TEST(StrongTypeTest, LessThan) {
    EXPECT_TRUE(Price{100} < Price{200});
    EXPECT_FALSE(Price{200} < Price{100});
    EXPECT_FALSE(Price{100} < Price{100});
}

TEST(StrongTypeTest, GreaterThan) {
    EXPECT_TRUE(Price{200} > Price{100});
    EXPECT_FALSE(Price{100} > Price{200});
}

TEST(StrongTypeTest, LessThanOrEqual) {
    EXPECT_TRUE(Price{100} <= Price{200});
    EXPECT_TRUE(Price{100} <= Price{100});
    EXPECT_FALSE(Price{200} <= Price{100});
}

TEST(StrongTypeTest, GreaterThanOrEqual) {
    EXPECT_TRUE(Price{200} >= Price{100});
    EXPECT_TRUE(Price{100} >= Price{100});
    EXPECT_FALSE(Price{100} >= Price{200});
}

TEST(StrongTypeTest, ComparisonWithBaseType) {
    EXPECT_TRUE(Price{100} == 100);
    EXPECT_TRUE(Price{100} < 200);
    EXPECT_TRUE(Price{200} > 100);
}

// =============================================================================
// is_zero and Logical NOT
// =============================================================================

TEST(StrongTypeTest, IsZero) {
    EXPECT_TRUE(Quantity{0}.is_zero());
    EXPECT_FALSE(Quantity{1}.is_zero());
    EXPECT_FALSE(Quantity{100}.is_zero());
}

TEST(StrongTypeTest, LogicalNot) {
    EXPECT_TRUE(!Quantity{0});
    EXPECT_FALSE(!Quantity{1});
    EXPECT_FALSE(!Quantity{100});
}

// =============================================================================
// Value Accessor
// =============================================================================

TEST(StrongTypeTest, ValueAccessor) {
    Price p{12345};
    EXPECT_EQ(p.value(), 12345ULL);
}

TEST(StrongTypeTest, ExplicitConversion) {
    Price p{12345};
    auto raw = static_cast<std::uint64_t>(p);
    EXPECT_EQ(raw, 12345ULL);
}

// =============================================================================
// Default Construction
// =============================================================================

TEST(StrongTypeTest, DefaultConstruction) {
    Quantity q;
    EXPECT_EQ(q.value(), 0ULL);
    EXPECT_TRUE(q.is_zero());
}

// =============================================================================
// Different Strong Types
// =============================================================================

TEST(StrongTypeTest, AllTypesWork) {
    Timestamp ts{100};
    Price pr{200};
    Quantity qt{300};
    OrderID oid{400};
    InstrumentID iid{500};
    TradeID tid{600};
    ClientID cid{700};
    EventSequenceNumber esn{800};
    Cash cash{-100};

    EXPECT_EQ(ts.value(), 100ULL);
    EXPECT_EQ(pr.value(), 200ULL);
    EXPECT_EQ(qt.value(), 300ULL);
    EXPECT_EQ(oid.value(), 400ULL);
    EXPECT_EQ(iid.value(), 500U);
    EXPECT_EQ(tid.value(), 600ULL);
    EXPECT_EQ(cid.value(), 700ULL);
    EXPECT_EQ(esn.value(), 800ULL);
    EXPECT_EQ(cash.value(), -100LL);
}

TEST(StrongTypeTest, CashSignedArithmetic) {
    Cash c1{100};
    Cash c2{-50};
    EXPECT_EQ((c1 + c2).value(), 50LL);
    EXPECT_EQ((c2 - c1).value(), -150LL);
}
