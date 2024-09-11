#include "Launcher.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include <boost/program_options.hpp>

namespace fs = std::filesystem;
using std::string;
using std::vector;

Launcher::Launcher(boost::program_options::variables_map &options)
    : options(options)
{
    string fn = options["application"].as<std::string>();
    
    if(fn == "-")
    {
        std::stringstream tmp;
        tmp << std::cin.rdbuf();
        if(!app.read(tmp, ResourceFile::Format::macbin))
            throw std::runtime_error("Could not load application from stdin.");
    }
    else
    {
        if(!app.read(fn))
            throw std::runtime_error("Could not load application file.");
    }

    auto suffix = "launchappl." + std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    tempDir = fs::temp_directory_path() / suffix;
    fs::create_directories(tempDir);

    appPath = tempDir / "Application";
    outPath = tempDir / "out";

    std::ofstream out(outPath);
}

Launcher::Launcher(boost::program_options::variables_map &options, ResourceFile::Format f)
    : Launcher(options)
{
    app.write(appPath.string(), f);
}

void Launcher::DumpOutput()
{
    std::ifstream in(outPath);
    std::cout << in.rdbuf();
}

Launcher::~Launcher()
{
    fs::remove_all(tempDir);
}



