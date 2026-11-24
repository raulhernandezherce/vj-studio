#include <cstdio>
#include <string>

bool writeFixtureVideo(const std::string& path);

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "uso: vj_make_fixture salida.mp4\n");
    return 2;
  }
  return writeFixtureVideo(argv[1]) ? 0 : 1;
}
