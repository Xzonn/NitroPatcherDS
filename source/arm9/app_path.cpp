#include "app_path.h"
#include "../patch/core.h"

namespace np3d {
std::string appPath(int argc, char** argv) {
    require(argc>0 && argv && argv[0] && argv[0][0],"Cannot determine the app path.");
    std::string filename=argv[0];
    if (filename.compare(0,5,"fat:/")==0) filename="sd:"+filename.substr(4);
    if (filename.compare(0,6,"sdmc:/")==0) filename="sd:"+filename.substr(5);
    require(filename.compare(0,4,"sd:/")==0,"Start this app from the console SD card.");
    return filename;
}
}
