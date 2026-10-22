#ifndef SAVING_HPP
#define SAVING_HPP

#include <string>
#include "../include/library.hpp"

class library;
class database;



void save_library(const library &lib, const std::string &filename);

void load_library(library &lib, const std::string &filename);

bool open_library(library &lib, database &db, const std::string &import_filename);


#endif