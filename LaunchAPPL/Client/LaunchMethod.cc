#include "LaunchMethod.h"

LaunchMethod::LaunchMethod()
{

}

LaunchMethod::~LaunchMethod()
{

}

void LaunchMethod::GetOptions(boost::program_options::options_description &)
{
}

bool LaunchMethod::CheckPlatform()
{
    return true;
}

bool LaunchMethod::CheckOptions(boost::program_options::variables_map &)
{
    return true;
}
