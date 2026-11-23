//    Copyright (C) 2026  Armin Felder
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <https://www.gnu.org/licenses/>.

//
// Created by armin on 23.09.25.
//

#include <cstdlib>
#include <exception>
#include <iostream>

#include "BrThermal.h"

int main(const int argc, char **argv)
{
    // An exception that escapes a C++ callback cannot unwind through libpappl.
    // Leave one line that names the cause before the process ends.
    std::set_terminate([]
    {
        std::cerr << "br-thermal: terminating, unhandled exception in a driver callback\n";
        std::abort();
    });

    BrThermal driver;

    driver.runServer(argc, argv);

    return 0;
}
