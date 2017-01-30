#include "CLI.hpp"


int main (int argc, char** argv) {

    CLI::App app("K3Pi goofit fitter");

    std::string file;
    app.add_option("f,file", file, "File name");
    
    int count;
    app.add_flag("c,count", count, "File name");

    CLI::Return ret = app.start(argc, argv);
    if(ret != CLI::Return::Continue)
        return (int) ret;

    std::cout << "Working on file: " << file << ", direct count: " << app.count("file") << std::endl;
    std::cout << "Working on count: " << count << ", direct count: " << app.count("count") << std::endl;

    return 0;
}
