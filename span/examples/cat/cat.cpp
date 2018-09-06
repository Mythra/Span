#include <iostream>
#include <exception>

#include "span/io/streams/File.hh"
#include "span/io/streams/Std.hh"
#include "span/io/streams/Transfer.hh"
#include "span/fibers/WorkerPool.hh"

int main(int argc, const char * const argv[]) {
  try {
    span::io::streams::StdoutStream stdoutStream;
    span::fibers::WorkerPool pool(2);
    for (int idx = 1; idx < argc; ++idx) {
      span::io::streams::Stream::ptr inStream;
      std::string arg(argv[idx]);
      if (arg == "-") {
        inStream.reset(new span::io::streams::StdinStream());
      } else {
        inStream.reset(new span::io::streams::FileStream(arg, span::io::streams::FileStream::READ));
      }
      span::io::streams::transferStream(inStream.get(), &stdoutStream);
    }
  } catch (std::exception& e) {
    std::cerr << "Exception!" << std::endl;
  }
  return 0;
}
