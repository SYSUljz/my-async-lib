#include "ant_server/server.hpp"
int main() {
  Context context(256);
  Server server = Server(context, AF_INET, 8012, SOCK_STREAM, 0, 10, INADDR_ANY);
  context.Start();
  return 0;
}