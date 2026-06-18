#include "ant_server/server.hpp"
int main() {
  struct Server server = Server(AF_INET, 8012, SOCK_STREAM, 0, 10, INADDR_ANY);
  server.launch();
  return 0;
}