#ifndef SAVING_HPP
#define SAVING_HPP

#include <filesystem>
#include <string>
#include "../include/library.hpp"

class library;
class database;



void save_library(const library &lib, const std::filesystem::path &filename);

bool load_library(library &lib, const std::filesystem::path &filename);

bool open_library(library &lib, database &db, const std::filesystem::path &import_filename);


#endif