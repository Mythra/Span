#include "gtest/gtest.h"

#include <utility>

#include "span/fibers/Fiber.hh"
#include "span/fibers/Scheduler.hh"
#include "span/fibers/WorkerPool.hh"
#include "span/io/streams/Buffer.hh"
#include "span/io/streams/Stream.hh"
#include "span/io/streams/Pipe.hh"

namespace {
  TEST(PipeStream, basic) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    ASSERT_TRUE(pipe.first->supportsRead());
    ASSERT_TRUE(pipe.first->supportsWrite());
    ASSERT_FALSE(pipe.first->supportsSeek());
    ASSERT_FALSE(pipe.first->supportsSize());
    ASSERT_FALSE(pipe.first->supportsTruncate());
    ASSERT_FALSE(pipe.first->supportsFind());
    ASSERT_FALSE(pipe.first->supportsUnread());

    ASSERT_TRUE(pipe.second->supportsRead());
    ASSERT_TRUE(pipe.second->supportsWrite());
    ASSERT_FALSE(pipe.second->supportsSeek());
    ASSERT_FALSE(pipe.second->supportsSize());
    ASSERT_FALSE(pipe.second->supportsTruncate());
    ASSERT_FALSE(pipe.second->supportsFind());
    ASSERT_FALSE(pipe.second->supportsUnread());

    span::io::streams::Buffer read;
    ASSERT_EQ(pipe.first->write("a"), 1u);
    ASSERT_EQ(pipe.second->read(&read, 10), 1u);
    ASSERT_TRUE(read == "a");
    pipe.first->close();
    ASSERT_EQ(pipe.second->read(&read, 10), 0u);
  }

  static void basicInFibers(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(*sequence, 1);
    ASSERT_EQ(stream->write("a"), 1u);
    stream->close();
    stream->flush();
    ++*sequence;
    ASSERT_EQ(*sequence, 3);
  }

  TEST(PipeStream, basicInFibers) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();
    // pool must destruct before pipe, because wheen pool destructs
    // it waits for the other fiber to complete, which has a weak ref
    // to pipe.second; if pipe.second is gone, it will throw an
    // exception that we don't want.
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(
      new span::fibers::Fiber(std::bind(&basicInFibers, pipe.first, &sequence))
    ));

    span::io::streams::Buffer read;
    ASSERT_EQ(pipe.second->read(&read, 10), 1u);
    ASSERT_TRUE(read == "a");
    ++sequence;
    ASSERT_EQ(sequence, 2);
    ASSERT_EQ(pipe.second->read(&read, 10), 0u);
  }

  TEST(PipeStream, readerClosedOne) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    pipe.second->close();
    ASSERT_THROW(pipe.first->write("a"), std::runtime_error);
    pipe.first->flush();
  }

  TEST(PipeStream, readerClosedTwo) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    ASSERT_EQ(pipe.first->write("a"), 1u);
    pipe.second->close();
    ASSERT_THROW(pipe.first->flush(), std::runtime_error);
  }

  TEST(PipeStream, readerGone) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    pipe.second.reset();
    ASSERT_THROW(pipe.first->write("a"), std::runtime_error);
  }

  TEST(PipeStream, readerGoneFlush) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    ASSERT_EQ(pipe.first->write("a"), 1u);
    pipe.second.reset();
    ASSERT_THROW(pipe.first->flush(), std::runtime_error);
  }

  TEST(PipeStream, readerGoneReadEverything) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    pipe.second.reset();
    pipe.first->flush();
  }

  TEST(PipeStream, writerGone) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    pipe.first.reset();
    span::io::streams::Buffer buff;
    ASSERT_THROW(pipe.second->read(&buff, 10), std::runtime_error);
  }

  static void blockingRead(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(++*sequence, 2);
    ASSERT_EQ(stream->write("hello"), 5u);
    ASSERT_EQ(++*sequence, 3);
  }

  TEST(PipeStream, blockingRead) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(std::bind(
      &blockingRead, pipe.second, &sequence
    ))));

    span::io::streams::Buffer buff;
    ASSERT_EQ(pipe.first->read(&buff, 10), 5u);
    ASSERT_EQ(++sequence, 4);
    ASSERT_TRUE(buff == "hello");
  }

  static void blockingWrite(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(++*sequence, 3);
    span::io::streams::Buffer output;
    ASSERT_EQ(stream->read(&output, 10), 5u);
    ASSERT_TRUE(output == "hello");
  }

  TEST(PipeStream, blockingWrite) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream(5);
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(
      std::bind(&blockingWrite, pipe.second, &sequence)
    )));

    ASSERT_EQ(pipe.first->write("hello"), 5u);
    ASSERT_EQ(++sequence, 2);
    ASSERT_EQ(pipe.first->write("world"), 5u);
    ASSERT_EQ(++sequence, 4);

    span::io::streams::Buffer output;
    ASSERT_EQ(pipe.second->read(&output, 10), 5u);
    ASSERT_EQ(++sequence, 5);
    ASSERT_TRUE(output == "world");
  }

  TEST(PipeStream, oversizedWrite) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream(5);

    ASSERT_EQ(pipe.first->write("helloworld"), 5u);
    span::io::streams::Buffer output;
    ASSERT_EQ(pipe.second->read(&output, 10), 5u);
    ASSERT_TRUE(output == "hello");
  }

  static void closeOnBlockingReader(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(++*sequence, 2);
    stream->close();
    ASSERT_EQ(++*sequence, 3);
  }

  TEST(PipeStream, closeOnBlockingReader) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(std::bind(
      &closeOnBlockingReader, pipe.first, &sequence
    ))));

    span::io::streams::Buffer buff;
    ASSERT_EQ(pipe.second->read(&buff, 10), 0u);
    ASSERT_EQ(++sequence, 4);
  }

  static void closeOnBlockingWriter(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(++*sequence, 3);
    stream->close();
    ASSERT_EQ(++*sequence, 4);
  }

  TEST(PipeStream, closeOnBlockingWriter) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream(5);
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(std::bind(
      &closeOnBlockingWriter, pipe.first, &sequence
    ))));

    ASSERT_EQ(pipe.second->write("hello"), 5u);
    ASSERT_EQ(++sequence, 2);
    ASSERT_THROW(pipe.second->write("world"), std::runtime_error);
    ASSERT_EQ(++sequence, 5);
  }

  static void destructOnBlockingReader(std::weak_ptr<span::io::streams::Stream> weakStream, int *sequence) {
    span::io::streams::Stream::ptr stream(weakStream);
    span::fibers::Fiber::yield();
    ASSERT_EQ(++*sequence, 2);
    ASSERT_TRUE(stream.unique());
    stream.reset();
    ASSERT_EQ(++*sequence, 3);
  }

  TEST(PipeStream, destructOnBlockingReader) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();
    span::fibers::WorkerPool pool;
    int sequence = 1;

    span::fibers::Fiber::ptr fiber = span::fibers::Fiber::ptr(new span::fibers::Fiber(std::bind(
      &destructOnBlockingReader, std::weak_ptr<span::io::streams::Stream>(pipe.first), &sequence
    )));
    fiber->call();
    pipe.first.reset();
    pool.schedule(fiber);

    span::io::streams::Buffer output;
    ASSERT_THROW(pipe.second->read(&output, 10), std::runtime_error);
    ASSERT_EQ(++sequence, 4);
  }

  static void destructOnBlockingWriter(std::weak_ptr<span::io::streams::Stream> weakPtr, int *sequence) {
    span::io::streams::Stream::ptr stream(weakPtr);
    span::fibers::Fiber::yield();
    ASSERT_EQ(++*sequence, 3);
    ASSERT_TRUE(stream.unique());
    stream.reset();
    ASSERT_EQ(++*sequence, 4);
  }

  TEST(PipeStream, destructOnBlockingWriter) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream(5);
    span::fibers::WorkerPool pool;
    int sequence = 1;

    span::fibers::Fiber::ptr fiber = span::fibers::Fiber::ptr(new span::fibers::Fiber(
      std::bind(&destructOnBlockingWriter, std::weak_ptr<span::io::streams::Stream>(pipe.first), &sequence)
    ));
    fiber->call();
    pipe.first.reset();
    pool.schedule(fiber);

    ASSERT_EQ(pipe.second->write("hello"), 5u);
    ASSERT_EQ(++sequence, 2);
    ASSERT_THROW(pipe.second->write("world"), std::runtime_error);
    ASSERT_EQ(++sequence, 5);
  }

  static void cancelOnBlockingReader(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(++*sequence, 2);
    stream->cancelRead();
    ASSERT_EQ(++*sequence, 3);
  }

  TEST(PipeStream, cancelOnBlockingReader) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(std::bind(
      &cancelOnBlockingReader, pipe.first, &sequence
    ))));

    span::io::streams::Buffer output;
    ASSERT_THROW(pipe.first->read(&output, 10), std::runtime_error);
    ASSERT_EQ(++sequence, 4);
    ASSERT_THROW(pipe.first->read(&output, 10), std::runtime_error);
  }

  static void cancelOnBlockingWriter(span::io::streams::Stream::ptr stream, int *sequence) {
    ASSERT_EQ(++*sequence, 2);
    char buff[4096];
    memset(buff, 0, sizeof(buff));
    ASSERT_THROW(while (true) { stream->write(&buff, 4096); }, std::runtime_error);
    ASSERT_EQ(++*sequence, 4);
    ASSERT_THROW(stream->write(&buff, 4096), std::runtime_error);
  }

  TEST(PipeStream, cancelOnBlockingWriter) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream(5);
    span::fibers::WorkerPool pool;
    int sequence = 1;

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(
      std::bind(&cancelOnBlockingWriter, pipe.first, &sequence)
    )));
    span::fibers::Scheduler::yield();
    ASSERT_EQ(++sequence, 3);
    pipe.first->cancelWrite();
    pool.dispatch();
    ASSERT_EQ(++sequence, 5);
  }

  void threadStress(span::io::streams::Stream::ptr stream) {
    size_t totalRead = 0;
    size_t totalWritten = 0;
    size_t buff[64];
    span::io::streams::Buffer buffer;
    for (int idx = 0; idx < 10000; ++idx) {
      if (idx % 2) {
        size_t toRead = 64;
        size_t read = stream->read(&buffer, toRead * sizeof(size_t));
        ASSERT_EQ(read % sizeof(size_t), 0);
        buffer.copyOut(&buff, read);
        for (size_t jdx = 0; read > 0; read -= sizeof(size_t), ++jdx) {
          ASSERT_EQ(buff[jdx], ++totalRead);
        }
        buffer.clear();
      } else {
        size_t toWrite = 64;
        for (size_t jdx = 0; jdx < toWrite; ++jdx) {
          buff[jdx] = ++totalWritten;
        }
        buffer.copyIn(&buff, toWrite * sizeof(size_t));
        size_t written = stream->write(&buffer, toWrite * sizeof(size_t));
        totalWritten -= (toWrite - written / sizeof(size_t));
        buffer.clear();
      }
    }

    stream->close(span::io::streams::Stream::WRITE);

    while (true) {
      size_t toRead = 64;
      size_t read = stream->read(&buffer, toRead);
      if (read == 0) {
        break;
      }
      ASSERT_EQ(read % sizeof(size_t), 0);
      buffer.copyOut(&buff, read);
      for (size_t idx = 0; read > 0; read -= sizeof(size_t), ++idx) {
        ASSERT_EQ(buff[idx], ++totalRead);
      }
      buffer.clear();
    }

    stream->flush();
  }

  TEST(PipeStream, threadStress) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();
    span::fibers::WorkerPool pool(2);

    pool.schedule(span::fibers::Fiber::ptr(new span::fibers::Fiber(
      std::bind(&threadStress, pipe.first)
    )));
    threadStress(pipe.second);
  }

  static void closed(bool *remoteClosed) {
    *remoteClosed = true;
  }

  TEST(PipeStream, eventOnRemoteClose) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    bool remoteClosed = false;
    pipe.first->onRemoteClose(std::bind(&closed, &remoteClosed));
    pipe.second->close();
    ASSERT_TRUE(remoteClosed);
  }

  TEST(PipeStream, eventOnRemoteReset) {
    std::pair<
      span::io::streams::Stream::ptr,
      span::io::streams::Stream::ptr
    > pipe = span::io::streams::pipeStream();

    bool remoteClosed = false;
    pipe.first->onRemoteClose(std::bind(&closed, &remoteClosed));
    pipe.second.reset();
    ASSERT_TRUE(remoteClosed);
  }
}  // namespace
