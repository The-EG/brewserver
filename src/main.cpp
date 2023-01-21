#include "app.h"


int main(int argc, char *argv[]) {
    App::init(argc, argv);
    int r = App::run();
    App::cleanup();

    return r;
}