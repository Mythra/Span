#include "gtest/gtest.h"

#include "span/io/streams/Buffer.hh"

namespace {
  TEST(Buffer, copyInString) {
    span::io::streams::Buffer buff;
    buff.copyIn("hello");

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "hello");
  }

  TEST(Buffer, stdStringMechanics) {
    span::io::streams::Buffer buff;
    std::string str("abc\0def", 7);
    buff.copyIn(str);

    ASSERT_EQ(buff.readAvailable(), 7u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == str);
  }

  TEST(Buffer, stringStreamMechanics) {
    span::io::streams::Buffer buff;
    std::ostringstream os;
    os << "hello" << '\0' << "world" << '\0' << '\x0a';
    buff.copyIn(os.str());

    ASSERT_EQ(buff.readAvailable(), 13u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == os.str());
  }

  TEST(Buffer, copyInOtherBuffer) {
    span::io::streams::Buffer buff, buff_two("hello");
    buff.copyIn(&buff_two);

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "hello");
  }

  TEST(Buffer, copyInPartial) {
    span::io::streams::Buffer buff, buff_two("hello");
    buff.copyIn(&buff_two, 3);

    ASSERT_EQ(buff.readAvailable(), 3u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "hel");
  }

  TEST(Buffer, copyInOffset) {
    span::io::streams::Buffer buff, buff_two("hello world");
    buff.copyIn(&buff_two, 7, 2);

    ASSERT_EQ(buff.readAvailable(), 7u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "llo wor");
  }

  TEST(Buffer, copyInOffsetMultiSegment) {
    span::io::streams::Buffer buff, buff_two;
    buff_two.copyIn("hello\n");
    buff_two.copyIn("foo\n");
    buff_two.copyIn("bar\n");
    ASSERT_EQ(buff_two.segments(), 3u);
    buff.copyIn(&buff_two, 5, 7);

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 2u);
    ASSERT_TRUE(buff == "oo\nba");
  }

  TEST(Buffer, copyInToStringReserved) {
    span::io::streams::Buffer buff;
    buff.reserve(5);
    buff.copyIn("hello");

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "hello");
  }

  TEST(Buffer, copyInStringAfterAnotherSegment) {
    span::io::streams::Buffer buff("hello");
    buff.copyIn("world");

    ASSERT_EQ(buff.readAvailable(), 10u);
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff.segments(), 2u);
    ASSERT_TRUE(buff == "helloworld");
  }

  TEST(Buffer, copyInStringToSplitSegment) {
    span::io::streams::Buffer buff;
    buff.reserve(10);
    buff.copyIn("hello");

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_GE(buff.writeAvailable(), 5u);
    ASSERT_EQ(buff.segments(), 1u);

    buff.copyIn("world");

    ASSERT_EQ(buff.readAvailable(), 10u);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "helloworld");
  }

  TEST(Buffer, copyInWithReserve) {
    span::io::streams::Buffer buff, buff_two("hello");
    buff.reserve(10);

    ASSERT_GE(buff.writeAvailable(), 10u);
    ASSERT_EQ(buff.segments(), 1u);

    size_t writeAvailable = buff.writeAvailable();
    buff.copyIn(&buff_two);

    ASSERT_EQ(buff.readAvailable(), 5u);
    // Shouldn't have eaten any
    ASSERT_EQ(buff.writeAvailable(), writeAvailable);
    ASSERT_EQ(buff.segments(), 2u);
    ASSERT_TRUE(buff == "hello");
  }

  TEST(Buffer, copyInToSplitSegment) {
    span::io::streams::Buffer buff, buff_two("world");
    buff.reserve(10);
    buff.copyIn("hello");

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_GE(buff.writeAvailable(), 5u);
    ASSERT_EQ(buff.segments(), 1u);

    size_t writeAvailable = buff.writeAvailable();
    buff.copyIn(&buff_two, 5);

    ASSERT_EQ(buff.readAvailable(), 10u);
    // Shouldn't have eaten any
    ASSERT_EQ(buff.writeAvailable(), writeAvailable);
    ASSERT_EQ(buff.segments(), 3u);
    ASSERT_TRUE(buff == "helloworld");
  }

  TEST(Buffer, copyOutOffset) {
    span::io::streams::Buffer buff("hello world");
    std::string out;
    out.resize(7);
    buff.copyOut(&out[0], 7, 2);

    ASSERT_TRUE(out == "llo wor");
  }

  TEST(Buffer, noSplitOnTruncate) {
    span::io::streams::Buffer buff;
    buff.reserve(10);
    buff.copyIn("hello");
    buff.truncate(5);

    ASSERT_GE(buff.writeAvailable(), 5u);

    buff.copyIn("world");

    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_TRUE(buff == "helloworld");
  }

  TEST(Buffer, copyConstructor) {
    span::io::streams::Buffer buff;
    buff.copyIn("hello");
    span::io::streams::Buffer buff_two(&buff);

    ASSERT_TRUE(buff == "hello");
    ASSERT_TRUE(buff_two == "hello");
    ASSERT_EQ(buff.writeAvailable(), 0u);
    ASSERT_EQ(buff_two.writeAvailable(), 0u);
  }

  TEST(Buffer, copyConstructorImmutability) {
    span::io::streams::Buffer buff;
    buff.reserve(10);
    span::io::streams::Buffer buff_two(&buff);
    buff.copyIn("hello");
    buff_two.copyIn("tommy");

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_GE(buff.writeAvailable(), 5u);
    ASSERT_EQ(buff_two.readAvailable(), 5u);
    ASSERT_EQ(buff_two.writeAvailable(), 0u);
    ASSERT_TRUE(buff == "hello");
    ASSERT_TRUE(buff_two == "tommy");
  }

  TEST(Buffer, truncate) {
    span::io::streams::Buffer buff("hello");
    buff.truncate(3);

    ASSERT_TRUE(buff == "hel");
  }

  TEST(Buffer, truncateMultipleSegmentsOne) {
    span::io::streams::Buffer buff("hello");
    buff.copyIn("world");
    buff.truncate(3);

    ASSERT_TRUE(buff == "hel");
  }

  TEST(Buffer, truncateMultipleSegmentsTwo) {
    span::io::streams::Buffer buff("hello");
    buff.copyIn("world");
    buff.truncate(8);

    ASSERT_TRUE(buff == "hellowor");
  }

  TEST(Buffer, truncateBeforeWriteSegments) {
    span::io::streams::Buffer buff("hello");
    buff.reserve(5);
    buff.truncate(3);

    ASSERT_TRUE(buff == "hel");
    ASSERT_GE(buff.writeAvailable(), 5u);
  }

  TEST(Buffer, truncateAtWriteSegments) {
    span::io::streams::Buffer buff("hello");
    buff.reserve(10);
    buff.copyIn("world");
    buff.truncate(8);

    ASSERT_TRUE(buff == "hellowor");
    ASSERT_GE(buff.writeAvailable(), 10u);
  }

  TEST(Buffer, compareEmpty) {
    span::io::streams::Buffer buff, buff_two;

    ASSERT_TRUE(buff == buff_two);
    ASSERT_FALSE(buff != buff_two);
  }

  TEST(Buffer, compareSimpleInequality) {
    span::io::streams::Buffer buff, buff_two("h");

    ASSERT_TRUE(buff != buff_two);
    ASSERT_FALSE(buff == buff_two);
  }

  TEST(Buffer, compareIdentical) {
    span::io::streams::Buffer buff("hello"), buff_two("hello");

    ASSERT_TRUE(buff == buff_two);
    ASSERT_FALSE(buff != buff_two);
  }

  TEST(Buffer, compareLotsOfSegmentsOnTheLeft) {
    span::io::streams::Buffer buff, buff_two("hello world!");
    buff.copyIn("he");
    buff.copyIn("l");
    buff.copyIn("l");
    buff.copyIn("o wor");
    buff.copyIn("ld!");

    ASSERT_TRUE(buff == buff_two);
    ASSERT_FALSE(buff != buff_two);
  }

  TEST(Buffer, compareLotsOfSegmentsOnTheRight) {
    span::io::streams::Buffer buff("hello world!"), buff_two;
    buff_two.copyIn("he");
    buff_two.copyIn("l");
    buff_two.copyIn("l");
    buff_two.copyIn("o wor");
    buff_two.copyIn("ld!");

    ASSERT_TRUE(buff == buff_two);
    ASSERT_FALSE(buff != buff_two);
  }

  TEST(Buffer, compareLotsOfSegments) {
    span::io::streams::Buffer buff, buff_two;

    buff.copyIn("he");
    buff.copyIn("l");
    buff.copyIn("l");
    buff.copyIn("o wor");
    buff.copyIn("ld!");

    buff_two.copyIn("he");
    buff_two.copyIn("l");
    buff_two.copyIn("l");
    buff_two.copyIn("o wor");
    buff_two.copyIn("ld!");

    ASSERT_TRUE(buff == buff_two);
    ASSERT_FALSE(buff != buff_two);
  }

  TEST(Buffer, compareLotsOfMismatchedSegments) {
    span::io::streams::Buffer buff, buff_two;

    buff.copyIn("hel");
    buff.copyIn("lo ");
    buff.copyIn("wo");
    buff.copyIn("rld!");

    buff_two.copyIn("he");
    buff_two.copyIn("l");
    buff_two.copyIn("l");
    buff_two.copyIn("o wor");
    buff_two.copyIn("ld!");

    ASSERT_TRUE(buff == buff_two);
    ASSERT_FALSE(buff != buff_two);
  }

  TEST(Buffer, compareLotsOfSegmentsOnTheLeftInequality) {
    span::io::streams::Buffer buff, buff_two("hello world!");

    buff.copyIn("he");
    buff.copyIn("l");
    buff.copyIn("l");
    buff.copyIn("o wor");
    buff.copyIn("ld! ");

    ASSERT_TRUE(buff != buff_two);
    ASSERT_FALSE(buff == buff_two);
  }

  TEST(Buffer, compareLotOfSegmentsOnTheRightInequality) {
    span::io::streams::Buffer buff("hello world!"), buff_two;

    buff_two.copyIn("he");
    buff_two.copyIn("l");
    buff_two.copyIn("l");
    buff_two.copyIn("o wor");
    buff_two.copyIn("ld! ");

    ASSERT_TRUE(buff != buff_two);
    ASSERT_FALSE(buff == buff_two);
  }

  TEST(Buffer, compareLotsOfSegmentsInequality) {
    span::io::streams::Buffer buff, buff_two;

    buff.copyIn("he");
    buff.copyIn("l");
    buff.copyIn("l");
    buff.copyIn("o wor");
    buff.copyIn("ld!");

    buff_two.copyIn("he");
    buff_two.copyIn("l");
    buff_two.copyIn("l");
    buff_two.copyIn("o wor");
    buff_two.copyIn("ld! ");

    ASSERT_TRUE(buff != buff_two);
    ASSERT_FALSE(buff == buff_two);
  }

  TEST(Buffer, reserveWithReadAvailable) {
    span::io::streams::Buffer buff("hello");
    buff.reserve(10);

    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_GE(buff.writeAvailable(), 10u);
  }

  TEST(Buffer, reserveWithWriteAvailable) {
    span::io::streams::Buffer buff;
    buff.reserve(5);
    // Internal knowledge that reserve doubles the reservation.
    ASSERT_EQ(buff.writeAvailable(), 10u);
    buff.reserve(11);
    ASSERT_EQ(buff.writeAvailable(), 22u);
  }

  TEST(Buffer, reserveWithReadAndWriteAvailable) {
    span::io::streams::Buffer buff("hello");
    buff.reserve(5);
    // Internal knowledge that reserve doubles the reservation.
    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_EQ(buff.writeAvailable(), 10u);
    buff.reserve(11);
    ASSERT_EQ(buff.readAvailable(), 5u);
    ASSERT_EQ(buff.writeAvailable(), 22u);
  }

  static void visitorOne(const void *buff, size_t len) {
    throw std::runtime_error("Not reached!");
  }

  TEST(Buffer, visitEmpty) {
    span::io::streams::Buffer buff;
    buff.visit(&visitorOne);
  }

  TEST(Buffer, visitNonEmptyZero) {
    span::io::streams::Buffer buff;
    buff.visit(&visitorOne, 0);
  }

  static void visitorTwo(const void *buff, size_t len, int *sequence) {
    ASSERT_EQ(++*sequence, 1);
    ASSERT_EQ(len, 5u);
    ASSERT_EQ(memcmp(buff, "hello", 5), 0);
  }

  TEST(Buffer, visitSingleSegment) {
    span::io::streams::Buffer buff("hello");
    int sequence = 0;

    buff.visit(std::bind(&visitorTwo, std::placeholders::_1, std::placeholders::_2, &sequence));
    ASSERT_EQ(++sequence, 2);
  }

  static void visitorThree(const void *buff, size_t len, int *sequence) {
    switch (len) {
      case 1:
        ASSERT_EQ(++*sequence, 1);
        ASSERT_EQ(memcmp(buff, "a", 1), 0);
        break;
      case 2:
        ASSERT_EQ(++*sequence, 2);
        ASSERT_EQ(memcmp(buff, "bc", 2), 0);
        break;
      default:
        throw std::runtime_error("Not reached!");
    }
  }

  TEST(Buffer, visitMultipleSegments) {
    span::io::streams::Buffer buff;
    int sequence = 0;
    buff.copyIn("a");
    buff.copyIn("bc");
    buff.visit(std::bind(&visitorThree, std::placeholders::_1, std::placeholders::_2, &sequence));

    ASSERT_EQ(++sequence, 3);
  }

  TEST(Buffer, visitMultipleSegmentsPartial) {
    span::io::streams::Buffer buff;
    int sequence = 0;
    buff.copyIn("a");
    buff.copyIn("bcd");
    buff.visit(std::bind(&visitorThree, std::placeholders::_1, std::placeholders::_2, &sequence), 3);

    ASSERT_EQ(++sequence, 3);
  }

  TEST(Buffer, visitWithWriteSegment) {
    span::io::streams::Buffer buff("hello");
    buff.reserve(5);
    int sequence = 0;
    buff.visit(std::bind(&visitorTwo, std::placeholders::_1, std::placeholders::_2, &sequence));
    ASSERT_EQ(++sequence, 2);
  }

  TEST(Buffer, visitWithMixedSegment) {
    span::io::streams::Buffer buff;
    buff.reserve(10);
    buff.copyIn("hello");
    int sequence = 0;
    buff.visit(std::bind(&visitorTwo, std::placeholders::_1, std::placeholders::_2, &sequence));

    ASSERT_EQ(++sequence, 2);
  }

  TEST(Buffer, findCharEmpty) {
    span::io::streams::Buffer buff;
    ASSERT_EQ(buff.segments(), 0u);
    ASSERT_EQ(buff.find('\n'), -1);
    ASSERT_EQ(buff.find('\n', 0), -1);

    buff.reserve(10);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_EQ(buff.find('\n'), -1);
  }

  TEST(Buffer, findCharSimple) {
    span::io::streams::Buffer buff("\nhello");

    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_EQ(buff.find('\r'), -1);
    ASSERT_EQ(buff.find('\n'), 0);
    ASSERT_EQ(buff.find('h'), 1);
    ASSERT_EQ(buff.find('e'), 2);
    ASSERT_EQ(buff.find('l'), 3);
    ASSERT_EQ(buff.find('o'), 5);

    ASSERT_EQ(buff.find('\r', 2), -1);
    ASSERT_EQ(buff.find('\n', 2), 0);
    ASSERT_EQ(buff.find('h', 2), 1);
    ASSERT_EQ(buff.find('e', 2), -1);
    ASSERT_EQ(buff.find('l', 2), -1);
    ASSERT_EQ(buff.find('0', 2), -1);

    ASSERT_EQ(buff.find('\n', 0), -1);
  }

  TEST(Buffer, findCharTwoSegments) {
    span::io::streams::Buffer buff("\nhe");
    buff.copyIn("llo");
    ASSERT_EQ(buff.segments(), 2u);

    ASSERT_EQ(buff.find('\r'), -1);
    ASSERT_EQ(buff.find('\n'), 0);
    ASSERT_EQ(buff.find('h'), 1);
    ASSERT_EQ(buff.find('e'), 2);
    ASSERT_EQ(buff.find('l'), 3);
    ASSERT_EQ(buff.find('o'), 5);

    ASSERT_EQ(buff.find('\r', 2), -1);
    ASSERT_EQ(buff.find('\n', 2), 0);
    ASSERT_EQ(buff.find('h', 2), 1);
    ASSERT_EQ(buff.find('e', 2), -1);
    ASSERT_EQ(buff.find('l', 2), -1);
    ASSERT_EQ(buff.find('0', 2), -1);

    ASSERT_EQ(buff.find('\r', 4), -1);
    ASSERT_EQ(buff.find('\n', 4), 0);
    ASSERT_EQ(buff.find('h', 4), 1);
    ASSERT_EQ(buff.find('e', 4), 2);
    ASSERT_EQ(buff.find('l', 4), 3);
    ASSERT_EQ(buff.find('o', 4), -1);

    // Put a write segment on the end.
    buff.reserve(10);
    ASSERT_EQ(buff.segments(), 3u);

    ASSERT_EQ(buff.find('\r'), -1);
    ASSERT_EQ(buff.find('\n'), 0);
    ASSERT_EQ(buff.find('h'), 1);
    ASSERT_EQ(buff.find('e'), 2);
    ASSERT_EQ(buff.find('l'), 3);
    ASSERT_EQ(buff.find('o'), 5);

    ASSERT_EQ(buff.find('\r', 2), -1);
    ASSERT_EQ(buff.find('\n', 2), 0);
    ASSERT_EQ(buff.find('h', 2), 1);
    ASSERT_EQ(buff.find('e', 2), -1);
    ASSERT_EQ(buff.find('l', 2), -1);
    ASSERT_EQ(buff.find('0', 2), -1);

    ASSERT_EQ(buff.find('\r', 4), -1);
    ASSERT_EQ(buff.find('\n', 4), 0);
    ASSERT_EQ(buff.find('h', 4), 1);
    ASSERT_EQ(buff.find('e', 4), 2);
    ASSERT_EQ(buff.find('l', 4), 3);
    ASSERT_EQ(buff.find('o', 4), -1);
  }

  TEST(Buffer, findCharMixedSegment) {
    span::io::streams::Buffer buff("\nhe");
    buff.reserve(10);
    buff.copyIn("llo");
    ASSERT_EQ(buff.segments(), 2u);

    ASSERT_EQ(buff.find('\r'), -1);
    ASSERT_EQ(buff.find('\n'), 0);
    ASSERT_EQ(buff.find('h'), 1);
    ASSERT_EQ(buff.find('e'), 2);
    ASSERT_EQ(buff.find('l'), 3);
    ASSERT_EQ(buff.find('o'), 5);

    ASSERT_EQ(buff.find('\r', 2), -1);
    ASSERT_EQ(buff.find('\n', 2), 0);
    ASSERT_EQ(buff.find('h', 2), 1);
    ASSERT_EQ(buff.find('e', 2), -1);
    ASSERT_EQ(buff.find('l', 2), -1);
    ASSERT_EQ(buff.find('0', 2), -1);

    ASSERT_EQ(buff.find('\r', 4), -1);
    ASSERT_EQ(buff.find('\n', 4), 0);
    ASSERT_EQ(buff.find('h', 4), 1);
    ASSERT_EQ(buff.find('e', 4), 2);
    ASSERT_EQ(buff.find('l', 4), 3);
    ASSERT_EQ(buff.find('o', 4), -1);
  }

  TEST(Buffer, findStringEmpty) {
    span::io::streams::Buffer buff;

    ASSERT_EQ(buff.find("h"), -1);
    ASSERT_EQ(buff.find("h", 0), -1);

    // Put a write segment on the end.
    buff.reserve(10);
    ASSERT_EQ(buff.segments(), 1u);
    ASSERT_EQ(buff.find("h"), -1);
    ASSERT_EQ(buff.find("h", 0), -1);
  }

  TEST(Buffer, findStringSimple) {
    span::io::streams::Buffer buff("helloworld");
    ASSERT_EQ(buff.segments(), 1u);

    ASSERT_EQ(buff.find("abc"), -1);
    ASSERT_EQ(buff.find("helloworld"), 0);
    ASSERT_EQ(buff.find("helloworld2"), -1);
    ASSERT_EQ(buff.find("elloworld"), 1);
    ASSERT_EQ(buff.find("helloworl"), 0);
    ASSERT_EQ(buff.find("h"), 0);
    ASSERT_EQ(buff.find("l"), 2);
    ASSERT_EQ(buff.find("o"), 4);
    ASSERT_EQ(buff.find("lo"), 3);
    ASSERT_EQ(buff.find("d"), 9);

    ASSERT_EQ(buff.find("abc", 5), -1);
    ASSERT_EQ(buff.find("helloworld", 5), -1);
    ASSERT_EQ(buff.find("hello", 5), 0);
    ASSERT_EQ(buff.find("ello", 5), 1);
    ASSERT_EQ(buff.find("helloworld2", 5), -1);
    ASSERT_EQ(buff.find("elloworld", 5), -1);
    ASSERT_EQ(buff.find("hell", 5), 0);
    ASSERT_EQ(buff.find("h", 5), 0);
    ASSERT_EQ(buff.find("l", 5), 2);
    ASSERT_EQ(buff.find("o", 5), 4);
    ASSERT_EQ(buff.find("lo", 5), 3);
    ASSERT_EQ(buff.find("ow", 5), -1);

    ASSERT_EQ(buff.find("h", 0), -1);
  }

  TEST(Buffer, findStringTwoSegments) {
    span::io::streams::Buffer buff("hello");
    buff.copyIn("world");
    ASSERT_EQ(buff.segments(), 2u);

    ASSERT_EQ(buff.find("abc"), -1);
    ASSERT_EQ(buff.find("helloworld"), 0);
    ASSERT_EQ(buff.find("helloworld2"), -1);
    ASSERT_EQ(buff.find("elloworld"), 1);
    ASSERT_EQ(buff.find("helloworl"), 0);
    ASSERT_EQ(buff.find("h"), 0);
    ASSERT_EQ(buff.find("l"), 2);
    ASSERT_EQ(buff.find("o"), 4);
    ASSERT_EQ(buff.find("lo"), 3);
    ASSERT_EQ(buff.find("d"), 9);

    ASSERT_EQ(buff.find("abc", 7), -1);
    ASSERT_EQ(buff.find("helloworld", 7), -1);
    ASSERT_EQ(buff.find("hellowo", 7), 0);
    ASSERT_EQ(buff.find("ellowo", 7), 1);
    ASSERT_EQ(buff.find("helloworld2", 7), -1);
    ASSERT_EQ(buff.find("elloworld", 7), -1);
    ASSERT_EQ(buff.find("hellow", 7), 0);
    ASSERT_EQ(buff.find("h", 7), 0);
    ASSERT_EQ(buff.find("l", 7), 2);
    ASSERT_EQ(buff.find("o", 7), 4);
    ASSERT_EQ(buff.find("lo", 7), 3);
    ASSERT_EQ(buff.find("or", 7), -1);

    ASSERT_EQ(buff.find("h", 0), -1);
  }

  TEST(Buffer, findStringAcrossMultipleSegments) {
    span::io::streams::Buffer buff("hello");
    buff.copyIn("world");
    buff.copyIn("foo");

    ASSERT_EQ(buff.segments(), 3u);
    ASSERT_EQ(buff.find("lloworldfo"), 2);
  }

  TEST(Buffer, findStringLongFalsePositive) {
    span::io::streams::Buffer buff("100000011");

    ASSERT_EQ(buff.find("000011"), 3);
  }

  TEST(Buffer, findStringFalsePositiveAcrossMultipleSegments) {
    span::io::streams::Buffer buff("10");
    buff.copyIn("00");
    buff.copyIn("00");
    buff.copyIn("00");
    buff.copyIn("11");

    ASSERT_EQ(buff.segments(), 5u);
    ASSERT_EQ(buff.find("000011"), 4);
  }

  TEST(Buffer, toString) {
    span::io::streams::Buffer buff;
    ASSERT_TRUE(buff.to_string().empty());
    buff.copyIn("hello");
    ASSERT_EQ(buff.to_string(), "hello");
    buff.copyIn("world");
    ASSERT_EQ(buff.to_string(), "helloworld");
    buff.consume(3);
    ASSERT_EQ(buff.to_string(), "loworld");
  }

  TEST(Buffer, reserveZero) {
    span::io::streams::Buffer buff;
    buff.reserve(0);
    ASSERT_EQ(buff.segments(), 0u);
  }

  TEST(Buffer, writeBufferZero) {
    span::io::streams::Buffer buff;
    iovec iov = buff.writeBuffer(0, true);
    ASSERT_EQ(iov.iov_len, 0u);
    ASSERT_EQ(buff.segments(), 0u);
  }

  TEST(Buffer, clearReadPortionOnly) {
    span::io::streams::Buffer buff;

    ASSERT_EQ(buff.readAvailable(), 0u);
    ASSERT_EQ(buff.writeAvailable(), 0u);

    buff.clear(false);

    ASSERT_EQ(buff.readAvailable(), 0u);
    ASSERT_EQ(buff.writeAvailable(), 0u);

    buff.copyIn("hello");
    buff.clear(false);

    ASSERT_EQ(buff.readAvailable(), 0u);
    ASSERT_EQ(buff.writeAvailable(), 0u);

    buff.copyIn("hello");
    buff.reserve(10);
    buff.clear(false);

    ASSERT_EQ(buff.readAvailable(), 0u);
    ASSERT_GE(buff.writeAvailable(), 10u);

    buff.copyIn("world");
    buff.clear(false);

    ASSERT_EQ(buff.readAvailable(), 0u);
    ASSERT_GE(buff.writeAvailable(), 5u);
  }
}  // namespace
