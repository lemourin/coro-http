#include <event2/event.h>
#include <event2/thread.h>
#include <gtest/gtest.h>

int main(int argc, char** argv) {
#ifdef _WIN32
  WORD version_requested = MAKEWORD(2, 2);
  WSADATA wsa_data;

  (void)WSAStartup(version_requested, &wsa_data);
  evthread_use_windows_threads();
#else
  evthread_use_pthreads();
#endif
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}